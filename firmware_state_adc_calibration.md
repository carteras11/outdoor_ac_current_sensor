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
  - The raw unamplified and amplified ADC RMS (counts) and the calculated
    amps, from the 1-second window.
  - The raw unamplified and amplified ADC RMS (counts), calculated amps,
    estimated voltage, and power, from a rolling 10-second window.
  - The achieved sample rate.

## Hardware / pin mapping

- `CURRENT_SENSOR_PIN_UNAMPLIFIED` = GPIO34 — unamplified analog line
- `CURRENT_SENSOR_PIN_AMPLIFIED` = GPIO35 — ~10x amplified analog line
- Both are ADC1 pins (input-only), chosen deliberately since ADC2 pins
  conflict with WiFi on the ESP32.
- The analog front-end itself is centered on a 2.5V virtual ground (Vref)
  per the analog handoff doc, swinging up toward the 0-5V op-amp supply
  rail depending on current. **At the ESP32 ADC pins, after the resistor
  divider below, that becomes ~1.65V** — the ADC's own mid-scale (half of
  its 3.3V input range) — not 2.5V. This is intentional: the divider ratio
  was chosen so the DC bias lands on the ADC's own midpoint, maximizing
  usable swing in both directions rather than being biased toward one rail.
- **A resistor divider is required on each line** before the ADC pin — the
  analog circuit's 5V-rail swing exceeds the ESP32 ADC's ~3.6V absolute max
  input. Current divider: two 1k resistors in parallel (500 ohm) on top, one
  1k on bottom, giving a ratio of 1000/(1000+500) = 0.667, so a 5V worst-case
  swing becomes ~3.33V at the ADC pin, and the 2.5V DC bias becomes ~1.65V.
- WiFi and MQTT connection code is still present in `main.cpp` (`setup()`
  and `loop()`) but commented out — not deleted — since it drops back in
  unmodified once MQTT publishing is wired up.

## How the RMS calculation works (`rms_current.cpp`)

**Per-line RMS of one sample buffer** (`calculateRMS`):

1. Take `CURRENT_SAMPLES` (1500) raw ADC readings (0-4095 counts) into a
   buffer.
2. Compute the buffer's own mean. This matters because the signal is
   centered on ~1.65V (mid-scale ADC counts, ~2048 of 4095), not 0V — RMS
   has to be computed on the *deviation from that DC bias*, not on the raw
   counts, or the huge DC offset would dominate the result.
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

**Design intent: RMS first, calibrate once.** The calibration equations
(next section) are only ever applied to an already-RMS'd ADC value — the
1-second RMS or the 10-second RMS — never to individual raw samples or to a
per-sample current that then gets averaged/RMS'd separately. Converting
each sample to a current first and RMS-ing *those* would be a different
(and wrong) calculation, since the calibration polynomials are nonlinear
and were fit against RMS ADC values in the first place, not instantaneous
ones. Worth keeping in mind if this code gets restructured later.

## Calibration / amps conversion (`src/calibration.cpp`)

`calculateAmps(unamp_adc_rms, amp_adc_rms)` turns the two raw ADC RMS
readings into one amps value:

1. **Noise floor.** If `amp_adc_rms <= 20.0`, return `0.0` immediately — no
   polynomial is evaluated. The amplified line is used for this check (not
   unamplified) because it's the more sensitive of the two at small
   currents — it moves more per amp near zero, so it separates "no load"
   from "a little load" more clearly. No-load baseline measured during
   calibration was unamp=6.4, amp=10; 20 is set above that with margin for
   noise.
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

### Retrospective: is the amplified channel actually still needed?

Possibly not. The amplified line was added to get usable ADC resolution at
low current, back when the analog front-end's first version just did a
passive level-shift of the raw CT signal onto Vref with no buffering (no
high-impedance op-amp output to read from). The current analog front-end
(per `ac_current_monitor_handoff_v2.md`) is a real improvement — buffered,
higher-impedance output — and the unamplified line alone now appears to
already resolve small currents better than expected (see the calibration
methodology doc). The unamp/amp calibration curves do still show slight
measurable differences at low current, so the amplified line isn't
*useless*, but whether it's worth the extra hardware (second ADC line, gain
stage) and firmware complexity (a second calibration curve, blend logic)
is worth revisiting now that the unamplified signal alone is this good.

## Voltage sag / power estimate (`src/power.cpp`)

`calculateVoltage(amps)` and `calculatePower(amps)` implement a simple
linear voltage-sag model — i.e. assuming a constant wire/source resistance,
so the line voltage under load drops proportionally with current:

```
voltage = NO_LOAD_VOLTAGE - VOLTAGE_SAG_PER_AMP * amps
power   = voltage * amps
```

`NO_LOAD_VOLTAGE` and `VOLTAGE_SAG_PER_AMP` (in `src/config.h`) are tuned
from one real measurement (multimeter, so RMS voltage):

| condition | voltage | current |
|---|---|---|
| no load | 246.0V | 0A |
| AC loaded (blower fan) | 243.7V | 7.5A (independently measured) |

Slope: `(246.0 - 243.7) / 7.5 = 0.3067` V/A. This is a **single-point fit**
— only one loaded data point exists, so the line is anchored correctly at
both ends of that one measurement but its shape in between/beyond is
unverified. Worth adding more (amps, voltage) pairs across the current
range once convenient. See `callibration/voltage_sag_measurments_240.ods`
for that data as it's collected.

**Note on `callibration/measurments.csv`'s `avg_voltage` column:** that
data was also recorded while collecting the *current* calibration points
above, but on a separate, unrelated ~120V household circuit (various small
appliances — lights, a laptop, a vacuum, etc.), not on the actual 240V
outdoor AC circuit this firmware measures. Don't use those `avg_voltage`
values for tuning the voltage-sag model above — they aren't from the same
circuit, and 120V mains has entirely different source impedance than the
240V AC circuit's. The two real (amps, voltage) points in the table above
are the only voltage-sag data collected on the actual circuit so far.

`main.cpp` computes voltage/power from the smoothed 10-second amps
(`amps_10s`), not the noisier 1-second reading, and prints them over
serial. `publishPower()` (`src/mqtt_handler.cpp`, targeting
`MQTT_TOPIC_POWER`) exists and takes the computed power directly, but isn't
called yet — MQTT is being wired up separately later.

### Sanity check: blower fan reading (7.01A) vs. the 7.5A voltage-sag point

During this same test, the firmware's calculated current for the AC blower
fan came out to 7.01A over serial — close enough to the independently
measured 7.5A load current above to be worth checking for a code artifact
that would pin readings near 7A regardless of the real signal. Checked and
ruled out:

- The only literal `7` in the current-calibration code is
  `NOISE_FLOOR_AMP_ADC = 20.0` in `src/calibration.cpp` — that's a raw ADC
  **count** threshold near zero load, not an amps value, and is on a
  completely different code path (it only ever forces a reading to `0.0`,
  never to `7`). (At the time this check was made it was still
  `NOISE_FLOOR_UNAMP_ADC = 7.0` — since moved to the amplified channel, see
  "Calibration / amps conversion" above — but the conclusion is unchanged:
  neither version can produce a `7` output.)
- The unamplified calibration polynomial (the one active above 4A, which
  covers this reading) is a downward-opening parabola whose vertex — the
  only place it could flatten out or plateau — sits at unamp_adc_rms ≈ 6347
  counts, far outside the ADC's 0-4095 range. Across the whole valid ADC
  range the fit is strictly increasing, so there's no flat spot for
  unrelated readings to collapse toward.
- The ADC RMS value implied by 7.01A (~488 counts) falls directly between
  two real calibration points — `space heater low` (472 counts, 6.67A) and
  `space heater low + others` (524 counts, 7.385A) — so it's an
  interpolation inside measured, supported data, not extrapolation into the
  unmeasured 1-6.7A gap noted elsewhere in this doc.

Conclusion: nothing in the code pins or biases readings toward 7A; the
proximity between the fan's 7.01A and the 7.5A sag-test current looks like
coincidence. The fan pulling more RMS current than its real power would
suggest is consistent with blower/induction motors having a lower power
factor — current is high relative to real power because a chunk of it is
reactive, not converted to watts.

## Known issue investigated, not reproduced

A freeze was observed once at higher current with a resistive heater load,
with serial output stopping mid-print and no reboot banner — inconsistent
with a watchdog/brownout reset. Since the ESP32 and the heater were on the
same outlet/circuit as the computer running the serial monitor, the leading
theory is a USB/serial link dropout from electrical noise on the shared
circuit, not a firmware hang (the RMS math itself doesn't scale in
complexity with signal amplitude — same fixed sample count either way). Not
reproduced on the most recent test.

A heartbeat LED blink was added at the time to help distinguish a firmware
hang from a serial/USB dropout, but has since been removed (LED_PIN is now
solely used for the MQTT-controlled indicator, see `mqtt_handler.cpp`). If
a similar freeze recurs, telling the two apart will need another method —
e.g. checking whether the MQTT-controlled LED still responds, or
reintroducing a dedicated heartbeat if it becomes necessary again.

## Next steps

See [Project_plan.md](Project_plan.md) for the full roadmap. Immediately
next: wire MQTT publishing back up (uncomment the WiFi/MQTT code in
`main.cpp`, call `publishPower(power_10s)`), then collect more voltage-sag
data points to firm up the current single-point fit.
