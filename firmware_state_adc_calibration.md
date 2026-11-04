# Firmware State — ADC RMS Calibration Build

Snapshot of the firmware as of this commit, for the two-line analog
front-end described in
[ac_current_monitor_handoff_v2.md](ac_current_monitor_handoff_v2.md). It
converts both lines' ADC RMS into a calibrated amps value on-device
(`src/calibration.cpp`), derives an estimated voltage and power from that
(`src/power.cpp`), prints all of it over serial every second for live
debugging, and separately publishes power over MQTT every 5 seconds.

> **Calibration status (2026-11-03):** `src/calibration.cpp` now uses a
> model fit to the September and November measurements that keeps both
> channels (an amplified-channel curve below ~1.5A, the unamplified curve above
> ~2.1A, fading between). Its leave-one-out error over all points is mean
> 4.5% / worst case 9.9%; the earlier September equations read **16–26% low
> between about 2.8A and 6A**. It has been checked against the Python fit
> and builds, but has **not yet been run on the device**. WiFi noise still
> adds roughly +0.4A at low current. Details:
> [callibration/CALIBRATION_METHODOLOGY.md](callibration/CALIBRATION_METHODOLOGY.md)
> ("November 2026 re-fit").

## What this build does

- Connects to WiFi and the MQTT broker (`src/mqtt_handler.cpp/h`,
  credentials in `src/config.h`). The WiFi connect step (`setup()` in
  `main.cpp`) explicitly forces station mode and drops stale state
  (`WiFi.mode(WIFI_STA)`, `WiFi.disconnect(true)`) before calling
  `WiFi.begin()` — the ESP32 can otherwise carry over AP/STA state from a
  previous run in a way that blocks a clean connect. If it hasn't connected
  after 15 seconds, it prints the `WiFi.status()` code (with a plain-English
  reason for the common ones: SSID not found, likely wrong password, etc.)
  and retries, rather than spinning dots forever with no diagnostic.
- Samples both analog front-end lines (unamplified + amplified) via two ADC1
  pins on the ESP32.
- Computes RMS of each line in software.
- Converts each line's RMS into amps and blends the two per
  `calculateAmps()` (see "Calibration / amps conversion" below).
- Derives voltage and power from the smoothed 5-second amps via
  `calculateVoltage()` / `calculatePower()` (see "Voltage sag / power
  estimate" below).
- Prints, once per second, over Serial (115200 baud) — this cadence is
  independent of the MQTT publish interval below, kept fast for live
  debugging with a computer plugged in:
  - The raw unamplified and amplified ADC RMS (counts) and the calculated
    amps, from the 1-second window.
  - The raw unamplified and amplified ADC RMS (counts), calculated amps,
    estimated voltage, and power, from a rolling 5-second window.
  - The achieved sample rate.
- Publishes power (from the same 5-second window) to `MQTT_TOPIC_POWER`
  every 5 seconds — a separate timer from the 1-second serial print, so
  MQTT isn't spammed at 1Hz.

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

## Data path in detail: how the firmware gets, stores, samples, computes, and reports

**1. Input and ADC setup** (`initCurrentSensor()` in `rms_current.cpp`)
- Two ADC1 pins (GPIO34 unamplified, GPIO35 amplified) set as inputs, with
  `analogSetAttenuation(ADC_11db)` (about 0–3.3V input range) and
  `analogReadResolution(12)` (counts 0–4095).
- Raw ADC counts are used directly: there is no per-chip ADC calibration
  (`esp_adc_cal`) or linearization. The ESP32 ADC is known to be non-linear
  near both ends of its range; the signals here are centered near mid-scale
  (~1.65V), where it behaves best, and the polynomial calibration absorbs
  whatever non-linearity remains in the range that was measured.
- Two buffers of `CURRENT_SAMPLES` (1500) `int`s are allocated once on the
  heap at startup (about 12 kB total) and reused for every reading. Raw
  samples are overwritten by the next reading; only the computed RMS values
  are kept.

**2. Sampling** (`measureCurrent()`)
- One *reading*: a loop of 1500 iterations, each doing
  `analogRead(unamp pin)` then `analogRead(amp pin)` and storing both
  results. The calls block until each conversion finishes; there are no
  delays, timers, interrupts, or DMA. The two samples in a pair are taken
  ~120 µs apart (about 2.6° of a 60 Hz cycle), which doesn't matter because
  each line's RMS is computed separately.
- One *measurement* = 3 readings. Each line's result is the plain average of
  its 3 buffer RMS values (`CURRENT_READINGS_PER_MEASUREMENT`).
- The time of each reading is measured with `micros()`, and
  `sample_rate_hz = 1500 / duration` is averaged over the 3 readings and
  printed. This is the sampling rate **per line** (each line gets 1500
  samples per reading; the ADC does twice as many conversions per second in
  total).

**3. RMS of each buffer** (`calculateRMS()`): mean of the buffer (integer sum
divided by count), then `sqrt(mean of squared deviations from that mean)`,
in `float`. The result is the **AC RMS in ADC counts** — the DC bias is
removed by subtracting the buffer's own mean.

**4. Timing of the main loop** (`main.cpp`)
- `loop()` first services MQTT, then starts a new measurement whenever at
  least 1000 ms have passed since the last one *started*. A measurement
  itself takes about 1.1 s (3 readings × 1500 iterations × ~240 µs ≈ 1.08 s,
  plus the RMS math), so the next one starts as soon as the previous one
  ends. Sampling is effectively continuous, and the "1s" label in the serial
  output is nominal: each "1s" value is ~1.1 s of data, produced every
  ~1.1 s. The "5s" value is the last 5 measurements, about 5.5 s.
- The MQTT client's `loop()` (keepalive, incoming messages) only runs
  between measurements, roughly once per 1.1 s. That is fine for the 60 s
  keepalive that is configured.

**5. Rolling window** (see the 5-second window section below): a 5-slot ring
of mean-squares per line; until 5 measurements exist, the average uses
however many there are.

**6. Conversion**: `calculateAmps()` is called once on the latest
measurement and once on the 5-measurement window; voltage and power are
derived from the 5-measurement amps only.

**7. Reporting**
- **Serial (115200 baud)**, after every measurement: a `1s` line (raw unamp
  RMS, raw amp RMS, amps), a `5s` line (the same plus voltage and power), and
  the sample rate.
- **MQTT**: `publishPower(power_5s)` publishes the 5-measurement power to
  `hvac/outdoor/power` (`MQTT_TOPIC_POWER`) as plain text watts with one
  decimal (`"%.1f"`). The 5000 ms timer is only checked once per
  measurement, so in practice a publish happens about every 5.5 s. It uses
  PubSubClient's defaults (QoS 0, not retained) and is silently skipped if
  the client isn't connected. Only power is published — not amps, voltage, or
  the raw RMS values. The client also subscribes to `hvac/led` (`ON`/`OFF`
  drives the LED on GPIO2) and `hvac/command` (no handler exists for it).
  Reconnect attempts happen every 5 s (`MQTT_RECONNECT_INTERVAL`).
- **Startup**: `setup()` blocks until WiFi connects (retrying every 15 s), so
  the device does not start measuring at all while WiFi is unavailable.

### Why the sample rate is the same every time (about 4166 Hz)

It is not a bug, and it is not a configured rate — nothing in the firmware
sets one.

- `analogRead()` here (arduino-esp32 2.0.17) is a blocking call that
  triggers one ADC1 conversion and waits for the result. Its duration is set
  by the ADC hardware plus the driver's fixed overhead — about 120 µs per
  call on this board. Two calls per iteration is ~240 µs, and 1 / 240 µs ≈
  4167 Hz. Because the loop contains nothing else that varies (no delays, no
  waiting on other tasks), every iteration takes the same time, so the
  measured rate comes out the same every time.
- It is deterministic, not *controlled*: no timer or DMA keeps the samples
  evenly spaced, so small jitter is possible but isn't visible at this
  resolution.
- Likely reason WiFi activity doesn't change it: Arduino's `loop()` runs on
  core 1 and the WiFi stack mostly runs on core 0. (Plausible, not verified
  by tracing.)
- **Unexplained difference:** on 2026-09-14 the same sampling code printed
  about **5917 Hz**. The toolchain has not changed since December 2025 and
  the config values are the same, so the repo doesn't explain it. If a
  different ESP32 board is being used now, that is one candidate; this was
  not investigated.

**What the rate implies:**
- A reading is 1500 samples ÷ 4166 Hz ≈ **0.36 s**, about **21.6 cycles** of
  60 Hz — not a whole number of cycles. For a pure sine that leaves an RMS
  error of up to about 0.2% per reading (my estimate, not measured), reduced
  further by averaging 3 readings and the rolling window.
- About **69 samples per 60 Hz cycle**, so frequencies up to ~2.08 kHz
  (Nyquist) are represented; anything faster aliases into the readings.
  This matters for noise: very short spikes are only caught if a sample
  happens to land on them.

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

**5-second rolling RMS** (`main.cpp` `loop()`, `AVERAGING_WINDOW_SECONDS`):

- Each 1-second reading's RMS is squared and stored in a 5-slot ring
  buffer (one per line).
- The 5-second RMS is `sqrt(mean of the last 5 one-second mean-squares)`
  — averaging the *mean-squares* and taking the square root once at the end,
  not averaging the RMS values directly. This is the mathematically correct
  way to combine RMS values from equal-length, equal-sample-count windows;
  naively averaging RMS values themselves would be a (usually small, but
  needless) approximation error.
- The window was 10 seconds originally; shortened to 5 seconds so the
  smoothed reading used for MQTT publishing (below) tracks real changes
  faster while still filtering out 1-second sample-to-sample noise.

**Design intent: RMS first, calibrate once.** The calibration equations
(next section) are only ever applied to an already-RMS'd ADC value — the
1-second RMS or the 5-second RMS — never to individual raw samples or to a
per-sample current that then gets averaged/RMS'd separately. Converting
each sample to a current first and RMS-ing *those* would be a different
(and wrong) calculation, since the calibration polynomials are nonlinear
and were fit against RMS ADC values in the first place, not instantaneous
ones. Worth keeping in mind if this code gets restructured later.

## Calibration / amps conversion (`src/calibration.cpp`)

`calculateAmps(unamp_adc_rms, amp_adc_rms)` turns the two raw ADC RMS
readings into one amps value:

1. **Noise floor.** If `amp_adc_rms <= 200.0`, return `0.0` immediately —
   no curve is evaluated. The amplified line is used for this check (not
   unamplified) because it's the more sensitive of the two at small
   currents. This threshold corresponds to ~0.28A / ~70W and is set much
   higher than the bench no-load baseline (unamp=6.4, amp=10) because of
   real on-device noise — see "Noise floor / WiFi interference" below.
2. **Pick the weight from the amplified channel's own RMS.** At or below
   1000 counts (about 1.5A) the weight on the amplified curve is 1; between
   1000 and 1400 counts (about 1.5–2.1A) it falls linearly to 0; at or above
   1400 counts it is 0. Using the amplified RMS directly (rather than an
   estimate of the current) ties the switch to what actually limits that
   channel: it clips.
3. **Amplified curve** (`amps = 0.00148822 * amp_rms - 0.0138492`), a
   straight line fit on the points up to 1.5A where the channel is linear.
4. **Unamplified curve**
   (`4.71728e-09*u^3 - 1.03711e-05*u^2 + 0.0193302*u - 0.0887038`), a cubic
   fit over all points and weighted to minimize percent error, so it also
   covers the fade zone and everything above it.
5. The result is `weight * amplified + (1 - weight) * unamplified`, floored
   at 0. Each curve is only evaluated when its weight is non-zero.

The curves and crossover came from `callibration/refit_analysis.py`
(section H) fit on `callibration/measurments.csv` (September) plus
`callibration/measurments_2026-11.csv` — see
[callibration/CALIBRATION_METHODOLOGY.md](callibration/CALIBRATION_METHODOLOGY.md)
("November 2026 re-fit") and `callibration/output/refit_results.txt`. The
C++ was checked against the Python model (identical estimates at the test
points, e.g. 1.4729A at unamp 87.7 / amp 999).

**History — the September calibration this replaced** (from
`callibration/measurments.csv`, which had **no measurements between ~1A and
~6.7A**): degree-2 curves for both channels, with a blend keyed off an
unamplified-derived amps estimate (amplified weight 1.0 at 0A, 0.75 at 1A,
0.5 at 2A, 0 at 4A). The November measurements showed it:
- read **16–26% low for loads between 2.8A and 6A** (about 1–4% off at 6.5A
  and above, within 7% at 0.65–1.1A) — it was wrong in the range it had no
  data for;
- assumed the amplified channel was usable to 4A, but it is verified linear
  only up to 1.48A and is compressed by 2.77A (nothing was measured in
  between).

**What is still not right with the new model:**
- The points between 2.8A and 6A do not lie on the same curve as the points
  at 6.7A and above (they need ~25% more amps per ADC count) and no polynomial
  fits both groups well; the cause is undetermined. In the new model, points
  at 5–6A read about 8% low and points at 6.5–7.4A about 7–8% high.
- Leave-one-out error over 42 points is mean 4.5% / worst case 9.9% (the
  September equations on the same points: mean 6.7% / worst case 25.7%), but
  it is less accurate above 6.5A than the September fit (mean about 1% →
  3–4%).
- The fade zone (1000–1400 counts) is unvalidated: no point was measured
  between 1.48A and 2.77A.

### Noise floor / WiFi interference

With the device running on WiFi in its actual installed location (not on
the bench), no-load readings were consistently 10-50+ watts instead of 0 —
e.g. one sample: `Unamp RMS: 15.5 | Amp RMS: 133.8 | Amps: 0.188A` (1s),
`Unamp RMS: 17.9 | Amp RMS: 159.6 | Amps: 0.229A | Power: 56.2W` (5s), with
nothing plugged in. The original noise-floor threshold (`amp_adc_rms <=
20`) was tuned against a quiet bench no-load baseline (amp=10) and didn't
account for this.

A few things ruled out simpler explanations:

- **Not something averaging fixes.** The 5-second smoothed reading was
  *higher* than the 1-second reading in both examples above, not lower —
  this isn't zero-mean noise that a longer window would average toward
  zero, it's a persistent bias. The window size (see "5-second rolling
  RMS" above) doesn't help here.
- **Both channels are elevated together (hypothesis, not confirmed).** The
  amp/unamp readings in the examples above are in a ratio of roughly 8.5-8.9
  (133.8/15.5 ≈ 8.6, 159.6/17.9 ≈ 8.9). That is in the neighborhood of the
  amplifier gain but is not equal to it: in the calibration data the
  amp/unamp RMS ratio for real, unsaturated loads is ~11.1 (nominal gain is
  10.1), so the noise is *not* scaling exactly like the signal. One reading
  is that part of the noise enters on the analog signal before the gain
  stage (RF pickup or supply noise) and part is added after it or at the
  ADC; another is that the ratio simply isn't informative at these low
  levels. If it were purely an ADC-sampling or firmware-timing artifact it
  would not be expected to elevate both channels together, which weakly
  favors an analog-side cause, but **this needs the scope measurements in
  `Test_plan.md` to settle** — don't treat it as established.

**Fix applied:** raised `NOISE_FLOOR_AMP_ADC` from 20 to 200 (~0.28A /
~70W at no-load voltage) — comfortably above the observed noise ceiling
(~160 counts / ~56W) — so anything below that reads as a clean 0 instead of
a fabricated small wattage. This means **the device currently cannot
reliably report any load under ~70W**; real loads in that range will read
as 0.

**Update 2026-11-03 — worse farther from the router.** After moving the
device to a location farther from the WiFi router, the no-load reading rose
to as much as ~200W and hovers around ~150W consistently. The 200-count
floor above (~70W) no longer covers that, and has **not** been changed
yet — more data is being collected first. One thing to test is whether
placing a hotspot right next to the device lowers the floor again (see
`Test_plan.md`). Also recorded from memory, not re-measured: the noise seen
on the low-signal and high-signal ADC lines was a similar amount.

**Future improvement (not implemented):** increasing the amplified
channel's gain beyond the current ~10x would improve signal-to-noise for
currents under ~0.5A, since a stronger real signal would sit further above
this same noise floor. Worth revisiting if measuring small loads (under
70W) becomes a requirement.

**Root cause still unconfirmed -- two candidate mechanisms, not yet
distinguished:**

- **Shared power-rail noise.** The ESP32's WiFi TX draws current in sharp
  bursts (up to ~300-500mA). If that shares a supply/regulator (even
  indirectly, via a common 5V rail or ground plane) with the analog
  front-end, those bursts can show up as ripple on the analog Vref/supply.
- **RF field coupling.** The antenna and its transmit bursts could be
  coupling directly into nearby analog traces/wires, independent of the
  power rail entirely.

Both would produce a similar signature (noise on both channels), so the
sample data alone doesn't distinguish them.

**Result of the power-supply test:** the board was run from different power
supplies and the WiFi noise did not change. That counts against noise
arriving through the external supply. It doesn't fully rule out the first
mechanism above, because the radio's current bursts still happen on the
board itself regardless of which supply feeds it.

### Scope captures (2026-11-03)

Summary of what was observed (captures are in `scope captures/`; they have
only been summarized, not analyzed in depth):

- **WiFi off:** the 5V rail is quiet, and a ~0.6A load gives a clean 60 Hz
  waveform on both lines (amplified 337 mV rms / 1.42 V peak-to-peak;
  unamplified 30 mV rms / 132 mV peak-to-peak).
- **WiFi on:** the 5V rail has sharp dips to about 4.8-4.85V roughly 5 times
  per second (a 10 uF capacitor on the line did not help), and both signal
  lines show large spikes at the same rate. No load: amplified 437 mV rms /
  3.1 V peak-to-peak (the full swing), unamplified 46 mV rms / 423 mV
  peak-to-peak. At ~0.6A: amplified 0.68 V rms / 3.1 V peak-to-peak,
  unamplified 52 mV rms / 445 mV peak-to-peak. The noise is as large as or
  larger than the 0.6A signal.
- The probe was on the ADC-pin side of the lines (average level ~1.66V).

**Why ~5 Hz? (a guess, not confirmed):** WiFi power save. A sleeping ESP32
wakes up for the access point's DTIM beacons. Beacons normally go out every
102.4 ms, so a DTIM interval of 2 gives a wake-up every 204.8 ms, about
4.9 Hz — close to the ~0.2 s spacing between the dips on the 5V rail. Each
wake-up powers up the radio, which draws a burst of current. The MQTT publish
(every ~5.5 s) is far too slow to be the source. To check: the router's DTIM
setting, and `WiFi.setSleep(false)`.

Dips on the 5V rail and spikes on the signal lines at the same moments fit a
supply-droop explanation (Vref is made from a divider off the 5V rail), but
that is unconfirmed.

**Later, if needed:** analyze the scope captures in more depth (timing and
width of each event, probing Vref and the 3.3V rail). Note the WiFi-on
captures were 8000 samples at 1.6 kHz over about 5 s, so very short spikes
may be under-represented.

### Potential solutions (none implemented or tested)

*Power/analog hardware:*
1. **Much larger bulk capacitance on the 5V line** near the ESP32 (for
   example 100–470µF low-ESR, plus 0.1µF ceramics). 10µF was not enough.
2. **A separate regulated supply for the analog section** (an LDO behind a
   ferrite bead or RC filter) so the ESP32's current bursts can't move the
   analog rail; star-ground the analog return.
3. **Make Vref immune to rail dips:** more capacitance and filtering on the
   Vref node, or derive Vref from a regulated reference instead of a plain
   divider off the unregulated 5V.
4. **Local decoupling at each TL272** (0.1µF plus ~10µF at the supply pins).
5. **Physical separation/shielding** of the ESP32 antenna from the analog
   wiring, with short or shielded analog input leads.
6. **Split the WiFi radio onto a second ESP32** on its own supply, sending
   the computed power over UART. This addresses supply coupling and
   antenna proximity at once, but is a bigger change (two boards, a UART
   link, splitting the firmware into a producer and consumer).

*Firmware:*
7. **Robust RMS:** compute RMS over short sub-windows (for example one 60 Hz
   cycle each) and take the median rather than the mean over the whole
   buffer, so a burst that only corrupts a few sub-windows is rejected. This
   only works if the bursts are short compared with the gaps between them;
   their duration hasn't been measured.
8. **Keep the radio quiet while sampling:** sample with WiFi off and switch it
   on briefly to publish. This costs reconnect time on every cycle and is
   awkward with a 5 s publish interval.
9. **WiFi settings:** lower `WiFi.setTxPower()`, `WiFi.setSleep(false)`, and
   the router's DTIM interval can change the burst pattern; test each, since
   none is guaranteed to help (disabling sleep could make it worse).

**Raised noise floor as a stopgap:** the current mitigation is only the
threshold in `calculateAmps()`; it hides the noise below ~70W (and it is
already too low for the ~150–200W seen at the farther location).

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

November measurements strengthen that question: with a measured gain of
~11.1x, the amplified line clips well before the loads it was meant to hand
off at. It is verified linear up to about 1.5A and is already compressed at
2.8A, so it is only useful for roughly the bottom 1.5–2A of the range — which
is also where the WiFi noise floor (see below) dominates. The current
calibration keeps it for exactly that range (see above), with the
unamplified channel taking over above it.

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

`main.cpp` computes voltage/power from the smoothed 5-second amps
(`amps_5s`), not the noisier 1-second reading. It's printed over serial
every second (alongside the faster 1-second reading, for live debugging),
and separately published to `MQTT_TOPIC_POWER` via `publishPower()`
(`src/mqtt_handler.cpp`) every 5 seconds, on its own timer independent of
the serial print cadence.

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

## WiFi connect issue investigated, resolved

WiFi wasn't connecting on first attempts even with SSID/password confirmed
correct. Root cause not conclusively identified — the original connect
loop (`WiFi.begin()` followed by a bare `while (WiFi.status() !=
WL_CONNECTED)`) gave no diagnostic at all, so there was no way to tell
"still connecting," "wrong password," and "SSID not found" apart from the
serial output. That loop now prints the `WiFi.status()` code (decoded for
the common failure cases) after 15 seconds and retries with a fresh
`WiFi.mode(WIFI_STA)` / `WiFi.disconnect(true)` / `WiFi.begin()`, in case
stale STA/AP state from a previous run was blocking a clean connect. After
this change (and re-flashing), the device connected successfully — whether
the fix or something environmental (router, signal) resolved it isn't
fully confirmed, but the improved diagnostics are worth keeping either way
in case a similar issue recurs.

## Improvements and next steps

See [Project_plan.md](Project_plan.md) for the roadmap. The ideas below are
collected here with the firmware context; none are implemented.

**Calibration (highest priority, see `callibration/CALIBRATION_METHODOLOGY.md`)**
- Run the new calibration on the device and compare its readings with the
  reference meters.
- Record the load type for every calibration point, and measure resistive
  loads between 1.5A and 6.5A to find out whether the mid-range discrepancy
  depends on the load. Also measure a load near 2A to validate the amplified
  channel's fade zone.
- Use a held-out set of measurements (not used in fitting) to state accuracy.

**Sampling and RMS**
- Sample on a timer or with DMA (for example the ESP32's continuous ADC
  mode) so the sample rate is fixed and chosen, not whatever `analogRead`
  happens to take.
- Choose the window to be a whole number of 60 Hz cycles (or synchronize to
  a zero crossing) to remove the partial-cycle error.
- Use the ESP32's ADC calibration to correct its non-linearity, or an
  external ADC.
- Robust RMS (median over sub-windows) to reject WiFi bursts — see the
  noise section.

**Power measurement**
- Measure line voltage and the voltage–current phase to report real power
  (the current estimate is closer to apparent power: the firmware multiplies
  amps by a modeled voltage and ignores the power factor).
- Collect more voltage-sag points to replace the single-point
  `VOLTAGE_SAG_PER_AMP` fit.

**Noise and hardware**
- See "Potential solutions" in the noise section (bulk capacitance, a
  separate analog supply, a more robust Vref, a second ESP32 for WiFi).
- Decide whether to keep the amplified channel, or give it more gain and
  restrict it to the lowest ~1.5A.

**Firmware robustness**
- Don't block `setup()` forever waiting for WiFi, so measurement can start
  without a network.
- MQTT: publish amps and voltage as well as power, consider retained messages
  and a last-will message so Home Assistant can tell when the device is
  offline.
- Keep credentials and tuned constants in a version-controlled template
  (`config.h` is gitignored, so the tuned voltage-sag values are not in
  version control).
