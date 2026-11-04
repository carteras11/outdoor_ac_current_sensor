# AC Current Monitor — Analog Chain Handoff (v2)

*Last updated 2026-11-03. Component values below are what is on the board now.*

## Signal chain at a glance

```
CT --> input divider (330R / 100R) --> Cc (21uF) --> level-shifted node (Rbias 1k to Vref 2.5V)
   --> lossy integrator (R1 10k, Rf 270k || Cf 0.1uF)
   --> Cc2 (20uF) recentering (Rbias2 1k to Vref)
   --> unity buffer (TL272 #2A) --+--> 2/3 divider --> ADC1 (GPIO34)  "unamplified"
                                  |
                                  +--> gain stage (~10.1x, TL272 #2B) --> 2/3 divider --> ADC2 (GPIO35)  "amplified"
```

Design goal throughout: keep every node centered mid-range so the op-amps and the
ADC both operate in their most linear region. Analog stages center on Vref
(2.5V, mid-rail of the 5V supply); after the final 2/3 divider the ADC pins sit
centered at ~1.65V (mid-range of the ADC's 3.3V input range).

## History: why this circuit exists

The software RMS calculation (`sqrt(mean(i(t)^2))`) was already the approach in the
very first version of this project, roughly 8 months before this was written. That
first version had minimal preconditioning circuitry — no op-amps, no buffering, just
enough to get the signal into the ADC's range — so the readings were not very accurate
and the noise was very high. That is what motivated redoing the analog front end into
the circuit described in this document (integrator, recentering, buffer, and a
separate gain stage), which is much more accurate.

## Values currently on the board

All op-amp stages use TI TL272 dual op-amps (two chips, four channels total).

| Stage | Value | Notes |
|---|---|---|
| Input divider | 330 ohm (top) / 100 ohm (bottom) | ratio 100/430 = 0.233; Thevenin 330 \|\| 100 = ~77 ohm |
| Input coupling cap Cc | 21 uF | |
| Level-shift bias Rbias | 1k to Vref | |
| Integrator | R1 = 10k, Rf = 270k, Cf = 0.1 uF, Rom = 9.1k | corner ~5.9 Hz |
| Recentering cap Cc2 | 20 uF | |
| Recentering bias Rbias2 | 1k to Vref | |
| Unity buffer | TL272 #2 ch. A | provides a high-impedance source for the next stage |
| Gain stage | R1 = 1k, Rf = 9.1k, gain 1 + 9.1 = 10.1x | |
| ADC-side dividers | 2/3 on each line (two 1k in parallel on top, 1k on bottom) | scales the 5V rail to ~3.3V and moves the 2.5V bias to ~1.65V |

## Architecture (updated)

Original plan was two fully separate chains (analog RMS via precision rectifier, and a
separate digital RMS chain). That plan has changed:

- **Rectification stage is dropped.** The integrator's output is sampled directly by the
  ADC and RMS is computed entirely in software: `RMS = sqrt(mean(i(t)^2))` over a full
  cycle. This is correct for any waveform shape, unlike an analog average-responding
  rectifier + fixed scale factor, which is only accurate for pure sinusoids.
- **Two parallel analog output lines feed the ADC**, both centered on Vref (2.5V) and
  in-phase with each other:
  - **Non-amplified line** — unity gain, for higher-current loads (up to ~15-18A).
  - **Amplified line** — gain ~10x, for lower-current loads, to get usable ADC dynamic
    range out of small signals.
  - A planned software crossover picks which line to trust at a given reading.

## Constraints
- Single 5V supply, no negative rail. All stages reference a 2.5V "virtual ground" (Vref).
- Active components: 2x TI TL272 (dual op-amp, 4 channels total), 1x LM741 (currently unused/free).
- CT output is proportional to dI/dt, not I — integration is required to recover true
  current shape for non-sinusoidal loads.

## Stage 1 — Bias reference (Vref)
Unchanged, working, confirmed 2.50V with a multimeter.

- Ra = Rb = 10k divider from 5V to GND, midpoint filtered by C1 = 0.1uF
- TL272 #1, channel A, wired as unity-gain follower, buffers the divider midpoint to Vref
- C2 = 1-10uF on Vref for bulk filtering

## Stage 2 — CT input attenuation (added this round)

**Why it was needed:** the non-amplified line was hitting ~5V peak-to-peak at only 12A,
leaving no headroom before the op-amp rails. Needed to scale the signal down before it
ever reaches the level shifter.

**Final values:**
- Rtop = 330 ohm (CT signal -> tap node)
- Rbottom = 100 ohm (tap node -> GND)
- Attenuation ratio = Rbottom / (Rtop + Rbottom) = 100/430 = **0.233** (23.3% passed through)
- Divider Thevenin resistance = 330 || 100 = **~76.7 ohm** — well below the downstream
  Rbias (1k), so it doesn't get loaded down and the attenuation ratio holds accurately.

**Sizing rationale:**
- Max expected current dropped from an original ~20-24A target down to a realistic
  ~15-18A ceiling, so the divider was resized around that instead of the original
  620/510 ohm pair calculated for 24A.
- Empirically confirmed: 11.5A -> ~2.8V pp on the non-amplified line, i.e. ~0.244 V/A.
  Extrapolating linearly, 18A lands around ~4.4V pp — good headroom under the ~4.5-5V pp
  safe ceiling without wasting ADC dynamic range on unused headroom.
- Divider resistor values are low enough (Thevenin ~77 ohm) that loading on the CT
  burden resistor is not a practical concern at this scale.
- Side effect: adding Rtop/Rbottom's Thevenin resistance in series with Rbias slightly
  lowers the AC-coupling high-pass corner (marginal, not a concern).

## Stage 3 — AC-coupling / level shift onto Vref
Same topology as originally documented, final tuned values:

- Cc = 21uF (originally 10uF; raised to get the DC bias point centering tighter)
- Rbias = 1k, tied to Vref — kept low deliberately, since large Rbias values amplify any
  op-amp input offset current into a DC centering error (this was root-caused earlier
  when Rbias=150k gave a 2.25V offset instead of 2.5V)
- High-pass corner = 1/(2*pi*(Rbias + Rthevenin)*Cc) = 1/(2*pi*1077*21uF) = ~7.0 Hz,
  well below 60 Hz, so the 60 Hz signal passes essentially unattenuated.

## Stage 4 — Integrator (moved to TL272, off the LM741)

**Why it moved:** the integrator was first built on an LM741, which worked poorly in this
circuit. What was observed: under a resistive load the integrator's output swing was
compressed and lopsided (clipping more on one side than the other). The reasons, based on
typical LM741 datasheet behavior rather than measurements made here:

- **Supply voltage.** The LM741 is specified for supplies of about +/-5V to +/-15V, i.e.
  at least ~10V total. This circuit runs from a single 5V rail — half the minimum — so
  none of its specifications apply.
- **It can't swing near its supply rails.** The LM741's input and output stages work
  properly only when they stay a volt or more (typically ~1-3V) away from each supply
  rail. On a +/-15V supply that costs little. On a 0-5V supply with the signal centered at
  2.5V, it leaves very little usable swing, and the headroom isn't symmetric, which matches
  the compressed, one-sided swing that was seen.
- **Larger input currents.** The LM741 has a bipolar input stage with much larger input
  bias/offset currents (typically tens to hundreds of nanoamps) than a JFET-input part
  (picoamps). Those currents flow through the large feedback and bias resistors in this
  circuit and turn into a DC centering error.

The TL272 is a JFET-input part specified for single-supply, low-voltage operation, with
far lower input bias current, which addresses all three problems. (The recentering stage
in Stage 5 was still needed afterward, to remove the remaining DC offset.)

- U1 = TL272 #1, channel B
- R1 = 10k: level-shifted node -> pin 6 (-IN)
- Cf = 0.1uF, Rf = 270k: both in parallel, between pin 7 (output) and pin 6 (feedback)
- Rom = 9.1k: pin 5 (+IN) -> Vref
- Lossy-integrator corner: f_c = 1/(2*pi*Rf*Cf) = 1/(2*pi*270k*0.1uF) = ~5.9 Hz
- DC gain bound = -Rf/R1 = -27x (keeps small DC offsets from ramping the integrator
  output to a rail)

### Bug found and fixed: Rom miswired to GND instead of Vref
- Symptom: integrator output stuck at 0V.
- Diagnosis: measured ~2.4V at the summing junction (pin 6). This matches a *passive
  resistor-divider* calculation, not a virtual-ground calculation:
  `V(pin6) = V(level-shifted node) * Rf/(R1+Rf) + V(out) * R1/(R1+Rf)`
  `= 2.5V * (270k/280k) + 0V * (10k/280k) ~= 2.41V`
  The near-exact match confirmed the feedback loop was broken/saturated, not just
  showing a small linear offset — the amp had given up trying to hold pin 6 at Vref.
- Root cause: Rom's far end was tied to GND instead of Vref, pulling pin 5 near 0V.
  Since pin 6 tracks pin 5 in linear operation, and R1's source (level-shifted node)
  sits at 2.5V, the resulting differential exceeded what -27x gain can produce inside
  the 0-5V rail — so the amp saturated hard at 0V.
- Fix: rewired Rom to Vref. Confirmed working after fix.

### Why a 2.5V DC bus doesn't runaway the integrator (for future reference)
- Negative feedback forces pin 6 to match pin 5 (~Vref), as long as the amp has
  headroom to do so.
- Since the level-shifted node's DC average is also ~Vref, there's no persistent DC
  current through R1, so nothing accumulates — the integrator only reacts to the AC
  deviation from that shared baseline.
- Any small residual mismatch is bounded to -27x gain by Rf, rather than ramping
  indefinitely, as long as that bounded error doesn't itself exceed the rails (as it
  did in the Rom bug above).

## Waveform verification (post-fix)

Tested at 60 Hz against a waveform generator, all results consistent with correct
integrator behavior:

- **Sine -> sine**: the output stayed a clean sine.
- **Square -> triangle**, as expected (integral of a constant is a ramp).
- **Triangle -> "sin-power"-looking dome shape**: also expected. A triangle wave has
  piecewise-constant slope, and integrating a constant slope gives a parabola segment;
  alternating up/down parabolic segments produce a smooth, rounded dome rather than
  sharp points.
- **Trapezoid -> rounded, slightly-higher-amplitude sine-like shape**: expected, since a
  trapezoid's harmonic content sits between a square and a triangle wave.
- Amplitude differences between waveform shapes reflect each waveform's own frequency
  content interacting with the integrator's frequency response — not an RMS effect on
  the integrator's own output.

## RMS clarification (resolved point of confusion)

- The integrator only recovers **i(t)**, the instantaneous current waveform. It does
  not and was never going to output RMS directly.
- Two ways to get RMS from i(t):
  - **Average-responding + fixed scale factor** (the originally-planned analog
    rectifier path): only accurate for pure sinusoids, since it assumes one fixed
    ratio between average and RMS.
  - **True digital RMS**: `sqrt(mean(i(t)^2))` computed over sampled data — correct for
    any waveform shape.
- Decision: drop the rectifier stage, sample the integrator's (recentered/amplified)
  output directly, and compute true RMS in software on the ESP32. This keeps analog
  integration (cheap, continuous, good at recovering true current shape) and moves the
  waveform-sensitive part (RMS) to software where it can be done exactly.

## Stage 5 — Post-integrator recentering

**Why it was needed:** the integrator output was averaging ~2.46V instead of 2.50V
(consistent with LM741/TL272 input offset voltage multiplied by the ~27x DC gain of the
integrator stage). A unity-gain buffer alone would not fix this — a follower just
reproduces whatever DC level is already present, it does not correct it. The fix is a
second AC-coupling network, same principle as the original CT-input level shifter: it
blocks 100% of the existing DC error and re-centers cleanly on Vref regardless of what
that error's source or magnitude was.

- Cc2 = 20uF: integrator output (TL272 #1 pin 7) -> new "recentered node"
- Rbias2 = 1k: recentered node -> Vref
- f_corner = 1/(2*pi*1k*20uF) = ~8.0 Hz
- **Why 20uF?** It was chosen as a balance between two competing effects: a larger
  capacitor attenuates the signal less, but lets the DC level wander further from Vref;
  a smaller one pulls the DC level back to Vref more firmly but starts to attenuate the
  signal. At 20uF the signal amplitude and waveform shape both stayed consistent, and
  the same size (21uF) had already worked well for the first centering network at the
  input (Stage 3), so the same order of value was used here. The value was not tuned
  further than that.

## Stage 6 — Non-amplified line (unity buffer)

- TL272 #2, channel A, wired as a unity-gain follower
- +IN (pin 3) = recentered node
- -IN (pin 2) tied to output (pin 1)
- Output (pin 1) = **non-amplified line**, feeds ADC1 and also feeds the gain stage below
- Confirmed as the fix point to re-verify a clean 2.50V average with a multimeter

## Stage 7 — Amplified line (gain stage)

**Gain selection process:**
- Original assumption: signals under ~1V pp are hard to get good RMS resolution out of
  the ADC. Target ceiling ~4.5V pp headroom. Gain_ideal = ceiling / V_min = 4.5/1.0 =
  4.5x (used R1=1k, Rf=3.6k, giving 4.6x).
- Revised assumption: 0.5V pp is a more realistic minimum-readable threshold, not 1V.
  Gain_ideal = 4.5/0.5 = **9x**, rounded up to 10x for margin.
- Crossover current scales linearly with the minimum-readable-amplitude assumption
  (not by a factor of 4, since gain and the per-amp sensitivity move together):
  crossover ~= V_min / (V per amp). Halving V_min from 1.0V to 0.5V halves the crossover
  current estimate accordingly.

**Final values (gain ~10x, non-inverting, referenced to Vref):**
- R1 = 1k: pin 6 (-IN) -> Vref
- Rf = 9.1k: pin 6 (-IN) <-> pin 7 (output), feedback
- +IN (pin 5) = non-amplified line (fed from TL272 #2 channel A output)
- Gain = 1 + Rf/R1 = 1 + 9.1 = **10.1x**
- Because R1 references Vref (not GND), the DC operating point is preserved through the
  gain stage: `Vout = Vref + (Vin - Vref) * gain` — only the AC swing is multiplied.
- Output (pin 7) = **amplified line**, feeds ADC2

## Stage 8 — ADC-side scaling (both lines)

The analog stages run from a 5V rail and swing around Vref = 2.5V, but the ESP32 ADC
input is limited to ~3.3V (about 3.6V absolute max). Each output line therefore passes
through one more resistor divider right before its ADC pin:

- Top = two 1k in parallel (500 ohm), bottom = 1k → ratio = 1000/(1000+500) = **2/3**
- 5V rail swing → ~3.33V at the ADC pin; the 2.5V bias → **~1.65V**, i.e. the middle of
  the ADC's 0–3.3V range, so the signal has room to swing symmetrically in both
  directions and the ADC's most linear region is used.
- ADC1 = GPIO34 (non-amplified line), ADC2 = GPIO35 (amplified line). Both are ADC1-bank
  pins on the ESP32, chosen because ADC2-bank pins conflict with WiFi.

## Bench-measured signal levels (from earlier tests)

- 11.5A on the non-amplified line measured ~2.8V peak-to-peak (~0.244 V/A). Presumably
  taken at the line output before the Stage 8 divider was added **[verify the probe point]**.
- The "~0.19 V/A" figure that appears in older notes was from before the Stage 2 input
  divider was added and no longer applies.

## Current component pin map

**TL272 #1**
| Function | Pin |
|---|---|
| Ch. A +IN (Vref buffer) | 3 |
| Ch. A -IN / output (Vref) | 2 / 1 |
| Ch. B -IN (integrator, R1 from level-shifted node) | 6 |
| Ch. B +IN (Rom to Vref) | 5 |
| Ch. B output (integrator out, feeds recentering network) | 7 |
| V+ / GND | 8 / 4 |

**TL272 #2**
| Function | Pin |
|---|---|
| Ch. A +IN (recentered node) | 3 |
| Ch. A -IN / output (non-amplified line) | 2 / 1 |
| Ch. B +IN (from non-amplified line) | 5 |
| Ch. B -IN (R1 to Vref, Rf to output) | 6 |
| Ch. B output (amplified line) | 7 |
| V+ / GND | 8 / 4 |

**LM741** — currently unused, freed up after the integrator moved to the TL272. Available
for a future stage if needed (e.g. a dedicated buffer, or voltage-sensing path for power
factor measurement).

## Status of the original next-steps list

Done (see `firmware_state_adc_calibration.md` and `callibration/`):
- ESP32 firmware logs raw ADC RMS from both lines and computes true RMS in software
  (`sqrt(mean(i(t)^2))` on the deviation from each buffer's mean).
- Calibration sweep against known loads, with calibration curves for both lines and a
  firmware blend between them.

Still open:
- Verify both lines average a clean 2.50V at the analog stage (1.65V at the ADC pins)
  with a multimeter.
- Scope both lines against the raw integrator output to confirm waveform shape survives
  the recentering network at 60 Hz.
- Compare accuracy across load types (linear, switch-mode, LED, VFD) against a reference
  true-RMS meter.
- Power factor measurement (planned, not yet started): peak-timing method using a
  resistor divider for voltage sensing, with the CT signal integrated first as it is now.

The test procedures for these (and the WiFi-noise investigation) are in
[Test_plan.md](Test_plan.md).
