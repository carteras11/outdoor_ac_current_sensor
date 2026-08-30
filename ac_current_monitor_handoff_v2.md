# AC Current Monitor — Analog Chain Handoff (v2)

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
- Active components: 2x TL272 (dual op-amp), 1x LM741 (currently unused/free).
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
- Rtop = 333 ohm (CT signal -> tap node)
- Rbottom = 100 ohm (tap node -> GND)
- Attenuation ratio = Rbottom / (Rtop + Rbottom) = 100/433 = **0.231** (23.1% passed through)
- Divider Thevenin resistance = 333 || 100 = **76.9 ohm** — well below the downstream
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

- Cc = 30uF (raised from the original 10uF to get the DC bias point centering tighter)
- Rbias = 1k, tied to Vref — kept low deliberately, since large Rbias values amplify any
  op-amp input offset current into a DC centering error (this was root-caused earlier
  when Rbias=150k gave a 2.25V offset instead of 2.5V)

## Stage 4 — Integrator (moved to TL272, off the LM741)

**Why it moved:** the LM741 is only rated for supplies from about +/-5V to +/-15V — a
single 5V rail is *below* its minimum spec, not just close to the edge of it. That
explained the asymmetric, compressed swing seen under resistive load. The TL272 is a
JFET-input part explicitly rated for single-supply low-voltage operation, and also has
much lower input bias current, which helps the offset/centering issues too.

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

Tested against a waveform generator, all results consistent with correct integrator
behavior:

- **Sine -> sine**, ~45 degrees of extra phase lag. This is frequency-dependent
  (phase lag = -arctan(f/f_c)), and hits exactly -45 degrees at f = f_c (~5.9 Hz here) —
  meaning the test was run near the integrator's corner frequency, not at 60 Hz. At
  60 Hz (~10x the corner) the response should be much closer to -84 degrees /
  near-ideal integrator phase.
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
  output directly, and compute true RMS in software on the STM32. This keeps analog
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

- Cc2 = 100uF: integrator output (TL272 #1 pin 7) -> new "recentered node"
- Rbias2 = 1k: recentered node -> Vref
- f_corner = 1/(2*pi*1k*100uF) = ~1.6 Hz

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

## Next steps
- Verify non-amplified line and amplified line both average a clean 2.50V with a
  multimeter.
- Scope both lines together against the raw integrator output to confirm waveform shape
  survives the recentering network at 60 Hz.
- Set up STM32 firmware to log raw ADC values from both lines.
- Run a calibration sweep against known resistive loads to build a current-vs-ADC-value
  curve for both lines, and pick a firmware crossover threshold from that data (current
  estimate: somewhere in the low single-digit amps, refine empirically).
- Implement software true-RMS calculation: `sqrt(mean(i(t)^2))` over sampled cycles.
- Compare accuracy across load types (linear, switch-mode, LED, VFD) against a reference
  true-RMS meter.
- Power factor measurement (planned, not yet started): peak-timing method using a
  resistor divider for voltage sensing, with the CT signal integrated first as it is now.
