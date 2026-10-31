# Temperature–Bias Correlation Test (BMI323 IMUs)

Goal: measure how each IMU's gyro (and accel) bias changes with die temperature,
to build the in-flight temperature compensation curve `calib_bias(T)` for each
IMU and axis.

Run this **after** the initial Allan deviation tests. Those runs tell us the
board's self-heated equilibrium temperature at room conditions and how long
warm-up takes.

## Equipment

- Flight computer with the **AllanTest** firmware (see `Core/Src/AllanTest.cpp`;
  build/flash with the `AllanTest` CMake preset)
- SD card (empty or with enough free space: about 120 MB per hour of logging)
- Battery that lasts the whole test (about 1.7 h, see below), fully charged
  - Very important: if the battery dies, logging stops and the rest of the sweep
    is lost. Data logged before that is kept (the logger syncs to the card once
    per second, and every run is appended to `ALLAN.CSV`, never overwritten).
- Airtight zip bag big enough for FC + battery, and 2–4 silica gel desiccant
  packs
- Rigid mounting block or tray, so the FC keeps the **same orientation** the
  whole time
- Refrigerator (**NOT A FREEZER**: frost and condensation risk, and a very cold
  LiPo loses capacity and its voltage sags, which can brown out the FC mid-test)

## Procedure

### Setup

1. Mount the FC on the block/tray. Note the orientation (which board axis points
   up).
2. Power on from the battery. Wait for the LED to go **solid** (logging). Fast
   blink means it is still waiting for the SD card or IMUs.
3. Put the FC, battery and desiccant into the zip bag, press the air out and
   seal it. **The bag stays sealed until the very end of the test** (see
   Condensation below).
4. Write down the start time (in like human time). From here on, record the time
   of every move (into/out of the fridge). These times make it easy to find
   motion in the data later.

### Cycles

5. Put the bagged FC in the fridge for **20 minutes**. Rough handling during
   moves is OK (those parts are removed in processing), but keep the FC in the
   same orientation.
6. Take it out and let it warm up at room temperature for **20 minutes**, still
   sealed and undisturbed.
7. Repeat steps 5–6 **one more time** (2 cold/warm cycles in total).

### Finish

8. Leave the bag sealed for another **20 mins**, to prevent condensation forming
   on board
9. Open the bag, power off the FC, remove the SD card, and copy `ALLAN.CSV`.
10. Write down anything unusual: bumps, fridge door left open, LED
    slow-blinking.

## Data Processing

`ALLAN.CSV` can contain several runs. Each starts with
`=== ALLAN RUN START ===`, followed by `#` comment lines with the scale factors.
Process each IMU (`imu` = 0, 1, 2) and each axis separately.

Unit conversions (from the run header; ±125 dps, ±2 g settings):

- gyro dps = raw × 125 / 32768
- accel g = raw × 2 / 32768
- temp °C = temp_raw / 512 + 23 (only needed for labels; the fit can use
  `temp_raw` directly)

Steps:

1. **Check the data first.** Look for gaps in `count` (lines dropped because the
   SD card fell behind) and in `sens_time` (samples missed; at 200 Hz,
   consecutive samples are about 128 ticks apart).
2. **Remove the parts where the FC was moved.** Use the event times you noted
   plus spikes in the gyro/accel. Remove a margin of time around each move.
3. **Average into short time windows** (1–10 s). This shrinks millions of points
   to thousands without changing the result.
4. **Bin by temperature** (`temp_raw`) and average the IMU values in each bin.
   Half a degree = **256 `temp_raw` counts** (1 count = 1/512 °C). Binning gives
   each temperature equal weight, even though the sweep spends much more time
   near the end temperatures.
5. **Split warming and cooling segments.** Plot them in different colors. If
   they don't overlap, the bias has hysteresis and a single curve will be off by
   about half the gap.
6. **Plot the points**: bias vs. `temp_raw`, one plot per IMU per axis.
7. **Interpret the points, then fit a curve.** Start with a low-order polynomial
   (2nd or 3rd order). Avoid high orders: they follow noise and behave badly at
   the edges.
8. **Plot the residuals** (data minus curve) vs. temperature. They should be
   flat and random. A clear shape means the curve is too simple.
9. **Validate on separate data.** Fit on one cycle, test on the other: apply
   `bias = bias_start + calib_bias(T_now) − calib_bias(T_start)` and check the
   leftover bias. This is the best estimate of how well compensation will work
   in flight.

## Condensation

Preventing condensation from the cold board being taken out of the fridge:

- **Seal the FC in an airtight bag with desiccant before it goes in the**
  **fridge**, and keep it sealed while it warms up. Moisture then condenses on
  the _outside_ of the bag, not on the board. The desiccant absorbs the little
  moisture sealed in with it.
- **Only open the bag once the board is back at room temperature.** Opening a
  cold bag defeats the purpose.
- **Use the fridge, not the freezer.** A fridge stays above 0 °C, so there's no
  frost. Freezer temperatures mean frost and much more condensation risk.
- Press the air out of the bag before sealing: less air means less moisture
  inside.
- Don't open the fridge door more than needed. Every opening lets in warm, humid
  air.

## Expected Temperature Range

- **Fridge air:** typically about 3–5 °C. **Room:** about 20–25 °C.
- **The IMU die won't reach those temperatures.** The board heats itself
  (regulators, MCU), so the die sits some degrees above the air around it. The
  bag's trapped air also insulates the board and slows down cooling. In 20
  minutes the board may not fully settle. Expect the die to swing somewhere
  between "fridge + self-heating" and "room + self-heating", likely around
  **15–20 °C of total swing**. The real numbers are unknown until measured; the
  initial Allan runs show the self-heating at room temperature.
- Pick the cycle length from the warm-up curve in the initial Allan runs. After
  this test, check whether `temp_raw` levelled off within each 20-minute phase.
  If it didn't, lengthen the cycles next time (e.g. 30–40 min) for a wider
  range.
- **Does this range cover flight?** The curve is only trustworthy inside the
  swept range. Compare it with the expected launch-day temperatures and the
  avionics-bay heating. If flight could be colder or warmer, a wider sweep is
  needed later (e.g. a warm box or a gentle heat source for the high end), and
  flight code should hold the value at the nearest end of the range instead of
  extrapolating.

## Battery and Safety Notes

- **Duration:** 2 × 40 min of cycles + 20 min final warm-up ≈ **1.7 h** (about
  200 MB of data). Make sure the battery lasts that long with margin. **Cold
  reduces battery capacity.** If power drops, the last ~1 s of data may be lost
  (the logger syncs to the card once per second).
- LiPo batteries: discharging at fridge temperatures is fine. **Never charge a**
  **cold LiPo**, and don't put the battery in a freezer.
- Keep the FC and battery in the bag together, so nothing pulls on the cables
  during moves.

## Possible Follow-Ups

- **Repeat on another power-on / another day** for a true hold-out data set.
  This also checks that the curve's shape is the same between power-ons (only
  shifted up/down by the power-on offset, which the on-pad calibration removes).
- Wider temperature range, if flight conditions require it.
- Same analysis for the accelerometer bias (the data is already in the file).
