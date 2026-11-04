# Project Overview — ESP32 Outdoor AC Current / Power Monitor

*Written 2026-11-03 as a single, self-contained summary of the whole project
(for interview preparation). It states what is measured, what is estimated,
what works, and what doesn't, with the source of each number. Detail lives in
the linked documents. Anything marked **pending** or **unconfirmed** is not
settled.*

## 1. What it is

A device that monitors the outdoor unit of a home air conditioner. It
clamps a current transformer (CT) around one conductor of the unit's 240V
supply, conditions the CT signal in an analog circuit, samples it with an
ESP32, computes true RMS current in software, converts it to amps with a
calibration, estimates voltage and power, prints everything over serial, and
publishes power over MQTT (to a Home Assistant broker) every few seconds.

Why: to see how much power the AC draws and how that changes with conditions
(for example outdoor temperature), using a cheap DIY sensor instead of a
commercial energy monitor.

History: the first version, roughly 8 months before this was written, already
computed RMS in software (`sqrt(mean(...))`) but had minimal conditioning
circuitry — no op-amps, no buffering — so the readings were inaccurate and
very noisy. That motivated the analog redesign described below. The git
history starts 2026-08-30 at "initial state before new current monitor analog
circuitry".

## 2. System at a glance

```
CT clamp -> input divider -> AC-coupling -> integrator -> AC-coupling/recentering
         -> unity buffer --+-> 2/3 divider -> ESP32 ADC (GPIO34)  "unamplified"
                           +-> ~11x gain stage -> 2/3 divider -> ESP32 ADC (GPIO35)  "amplified"

ESP32: sample both lines -> RMS (counts) -> 5-measurement rolling RMS
       -> calibration to amps -> modeled voltage -> power
       -> serial log (every measurement) + MQTT publish of power (~every 5.5 s)
```

Hardware: ESP32 (nodemcu-32s board), two TI TL272 dual op-amps (4 channels),
CT clamp, resistors/capacitors, 5V supply. Firmware: PlatformIO + Arduino
framework (arduino-esp32 2.0.17). Details:
[ac_current_monitor_handoff_v2.md](ac_current_monitor_handoff_v2.md) (circuit)
and [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
(firmware).

## 3. Analog front end (why each stage exists)

All stages run from one 5V rail and are centered on a 2.5V "virtual ground"
(Vref), because there is no negative supply. Full values and reasoning are in
the circuit document; in order:

1. **Vref**: a 10k/10k divider off 5V, buffered by a TL272 follower.
2. **Input divider (330 Ω / 100 Ω):** scales the CT signal down so ~15–18A
   fits within the op-amp swing (it was hitting ~5V peak-to-peak at 12A
   before).
3. **AC coupling (21 µF, 1k to Vref):** blocks DC and re-centers the signal on
   Vref. The bias resistor is deliberately small: with 150k it produced a
   ~2.25V centering error from op-amp input offset current.
4. **Lossy integrator (10k, 270k ∥ 0.1 µF, TL272):** the CT output is
   proportional to the *rate of change* of current, so integrating recovers
   the current waveform i(t) itself. The 270k "lossy" resistor caps the DC
   gain at 27x so small offsets can't drift the output to a rail.
5. **Recentering (20 µF, 1k to Vref):** the integrator output sat at ~2.46V
   instead of 2.50V. A buffer alone can't fix a DC offset, so a second AC
   coupling network re-injects an exact Vref bias. 20 µF was a balance between
   attenuating the signal (too small) and letting the DC level wander (too
   large); it kept amplitude and waveform shape consistent, and a similar value
   had already worked for the input coupling.
6. **Unity buffer (TL272):** a high-impedance source for the next stages.
7. **Gain stage (~10.1x nominal, ~11.1x measured):** a second, amplified line
   for small currents.
8. **2/3 dividers to the ADC:** scale 0–5V down to ~0–3.3V and move the 2.5V
   bias to ~1.65V, the middle of the ESP32 ADC range.

Notable engineering history: the integrator was first built on an **LM741**,
which performed badly on a single 5V rail (rated for ±5V to ±15V supplies,
can't swing near its rails, larger input offset currents), giving a
compressed, lopsided output swing; it was replaced with a **TL272**
(JFET-input, specified for single-supply use). A second bug — an op-amp
reference pin wired to ground instead of Vref — was diagnosed from a measured
2.4V at the summing junction that matched a passive-divider calculation.

Verification done on the analog chain: waveform-generator tests at 60 Hz
(sine → sine, square → triangle, triangle → rounded dome, trapezoid →
rounded sine-like), with amplitude and shape consistent. 11.5A produced
~2.8V peak-to-peak on the unamplified line.

## 4. Firmware data path

(Exact details, including timing: firmware document, "Data path in detail".)

- **Sampling:** two blocking `analogRead()` calls per iteration (unamplified
  then amplified) × 1500 iterations = one *reading*; ~4166 samples per second
  per line, so a reading is ~0.36 s ≈ 21.6 cycles of 60 Hz. Three readings
  make one *measurement* (~1.1 s). The rate is not configured — it is
  whatever `analogRead` takes, which is why it is constant; nothing paces it
  (no timer or DMA).
- **RMS:** per buffer, `sqrt(mean((x − mean(x))²))` in ADC counts (the DC bias
  is removed by subtracting the buffer mean). The 3 readings are averaged;
  then a 5-measurement rolling window combines mean-squares (not RMS values)
  and takes one square root.
- **RMS first, calibrate once:** the calibration is only applied to an
  already-computed RMS, never to individual samples — the curves are nonlinear
  and were fit to RMS values.
- **Output:** serial lines (raw unamp RMS, raw amp RMS, amps; plus voltage and
  power for the 5-measurement window), and MQTT `hvac/outdoor/power` as plain
  text watts (QoS 0, not retained) about every 5.5 s.

## 5. Calibration

**Method.** Known loads were measured with reference meters (a Kill-A-Watt
and a second meter; their average is the reference) while logging the raw RMS
of both lines. Polynomial curves (amps as a function of ADC RMS) were fit in
Python (`callibration/fit_calibration.py`): degree 2 for the unamplified line
over the full range, degree 2 for the amplified line on the low end, blended
with a piecewise-linear "DJ mixer" weight (amplified weight 1.0 at 0A, 0.75 at
1A, 0.5 at 2A, 0 at 4A) keyed off the unamplified line's own estimate. Degree
2 was chosen over 1 (much better) and 3 (little further gain, and it
extrapolates badly).

**What the firmware used before November:** the September 2026 fit, from 22
points that had **no data between 0.96A and 6.67A**. The firmware now uses the
new crossover model described below (not yet run on the device).

**What the November 2026 measurements showed** (20 new WiFi-off points,
`callibration/measurments_2026-11.csv`; analysis in
`callibration/output/refit_results.txt`):
- The September equations read **16–26% low between 2.8A and 6A**; they are
  within about 4% at 6.5A and above.
- The original and new data agree with each other where they overlap (mean
  difference 2.2%, max 5.6% in amps per ADC count), including 6.69A vs 6.67A
  at 471 vs 472 counts. The circuit hasn't visibly drifted over the months.
- The amplified line is verified linear up to 1.48A and compressed by 2.77A
  (nothing was measured between), so the "blend out to 4A" design doesn't hold.
- The 2.8–6A points need ~25% more amps per count than the ≥6.7A points; one
  polynomial fits both only poorly. The cause is **undetermined** (load types
  weren't recorded; the mid-range points included a battery charger).

**Current calibration (in `src/calibration.cpp`; checked against the Python fit and built, not yet run on the device):** both channels are kept.
A straight-line amplified curve is used up to an amplified RMS of 1000 counts
(about 1.5A), fades linearly to the unamplified curve by 1400 counts (about
2.1A), and a weighted cubic unamplified curve is used above that. The crossover
is as high as the data supports; the fade zone is unvalidated because nothing
was measured between 1.48A and 2.77A. A cubic helps modestly over a quadratic
here (worst case 12.6% → 9.9%).

**Accuracy** (WiFi off, reference = average of two meters; "LOO" = leave-one-out,
each point predicted by a fit that excluded it):

| Range | September fit (the previous firmware) | New model, now in the firmware (LOO, mean / max) |
|---|---|---|
| 0.3–1.5 A | within ~8% | 3.0% / 7.1% |
| 1.5–6.5 A | **mean 21%, max 26% low** (16–26% low at 2.8–6A) | 7.9% / 9.9% |
| ≥ 6.5 A | mean 1.0%, max 3.9% | 3.7% / 8.9% |
| all ≥ 0.3 A | mean 6.7%, max 25.7% | 4.5% / 9.9% |

Below ~0.3 A readings are not reliable, and with WiFi on the noise floor is
~150–200 W. The new model trades accuracy above 6.5A (the cause of the
two-group problem is unknown, so points just above 6.5A read about 7–8% high
and points at 5–6A about 8% low) for much better accuracy in the mid-range.

The two reference meters themselves disagree by a median of ~1% (above
0.5A), which limits how tight any claim can be. A figure such as "5%
accuracy" is **not** supported across the range right now; it would hold only
for parts of it.

Other calibration facts: the noise floor for WiFi-off data is `amp RMS ≤ 20`
(no-load baseline: unamp 6.4, amp 10 on the bench); the firmware currently
uses 200 because of WiFi noise.

## 6. Voltage and power

Power is **estimated**, not measured: `power = (246.0 − 0.3067 × amps) ×
amps`. The no-load voltage (246.0V) and the 0.3067 V/A slope come from one
multimeter measurement on the actual 240V circuit (243.7V at 7.5A), assuming
a constant wire/source resistance. Limits: a single-point fit; no power factor
is measured, so the result is closer to apparent power (volt-amps) than real
watts; the voltages recorded during the current calibration came from a
separate 120V circuit and were deliberately not used.

A sanity check worth knowing: the blower fan read 7.01A, close to the
independently measured 7.5A of the voltage-sag test. The code was checked for
anything that could pin readings near 7A (nothing does); the number lies
between two real calibration points, and a low power factor explains why a
blower motor draws more current than its watts suggest.

## 7. WiFi noise investigation

With WiFi on, no-load readings were 10–50 W, rising to ~150–200 W when the
device was moved farther from the router. Findings so far:
- Averaging doesn't help (the 5-second value was higher than the 1-second
  value): it is a bias, not zero-mean noise.
- Different external power supplies made **no difference**.
- Scope captures (2026-11-03; summarized, not yet analyzed in depth): with
  WiFi on, the 5V line dips to ~4.8–4.85V in sharp spikes about 5 times per
  second (a 10 µF capacitor didn't help), with matching spikes on both signal
  lines. Amplified line: 437 mV rms / 3.1V peak-to-peak with no load (vs.
  337 mV rms / 1.42V p-p and a clean 60 Hz waveform with WiFi off at ~0.6A).
  Unamplified line: 46 mV rms with no load vs. ~30 mV rms of actual signal at
  0.6A. A possible reason for ~5 Hz (a guess): WiFi power-save wake-ups at
  the router's DTIM beacons (2 × 102.4 ms ≈ 4.9 Hz).
- Working hypothesis (**unconfirmed**): the radio's current bursts droop the
  5V rail, and since Vref comes from a divider off that rail, the bias for
  every stage moves. The ~5 Hz rate might be WiFi power-save wake-ups at the
  router's DTIM interval — a guess.
- Current mitigation: a noise-floor threshold (loads under ~70W read as 0),
  which is now too low for the farther location.
- Potential fixes (none tried): larger bulk capacitance, a separate analog
  regulator, a more robust Vref, local decoupling, shielding, a second ESP32
  for WiFi, a median-of-sub-windows RMS, radio-off sampling. See the firmware
  document.

## 8. Evidence and cross-checks available

- Firmware RMS vs. scope: the 0.65A load read 37.3 and 418 counts, i.e. ~30 mV
  and ~337 mV at the ADC pin; the scope (WiFi off) read ~30 mV and 337 mV.
- 11.5A → ~2.8V peak-to-peak on the unamplified line is consistent with the
  firmware's 825 counts RMS at 11.5A: 825 counts ≈ 0.665V rms at the ADC pin,
  ≈ 1.0V rms before the 2/3 divider, ≈ 2.8V p-p if sinusoidal (assuming the
  2.8V was measured before that divider).
- Old vs. new calibration data agree where they overlap (section 5).
- Home Assistant data (15 points read off graphs): the AC's power draw rose
  about 12 W per °F, from 1504W at 51°F to 1911W at 85°F (+27%), consistent
  with falling efficiency at higher outdoor temperature (efficiency itself
  wasn't measured). See [analysis/ac_power_vs_outdoor_temp.md](analysis/ac_power_vs_outdoor_temp.md).

## 9. Known limitations

- The previous (September) calibration read 16–26% low at 2.8–6A. The new
  model now in the firmware (not yet run on the device) leaves up to ~10%
  error in the mid-range, and the cause of the two-group mid-range
  discrepancy is unknown.
- Amplified channel linear only to ~1.5A, so it only covers the lowest part of
  the range.
- Loads under ~70W (more at the far location) can't be measured with WiFi on.
- Power is estimated (apparent, single-point voltage model).
- Sampling is not paced by a timer, and a reading isn't a whole number of
  cycles.
- The device doesn't start measuring until WiFi connects.
- `config.h` (credentials and tuned constants) is not in version control.

## 10. Design decisions and trade-offs

- **Digital true RMS instead of an analog rectifier:** correct for any
  waveform shape (switch-mode loads, motor drives), not just sines; moves the
  waveform-sensitive step to software.
- **Analog integrator:** needed because the CT gives dI/dt; cheap and
  continuous.
- **Two output lines:** one for range, one for small-signal resolution. The
  amplified line clips early (linear only to ~1.5A), so it is kept for the
  lowest 1.5–2A and the unamplified line covers the rest.
- **Polynomial calibration blended between channels:** simple and runs on a
  microcontroller; its weakness is that it needs data everywhere it's used
  (the September gap) and can't fix a load-dependent effect.
- **5-measurement rolling window of mean-squares:** smooths 1-second noise
  while still tracking changes; mean-squares are combined rather than RMS values
  because that is the correct way to combine RMS over equal windows.
- **Redesigning the analog front end rather than filtering in software:** the
  first version's readings were inaccurate and very noisy because it had
  almost no conditioning circuitry, which is what prompted the redesign.

## 11. What I'd do next

Run the new calibration on the device and check it against the meters; improve it (record load
types, measure resistive loads from 1.5A to 6.5A and a point near 2A, use a
held-out set for accuracy); fix the WiFi noise at its source
(supply/Vref decoupling first, since the scope points there); sample on a timer
or DMA with whole-cycle windows; measure voltage and phase for real power; and
decide whether the amplified channel is worth keeping.

## 12. Glossary

- **CT:** current transformer, a clamp sensor that outputs a signal related
  to the current in the wire.
- **RMS:** root mean square, the effective value of an AC signal.
- **Integrator:** a circuit whose output is the running integral of its input.
- **Vref:** the 2.5V mid-rail bias that all analog stages are centered on.
- **ADC counts:** the ESP32's 12-bit readings (0–4095); 1 count ≈ 0.806 mV.
- **Apparent power (VA):** volts × amps, ignoring the phase between them;
  real power (W) also depends on the power factor.
- **Leave-one-out:** estimating accuracy by refitting without one point and
  predicting it.
- **DTIM:** the interval at which a WiFi access point delivers buffered
  messages to sleeping devices.

## 13. Where things live

- `ac_current_monitor_handoff_v2.md` — analog circuit, stage by stage
- `firmware_state_adc_calibration.md` — firmware, data path, noise investigation
- `callibration/CALIBRATION_METHODOLOGY.md` — calibration method, November re-fit
- `callibration/` — data CSVs, `fit_calibration.py`, `accuracy_analysis.py`,
  `refit_analysis.py`, `output/`
- `Test_plan.md` — what to measure next; `Project_plan.md` — status and roadmap
- `analysis/` — AC power vs. outdoor temperature
- `src/` — firmware (`main.cpp`, `rms_current.cpp`, `calibration.cpp`,
  `power.cpp`, `mqtt_handler.cpp`, `config.h` (untracked))
