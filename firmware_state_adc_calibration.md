# Firmware State — ADC RMS Calibration Build

Snapshot of the firmware as of this commit: a serial-only build used to
collect ADC-rms-vs-actual-current data points for the new two-line analog
front-end described in [ac_current_monitor_handoff_v2.md](ac_current_monitor_handoff_v2.md).
This is not the production firmware — no MQTT publishing, no current/power
conversion yet, since the calibration equation doesn't exist yet.

## What this build does

- Samples both analog front-end lines (unamplified + amplified) via two ADC1
  pins on the ESP32.
- Computes RMS of each line in software.
- Prints, once per second, over Serial (115200 baud):
  - The 1-second RMS for both lines (raw ADC counts and volts).
  - A rolling 10-second RMS for both lines.
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
  unmodified once a calibration equation exists to publish real current data.

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
next: run this build against known loads, record amps vs. printed ADC RMS
for both lines, and derive the calibration equation(s) (plus a
crossover point between the two lines) before bringing MQTT publishing back.
