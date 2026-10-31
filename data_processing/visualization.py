#!/usr/bin/env python3
"""
Plot ALLAN.CSV logs from the AllanTest firmware (Core/Src/AllanTest.cpp) in physical units.

Usage:
    python visualization.py ALLAN.CSV              # plot the last run in the file
    python visualization.py ALLAN.CSV --list       # list the runs in the file
    python visualization.py ALLAN.CSV --run 1      # plot run 1 (numbered from 1)
    python visualization.py ALLAN.CSV --avg 1      # average into 1 s blocks (faster for long runs)
    python visualization.py ALLAN.CSV --imu A C    # only IMUs A and C
    python visualization.py ALLAN.CSV --save plots # save PNGs to plots/ instead of opening a window

Opens one window (pyqtgraph) with a tab per chart; Ctrl+Tab / Ctrl+Shift+Tab switch tabs.
  - mouse wheel:  zoom time (the y axis fits the visible data automatically)
  - left drag:    pan
  - Home key, the "A" button (bottom-left corner), or right click > View All: show the whole run
  - right click:  menu (e.g. "Y axis" for a manual y range)
The time axis is linked across all tabs, so zooming into an event on one tab shows the same
span on the others. Large runs stay fast: only the visible data is drawn, reduced to the
screen's resolution while keeping peaks (spikes stay visible).
--save writes PNGs with matplotlib instead.

Units: gyro in deg/s, accel in g, temperature in deg C, time since the start of the run.
Scale factors are read from each run's header, so this works for any range settings.
"""

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import pandas as pd

RUN_MARKER = "=== ALLAN RUN START ==="
FULL_SCALE_LSB = 32768          # 16-bit signed: +-full scale = +-32768 counts
SENS_TIME_TICK_S = 39.0625e-6   # BMI323 sensor time resolution
SENS_TIME_WRAP = 2**32          # sens_time is a 32-bit counter
IMU_NAMES = ["A", "B", "C"]     # imu column 0, 1, 2 = BMI323 on CS1, CS2, CS3
IMU_COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c"]   # blue, orange, green


@dataclass
class Run:
    index: int                                   # 1-based, in file order
    header_line: int                             # 0-based line number of the CSV column header
    end_line: int = -1                           # 0-based line number just past the run
    params: dict = field(default_factory=dict)   # from "# key=value" header lines
    columns: list = field(default_factory=list)


def scan_runs(path):
    """Find every run in the file: its header parameters and line range."""
    runs, current, pending = [], None, None
    n = -1
    with open(path, "r", errors="replace") as f:
        for n, line in enumerate(f):
            s = line.strip()
            if s == RUN_MARKER:
                if current is not None:
                    current.end_line = n
                    runs.append(current)
                current, pending = None, {}
            elif pending is not None:            # between a marker and its column header
                if s.startswith("#"):
                    for key, value in re.findall(r"(\w+)=([-+\d.]+)", s):
                        pending[key] = float(value)
                elif s.startswith("imu,"):
                    current = Run(index=len(runs) + 1, header_line=n,
                                  params=pending, columns=s.split(","))
                    pending = None
    if current is not None:
        current.end_line = n + 1
        runs.append(current)
    return runs


def load_run(path, run):
    """Read one run's data rows as numbers. Drops malformed rows."""
    df = pd.read_csv(path, skiprows=run.header_line, nrows=run.end_line - run.header_line - 1,
                     header=0, comment="#", on_bad_lines="skip", encoding_errors="replace",
                     low_memory=False)
    # Columns are parsed as numbers directly (fast); only a column that picked up junk
    # (e.g. the next run's marker line) comes back as text and needs converting
    for col in df.columns:
        if df[col].dtype == object:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna()
    # The last line of a run may have been cut short by a power loss: drop it to be safe
    return df.iloc[:-1].reset_index(drop=True)


def to_physical(df, params):
    """Raw counts -> deg/s, g, deg C. Time in seconds since the start of the run."""
    if "t_us" in df.columns:
        t = df["t_us"] / 1e6
    elif "tick_ms" in df.columns:       # older firmware logged milliseconds
        t = df["tick_ms"] / 1e3
    else:
        sys.exit("No time column (t_us or tick_ms) found")

    gyr_scale = params.get("gyr_range_dps", 125.0) / FULL_SCALE_LSB
    acc_scale = params.get("acc_range_g", 2.0) / FULL_SCALE_LSB
    return pd.DataFrame({
        "imu":       df["imu"].astype(int),
        "count":     df["count"].astype(np.int64),
        "sens_time": df["sens_time"].astype(np.int64),
        "t":         t - t.min(),
        "gx": df["gx"] * gyr_scale, "gy": df["gy"] * gyr_scale, "gz": df["gz"] * gyr_scale,
        "ax": df["ax"] * acc_scale, "ay": df["ay"] * acc_scale, "az": df["az"] * acc_scale,
        "temp_c": df["temp_raw"] / 512.0 + 23.0,
    })


def sample_intervals_ms(sens_time):
    """Time between consecutive samples from the BMI323's own clock (wrap-safe)."""
    ticks = np.diff(sens_time.to_numpy(dtype=np.int64)) % SENS_TIME_WRAP
    return ticks * SENS_TIME_TICK_S * 1e3


def block_average(d, seconds):
    """Average one IMU's data into fixed time blocks (counters keep the first value)."""
    groups = d.groupby((d["t"] // seconds).astype(np.int64))
    out = groups.mean()
    for col in ("imu", "count", "sens_time"):
        out[col] = groups[col].first()
    return out.reset_index(drop=True)


def print_summary(run, phys, odr_hz):
    nominal_ms = 1000.0 / odr_hz
    print(f"Run {run.index}: {run.params}")
    for i in sorted(phys["imu"].unique()):
        d = phys[phys["imu"] == i]
        # Sensor periods between consecutive logged lines vs. how many lines that should be:
        # extra sensor periods not explained by a count step = samples never read
        sensor_steps = np.round(sample_intervals_ms(d["sens_time"]) / nominal_ms)
        dcount = np.diff(d["count"].to_numpy())
        missed = int(np.sum(np.clip(sensor_steps - dcount, 0, None)))
        dropped = int(np.sum(dcount[dcount > 1] - 1))
        duration = d["t"].iloc[-1] - d["t"].iloc[0]
        print(f"  IMU {IMU_NAMES[i]}: {len(d)} samples over {duration:.1f} s "
              f"({len(d) / max(duration, 1e-9):.1f} Hz), "
              f"missed by sensor clock: {missed}, dropped lines: {dropped}, "
              f"temp {d['temp_c'].min():.2f}-{d['temp_c'].max():.2f} C")


@dataclass
class Chart:
    name: str        # tab name
    key: str         # PNG file name part
    title: str
    ylabel: str
    xlabel: str
    series: list     # [(legend label, color, x array, y array)]


def build_charts(run, phys, imus, avg_s, title):
    """Everything to draw, one Chart per tab. Shared by the window and --save."""
    duration = phys["t"].max()
    t_div, t_unit = (60.0, "min") if duration > 20 * 60 else (1.0, "s")
    odr_hz = run.params.get("odr_hz", 200.0)

    def series(col):
        return lambda d: (d["t"].to_numpy() / t_div, d[col].to_numpy())

    def intervals(d):
        return d["t"].to_numpy()[1:] / t_div, sample_intervals_ms(d["sens_time"])

    def count_steps(d):
        return d["t"].to_numpy()[1:] / t_div, np.diff(d["count"].to_numpy()).astype(float)

    # (tab name, file key, y label, function(data of one IMU) -> (x, y), use averaging?)
    specs = [
        ("Gyro X", "gyro_x", "Gyro X [deg/s]", series("gx"), True),
        ("Gyro Y", "gyro_y", "Gyro Y [deg/s]", series("gy"), True),
        ("Gyro Z", "gyro_z", "Gyro Z [deg/s]", series("gz"), True),
        ("Accel X", "accel_x", "Accel X [g]", series("ax"), True),
        ("Accel Y", "accel_y", "Accel Y [g]", series("ay"), True),
        ("Accel Z", "accel_z", "Accel Z [g]", series("az"), True),
        ("Temperature", "temp", "IMU die temperature [C]", series("temp_c"), True),
        # Timing diagnostics always use unaveraged data
        ("Sample interval", "interval", f"Sample interval, sensor clock [ms]"
         f"  (expected {1000.0 / odr_hz:.2f})", intervals, False),
        ("Count step", "count_step", "count step (1 = no drop)", count_steps, False),
    ]
    notes = {
        "interval": "spike here but not in Count step = sample never read from the sensor",
        "count_step": "spike here (and in Sample interval) = line dropped by the logger",
    }

    per_imu = {i: phys[phys["imu"] == i] for i in imus}
    averaged = {i: block_average(d, avg_s) for i, d in per_imu.items()} if avg_s else per_imu

    charts = []
    for name, key, ylabel, xy, use_avg in specs:
        series_list = []
        for i in imus:
            x, y = xy(averaged[i] if use_avg else per_imu[i])
            series_list.append((f"IMU {IMU_NAMES[i]}", IMU_COLORS[i], x, y))
        full_title = f"{name}  |  {title}" + (f"\n{notes[key]}" if key in notes else "")
        charts.append(Chart(name, key, full_title, ylabel,
                            f"Time since run start [{t_unit}]", series_list))
    return charts


def show_window(charts, window_title):
    """One pyqtgraph window, a tab per chart, time axis linked across tabs."""
    import pyqtgraph as pg
    from pyqtgraph.Qt import QtGui, QtWidgets

    app = pg.mkQApp(window_title)
    pg.setConfigOptions(background="w", foreground="k", antialias=False)

    tabs = QtWidgets.QTabWidget()                # Ctrl+Tab / Ctrl+Shift+Tab switch tabs
    tabs.setWindowTitle(window_title)
    master, widgets = None, []
    for chart in charts:
        pw = pg.PlotWidget(title=chart.title.replace("\n", "<br>"))
        pw.setLabel("left", chart.ylabel)
        pw.setLabel("bottom", chart.xlabel)
        for axis in ("left", "bottom"):              # show real values: no "(x0.001)" rescaling
            pw.getAxis(axis).enableAutoSIPrefix(False)
        pw.showGrid(x=True, y=True, alpha=0.3)
        pw.addLegend(offset=(-10, 10))           # top right
        # Draw only what's visible, reduced to screen resolution; "peak" keeps spikes
        pw.setDownsampling(auto=True, mode="peak")
        pw.setClipToView(True)
        # Mouse zooms/pans time only; y always fits the visible data
        pw.setMouseEnabled(x=True, y=False)
        pw.getViewBox().setAutoVisible(y=True)
        pw.enableAutoRange(axis="y")
        for label, color, x, y in chart.series:
            pw.plot(x, y, pen=pg.mkPen(color, width=1), name=label)
        if master is None:
            master = pw
        else:
            pw.setXLink(master)                  # linked time axis across all tabs
        tabs.addTab(pw, chart.name)
        widgets.append(pw)

    # pyqtgraph's own "View All" only fits the data currently on screen when clip-to-view
    # is on, so reset to the full run explicitly: right-click > View All, the "A" button
    # in the bottom-left corner, or the Home key.
    xs = [x for chart in charts for _, _, x, _ in chart.series if len(x)]
    x_full = (min(float(x[0]) for x in xs), max(float(x[-1]) for x in xs))

    def reset_view(*_):                          # signals pass extra args (button, checked)
        master.setXRange(*x_full, padding=0.02)
        for w in widgets:
            w.enableAutoRange(axis="y")

    for pw in widgets:
        pw.getViewBox().menu.viewAll.triggered.disconnect()
        pw.getViewBox().menu.viewAll.triggered.connect(reset_view)
        pw.getPlotItem().autoBtn.clicked.disconnect()
        pw.getPlotItem().autoBtn.clicked.connect(reset_view)
    QtGui.QShortcut(QtGui.QKeySequence("Home"), tabs, reset_view)   # Qt6: QShortcut is in QtGui

    tabs.resize(1400, 850)
    tabs.show()
    app.exec()


def save_pngs(charts, out_dir, run_index):
    """Static PNGs (matplotlib), one per chart."""
    import matplotlib
    matplotlib.use("Agg")
    from matplotlib.figure import Figure
    # Huge line plots: split paths into chunks so Agg doesn't overflow, simplify aggressively
    matplotlib.rcParams["agg.path.chunksize"] = 10000
    matplotlib.rcParams["path.simplify_threshold"] = 1.0

    out_dir.mkdir(parents=True, exist_ok=True)
    for chart in charts:
        fig = Figure(figsize=(13, 6), layout="constrained")
        ax = fig.add_subplot()
        for label, color, x, y in chart.series:
            ax.plot(x, y, lw=0.6, color=color, label=label)
        ax.set_title(chart.title, fontsize=10)
        ax.set_ylabel(chart.ylabel)
        ax.set_xlabel(chart.xlabel)
        ax.grid(True, alpha=0.3)
        ax.ticklabel_format(axis="y", useOffset=False)   # real values, not "+1" offsets
        for line in ax.legend(loc="upper right", fontsize=8, ncol=3).get_lines():
            line.set_linewidth(2.5)                      # thick enough to tell colors apart
        out = out_dir / f"run{run_index}_{chart.key}.png"
        fig.savefig(out, dpi=120)
        print(f"Saved {out}")


def main():
    parser = argparse.ArgumentParser(description="Plot AllanTest logs (ALLAN.CSV) in physical units.")
    parser.add_argument("file", type=Path, help="path to ALLAN.CSV")
    parser.add_argument("--list", action="store_true", help="list runs in the file and exit")
    parser.add_argument("--run", type=int, help="run number to plot (default: last run)")
    parser.add_argument("--avg", type=float, metavar="SECONDS",
                        help="average into blocks of this many seconds (shows slow trends)")
    parser.add_argument("--imu", nargs="+", choices=IMU_NAMES, default=IMU_NAMES,
                        help="which IMUs to plot (default: all)")
    parser.add_argument("--save", type=Path, metavar="DIR",
                        help="save PNGs to this folder instead of opening a window")
    args = parser.parse_args()

    runs = scan_runs(args.file)
    if not runs:
        sys.exit(f"No runs found in {args.file}")

    if args.list:
        for r in runs:
            print(f"Run {r.index}: lines {r.header_line + 2}-{r.end_line}, "
                  f"~{r.end_line - r.header_line - 1} rows, {r.params}")
        return

    run_no = args.run if args.run is not None else runs[-1].index
    if not 1 <= run_no <= len(runs):
        sys.exit(f"Run {run_no} doesn't exist: the file has runs 1-{len(runs)}")
    run = runs[run_no - 1]

    phys = to_physical(load_run(args.file, run), run.params)
    if phys.empty:
        sys.exit(f"Run {run_no} has no data rows")
    print_summary(run, phys, run.params.get("odr_hz", 200.0))

    imus = [IMU_NAMES.index(name) for name in args.imu]
    p = run.params
    title = (f"{args.file.name} - run {run.index} of {len(runs)}  "
             f"(ODR {p.get('odr_hz', '?'):g} Hz, gyro +-{p.get('gyr_range_dps', 125):g} deg/s, "
             f"accel +-{p.get('acc_range_g', 2):g} g"
             f"{f', averaged over {args.avg:g} s' if args.avg else ''})")
    charts = build_charts(run, phys, imus, args.avg, title)

    if args.save:
        save_pngs(charts, args.save, run.index)
    else:
        show_window(charts, f"{args.file.name} - run {run.index}")


if __name__ == "__main__":
    main()
