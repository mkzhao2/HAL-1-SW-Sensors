#!/usr/bin/env python3
"""
Allan deviation of the gyros in an AllanTest log (ALLAN.CSV), in physical units (deg/s).

Edit the constants below, then run:
    python allan_deviation.py                    # analyze the FILE setting
    python allan_deviation.py data/run2.CSV      # analyze another log

For each IMU and gyro axis it prints:
  - ARW (angle random walk): from the -1/2 slope region of the curve (its value at tau = 1 s)
  - bias instability: from the minimum of the curve (sigma_min / 0.664)
and plots the Allan deviation curves (log-log) with the ARW lines.

Notes:
  - Look at the data with visualization.py first, then list only the clean parts in KEEP
    (leave out warm-up, bumps, and the end of the run where the board is picked up).
  - Each KEEP segment is analyzed separately and the results are pooled, so no averaging
    window ever spans a gap: cutting out the middle of a run doesn't bias the result.
    The longest usable tau is limited by the longest segment.
  - tau uses each IMU's real sample rate, measured against the MCU clock. The BMI323s'
    internal oscillators are ~1% off 200 Hz, and sens_time can't show that because it runs
    on the same oscillator as the sampling.
"""

import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from visualization import (IMU_COLORS, IMU_NAMES, load_run, sample_intervals_ms,  # noqa: E402
                           scan_runs, to_physical)

# ── Settings ────────────────────────────────────────────────────────────────
FILE = "data/ALLAN.CSV"   # relative to this script's folder, or an absolute path
RUN = None                # run number (python visualization.py ALLAN.CSV --list); None = last
KEEP = None               # clean segments in seconds since run start: [(start, end), ...]
                          #   e.g. KEEP = [(300, 2500), (2600, 7000)];  None = whole run
IMUS = ["A", "B", "C"]
AXES = [("gx", "Gyro X"), ("gy", "Gyro Y"), ("gz", "Gyro Z")]
POINTS_PER_DECADE = 20    # tau resolution of the curve
MIN_CLUSTERS = 10         # tau points with fewer independent averaging windows are shown faded
                          #   and not used for the ARW / bias instability estimates
SAVE_PLOT = None          # e.g. "allan.png" to also save the figure

BI_FACTOR = 0.664         # flat (flicker) region: sigma = 0.664 * bias instability


def avar_terms(y, tau0, ms):
    """For one contiguous segment of rate samples y: the sum of squared overlapping
    second differences of the integrated angle, and how many terms, for each m."""
    x = np.concatenate(([0.0], np.cumsum(y))) * tau0      # integrated angle [deg]
    sums = np.zeros(len(ms))
    counts = np.zeros(len(ms), dtype=np.int64)
    for k, m in enumerate(ms):
        if 2 * m > len(y):
            break
        d = x[2 * m:] - 2 * x[m:-m] + x[:-2 * m]
        sums[k] = np.dot(d, d)
        counts[k] = len(d)
    return sums, counts


def allan_deviation(segments, tau0):
    """Overlapping Allan deviation pooled over separate segments (no window spans a gap).
    Returns tau [s], adev [deg/s], and whether each point has >= MIN_CLUSTERS windows."""
    n_max = max(len(s) for s in segments)
    ms = np.unique(np.round(np.logspace(0, np.log10(max(n_max // 2, 1)),
                                        int(POINTS_PER_DECADE * np.log10(max(n_max, 10))))
                            ).astype(np.int64))
    sums = np.zeros(len(ms))
    counts = np.zeros(len(ms), dtype=np.int64)
    clusters = np.zeros(len(ms), dtype=np.int64)
    for y in segments:
        s, c = avar_terms(y, tau0, ms)
        sums += s
        counts += c
        clusters += len(y) // ms
    valid = counts > 0
    tau = ms[valid] * tau0
    adev = np.sqrt(sums[valid] / (2.0 * tau**2 * counts[valid]))
    return tau, adev, clusters[valid] >= MIN_CLUSTERS


def estimate_arw(tau, adev, ok):
    """ARW coefficient N [deg/sqrt(s)]: sigma * sqrt(tau) where the slope is about -1/2."""
    t, a = tau[ok], adev[ok]
    if len(t) < 2:
        return np.nan
    slope = np.gradient(np.log10(a), np.log10(t))
    sel = np.abs(slope + 0.5) < 0.15
    if not sel.any():                       # no clean -1/2 region: closest point
        sel = np.abs(slope + 0.5) == np.min(np.abs(slope + 0.5))
    return float(np.median(a[sel] * np.sqrt(t[sel])))


def estimate_bias_instability(tau, adev, ok):
    """(bias instability [deg/s], tau at the minimum [s], minimum reached?)"""
    t, a = tau[ok], adev[ok]
    if len(t) == 0:
        return np.nan, np.nan, False
    i = int(np.argmin(a))
    # Only a real minimum if the curve clearly rises again afterwards (>= 10%); otherwise it
    # is still falling (or flat noise at the end of the trusted range): run longer
    reached = bool(np.any(a[i + 1:] >= 1.1 * a[i]))
    return float(a[i] / BI_FACTOR), float(t[i]), reached


def gap_warnings(d, odr_hz):
    """Dropped lines / missed samples inside a segment (they shift the timing slightly)."""
    nominal_ms = 1000.0 / odr_hz
    dcount = np.diff(d["count"].to_numpy())
    steps = np.round(sample_intervals_ms(d["sens_time"]) / nominal_ms)
    return int(np.sum(np.clip(steps - dcount, 0, None))), int(np.sum(dcount[dcount > 1] - 1))


def main():
    parser = argparse.ArgumentParser(description="Allan deviation of the gyros in an AllanTest log.")
    parser.add_argument("file", nargs="?", type=Path,
                        help=f"CSV log to analyze (default: FILE setting, {FILE})")
    args = parser.parse_args()

    if args.file is not None:
        path = args.file                         # relative to where you run the command
    else:
        path = Path(FILE) if Path(FILE).is_absolute() else Path(__file__).resolve().parent / FILE
    if not path.is_file():
        sys.exit(f"File not found: {path}")
    runs = scan_runs(path)
    if not runs:
        sys.exit(f"No runs found in {path}")
    run_no = RUN if RUN is not None else runs[-1].index
    if not 1 <= run_no <= len(runs):
        sys.exit(f"Run {run_no} doesn't exist: the file has runs 1-{len(runs)}")
    run = runs[run_no - 1]
    phys = to_physical(load_run(path, run), run.params)
    odr_hz = run.params.get("odr_hz", 200.0)
    windows = KEEP if KEEP is not None else [(0.0, float(phys["t"].max()) + 1.0)]

    print(f"{path.name}, run {run_no} of {len(runs)}, {run.params}")
    print(f"Segments (s): {windows}")

    fig, axs = plt.subplots(1, len(AXES), figsize=(16, 6), sharey=True)
    axs = np.atleast_1d(axs)
    results = []
    for imu_name in IMUS:
        i = IMU_NAMES.index(imu_name)
        d_imu = phys[phys["imu"] == i]
        if len(d_imu) < 2:
            print(f"IMU {imu_name}: no data")
            continue
        # Real sample rate against the MCU clock, over the whole run
        rate = (len(d_imu) - 1) / (d_imu["t"].iloc[-1] - d_imu["t"].iloc[0])
        segs = [d_imu[(d_imu["t"] >= s) & (d_imu["t"] < e)] for s, e in windows]
        segs = [s for s in segs if len(s) >= 4]
        if not segs:
            print(f"IMU {imu_name}: no data inside KEEP")
            continue
        missed, dropped = map(sum, zip(*(gap_warnings(s, odr_hz) for s in segs)))
        kept_s = sum(len(s) for s in segs) / rate
        print(f"IMU {imu_name}: rate {rate:.2f} Hz, {kept_s:.1f} s kept in {len(segs)} segment(s), "
              f"longest {max(len(s) for s in segs) / rate:.1f} s"
              + (f"  WARNING: {missed} missed samples, {dropped} dropped lines inside KEEP"
                 if missed or dropped else ""))

        for ax, (col, axis_name) in zip(axs, AXES):
            tau, adev, ok = allan_deviation([s[col].to_numpy() for s in segs], 1.0 / rate)
            arw = estimate_arw(tau, adev, ok)
            bi, bi_tau, bi_reached = estimate_bias_instability(tau, adev, ok)
            results.append((imu_name, axis_name, arw, bi, bi_tau, bi_reached))

            color = IMU_COLORS[i]
            ax.loglog(tau[ok], adev[ok], "-", color=color, lw=1.5, label=f"IMU {imu_name}")
            if (~ok).any():
                ax.loglog(tau[~ok], adev[~ok], ":", color=color, lw=1, alpha=0.5)
            if np.isfinite(arw):
                ax.loglog(tau, arw / np.sqrt(tau), "--", color=color, lw=0.8, alpha=0.8,
                          label=f"ARW {imu_name}: {arw * 60:.3f} deg/sqrt(h)")
            if np.isfinite(bi):
                ax.loglog([bi_tau], [bi * BI_FACTOR], "o", color=color, ms=6,
                          mfc="white" if not bi_reached else color)

    for ax, (_, axis_name) in zip(axs, AXES):
        ax.set_title(axis_name)
        ax.set_xlabel("tau [s]")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=7)
    axs[0].set_ylabel("Allan deviation [deg/s]")
    fig.suptitle(f"{path.name} run {run_no}, segments {windows}\n"
                 "solid = Allan deviation, dotted = too few windows to trust, "
                 "dashed = ARW line (slope -1/2), dot = bias-instability minimum "
                 "(hollow = minimum not reached)", fontsize=9)
    fig.tight_layout()

    print("\n IMU  axis      ARW [deg/sqrt(h)]   bias instability [deg/h]   (at tau)")
    for imu_name, axis_name, arw, bi, bi_tau, bi_reached in results:
        bi_text = (f"{bi * 3600:10.3f}" if bi_reached else f"   < {bi * 3600:.3f}")
        print(f"  {imu_name}   {axis_name:7s}   {arw * 60:12.4f}        {bi_text:>14s}"
              f"             ({bi_tau:.2f} s)"
              + ("" if bi_reached else "  minimum not reached: run longer"))

    if SAVE_PLOT:
        out = Path(SAVE_PLOT)
        fig.savefig(out, dpi=120)
        print(f"\nSaved {out.resolve()}")
    plt.show()


if __name__ == "__main__":
    main()
