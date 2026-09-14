# Firmware State — ADC RMS Calibration Build

Snapshot of the firmware as of this commit: a serial-only build for the
two-line analog front-end described in
[ac_current_monitor_handoff_v2.md](ac_current_monitor_handoff_v2.md). It
converts both lines' ADC RMS into a calibrated amps value on-device
(`src/calibration.cpp`) and derives an estimated voltage and power from that
(`src/power.cpp`), printing all three over serial. MQTT publishing exists
(`publishPower()` in `src/mqtt_handler.cpp`) but is not wired up yet — that's
being set up separately later.

## What this build does

- Samples both analog front-end lines (unamplified + amplified) via two ADC1
  pins on the ESP32.
- Computes RMS of each line in software.
- Converts each line's RMS into amps and blends the two per
  `calculateAmps()` (see "Calibration / amps conversion" below).
- Derives voltage and power from the smoothed 10-second amps via
  `calculateVoltage()` / `calculatePower()` (see "Voltage sag / power
  estimate" below).
- Prints, once per second, over Serial (115200 baud):
  - The calculated amps from the 1-second RMS.
  - The calculated amps, estimated voltage, and power from a rolling
    10-second RMS.
  - The achieved sample rate.
- Blinks the onboard LED (GPIO2) every ~100ms as a heartbeat, independent of
  the measurement/print cycle — a quick way to tell a firmware hang apart
  from a serial/USB dropout (LED keeps blinking = firmware's fine, link
  dropped; LED stops too = actual firmware hang).

## Hardware / pin mapping

- `CURRENT_SENSOR_PIN_UNAMPLIFIED` = GPIO34 — unamplified analog line
- `CURRENT_SENSOR_PIN_AMPLIFIED` = GPIO35 — ~10x amplified analog line
- Both are ADC1 pins (input-only), chosen deliberately since ADC2 pins
  conflict with WiFi on the ESP32.
- Both lines are centered on a ~2.5V virtual ground (Vref) per the analog
  handoff doc, swinging up toward the 0-5V op-amp supply rail depending on
  current.
- **A resistor divider is required on each line** before the ADC pin — the
  analog circuit's 5V-rail swing exceeds the ESP32 ADC's ~3.6V absolute max
  input. Current divider: two 1k resistors in parallel (500 ohm) on top, one
  1k on bottom, giving a ratio of 1000/(1000+500) = 0.667, so a 5V worst-case
  swing becomes ~3.33V at the ADC pin.
- WiFi and MQTT connection code is still present in `main.cpp` (`setup()`
  and `loop()`) but commented out — not deleted — since it drops back in
  unmodified once MQTT publishing is wired up.

## How the RMS calculation works (`rms_current.cpp`)

**Per-line RMS of one sample buffer** (`calculateRMS`):

1. Take `CURRENT_SAMPLES` (1500) raw ADC readings (0-4095 counts) into a
   buffer.
2. Compute the buffer's own mean. This matters because the signal is
   centered on ~2.5V (mid-scale ADC counts), not 0V — RMS has to be computed
   on the *deviation from that DC bias*, not on the raw counts, or the huge
   DC offset would dominate the result.
3. Sum the squared deviations from the mean, divide by sample count, take
   the square root: `rms = sqrt(mean((sample - mean)^2))`. This is a true
   RMS calculation, valid for any waveform shape — not an average-responding
   approximation that only works for pure sinusoids (see the "RMS
   clarification" section of the handoff doc for why that distinction
   mattered when the analog rectifier stage was dropped).

**Per-measurement averaging** (`measureCurrent`):

- Both lines are sampled together in the same loop (`analogRead` on the
  unamplified pin, then the amplified pin, repeated `CURRENT_SAMPLES`
  times), so both buffers cover the same span of time and the same AC
  cycles.
- This is repeated `CURRENT_READINGS_PER_MEASUREMENT` (3) times per call,
  and the resulting RMS values are averaged across those 3 independent
  buffers to smooth out sample-to-sample noise.
- Achieved sample rate is measured directly (`micros()` before/after the
  sampling loop) rather than assumed, since analogRead timing isn't fixed.
- Raw ADC-count RMS is converted to volts using the ESP32's 12-bit
  full-scale (4095 counts = 3.3V), matching `analogReadResolution(12)` and
  `analogSetAttenuation(ADC_11db)` set in `initCurrentSensor()`.

**10-second rolling RMS** (`main.cpp` `loop()`):

- Each 1-second reading's RMS is squared and stored in a 10-slot ring
  buffer (one per line).
- The 10-second RMS is `sqrt(mean of the last 10 one-second mean-squares)`
  — averaging the *mean-squares* and taking the square root once at the end,
  not averaging the RMS values directly. This is the mathematically correct
  way to combine RMS values from equal-length, equal-sample-count windows;
  naively averaging RMS values themselves would be a (usually small, but
  needless) approximation error.

## Calibration / amps conversion (`src/calibration.cpp`)

`calculateAmps(unamp_adc_rms, amp_adc_rms)` turns the two raw ADC RMS
readings into one amps value:

1. **Noise floor.** If `unamp_adc_rms <= 7.0` (the no-load baseline measured
   during calibration was unamp=6.4, amp=10), return `0.0` immediately —
   no polynomial is evaluated.
2. **Unamplified estimate.** Otherwise, run `unamp_adc_rms` through a
   degree-2 polynomial fit across the full measured current range.
3. **Above ~4A:** the unamplified estimate is returned as-is. The amplified
   line is never evaluated up here — it saturates around 6A and its
   calibration fit only used measurements up to 4A, so it has no basis
   beyond that.
4. **Below ~4A:** `amp_adc_rms` is also run through its own degree-2
   polynomial fit (fit only on the <=4A data), and the two estimates are
   blended with a piecewise-linear weight on the amplified channel: 1.0 at
   0A, 0.75 at 1A, 0.50 at 2A, 0.0 at 4A. The weight is keyed off the
   *unamplified* estimate from step 2, since that's the only current-like
   signal available at runtime — the firmware has no independent "true
   current" to drive the blend with.

The coefficients and this weighting scheme came from fitting
`callibration/measurments.csv` — see
[callibration/CALIBRATION_METHODOLOGY.md](callibration/CALIBRATION_METHODOLOGY.md)
for how they were derived, and `callibration/output/calibration_results.txt`
for the exact fit run they were taken from. Notably, the calibration data
has **no measurements between ~1A and ~6.7A**, so the 1A/2A/4A blend
anchors themselves are extrapolated, not validated against real
measurements in that range — worth re-checking if more calibration data
gets collected there.

## Voltage sag / power estimate (`src/power.cpp`)

`calculateVoltage(amps)` and `calculatePower(amps)` implement a simple
linear voltage-sag model — i.e. assuming a constant wire/source resistance,
so the line voltage under load drops proportionally with current:

```
voltage = NO_LOAD_VOLTAGE - VOLTAGE_SAG_PER_AMP * amps
power   = voltage * amps
```

`NO_LOAD_VOLTAGE` and `VOLTAGE_SAG_PER_AMP` (in `src/config.h`) are
**placeholder values right now** (240V, 1V/amp) — not yet measured. The
intended tuning process: measure line voltage with no load, then measure
line voltage and current together under a known load, and solve for the
slope from those two points. See
`callibration/voltage_sag_measurments_240.ods` for that data as it's
collected.

`main.cpp` computes voltage/power from the smoothed 10-second amps
(`amps_10s`), not the noisier 1-second reading, and prints them over
serial. `publishPower()` (`src/mqtt_handler.cpp`, targeting
`MQTT_TOPIC_POWER`) exists and takes the computed power directly, but isn't
called yet — MQTT is being wired up separately later.

## Known issue investigated, not reproduced

A freeze was observed once at higher current with a resistive heater load,
with serial output stopping mid-print and no reboot banner — inconsistent
with a watchdog/brownout reset. Since the ESP32 and the heater were on the
same outlet/circuit as the computer running the serial monitor, the leading
theory is a USB/serial link dropout from electrical noise on the shared
circuit, not a firmware hang (the RMS math itself doesn't scale in
complexity with signal amplitude — same fixed sample count either way). The
heartbeat LED above was added to help distinguish the two if it recurs. Not
reproduced on the most recent test.

## Next steps

See [Project_plan.md](Project_plan.md) for the full roadmap. Immediately
next: wire MQTT publishing back up (uncomment the WiFi/MQTT code in
`main.cpp`, call `publishPower(power_10s)`), then measure and tune the real
`NO_LOAD_VOLTAGE`/`VOLTAGE_SAG_PER_AMP` values (currently placeholders).
