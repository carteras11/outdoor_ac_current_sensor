# ADC Calibration Script — Methodology

This describes what `fit_calibration.py` does and why, so it's clear how to
re-run it, extend it, or sanity-check its output. It does **not** contain
results — run the script and read `output/calibration_results.txt` and
`output/calibration_plot.png` for those. This doc is regenerated only when
the approach changes, not when the data changes.

## Background

The board has two analog front-end lines feeding the ESP32 ADC for the same
current-sense signal: an **unamplified** line (`unamp_adc_rms`) and a
**~10x amplified** line (`amp_adc_rms`), each reduced to an RMS value in
firmware. See `../firmware_state_adc_calibration.md` for how those RMS
values are computed on-device. `measurments.csv` pairs each RMS reading with
a reference current (`avg_amps`) measured independently (Kill-A-Watt +
another meter, averaged).

The amplified line gives better resolution at low currents but saturates;
the unamplified line turned out to be more accurate at low
current than expected, and stays valid across the whole range. The goal is
a calibration equation for each line, plus a way to blend the two so the
amplified line contributes at low current (where it's most useful) and gets
phased out before it saturates.

> **Correction (November 2026):** the September data suggested the amplified
> line saturated around 6A. Measurements taken in November show it is already
> compressed by about 2.8A and is only demonstrably linear up to about 1A
> (there is no data in between). The September-era statements below that
> mention 4A or 6A reflect that older understanding — see "November 2026
> re-fit" at the end of this document.

## Data handling rules

- **Baseline row dropped, not subtracted.** The row with `device_name ==
  "none"` (no load connected: unamp_adc_rms=6.4, amp_adc_rms=10,
  avg_amps=0.0235) is excluded from every fit. It is *not* used as a
  zero-offset to shift other readings — it's just removed from the fitting
  data, per instruction.
- **Amplified channel is fit only on rows with avg_amps <= 4.0A.** Above 4A
  the amplified line is considered to have "no basis" — not just saturated
  in a soft sense, but a channel that firmware will ignore entirely above
  that point. So the script never fits or plots amplified-channel data above
  4A; it isn't a matter of the fit being *worse* up there, that data is
  excluded outright.
- **Unamplified channel is fit on the full range** (all non-baseline rows),
  since it's meant to be used across the entire operating range, including
  above where the amplified channel cuts out.

## Polynomial degrees: 1 and 2 only

Degree 3 was tried initially and dropped: it didn't meaningfully improve R^2
over degree 2 for either channel, and in the region between measured points
(see "known data gap" below) a degree-3 fit oscillates wildly — visibly
wrong on a plot despite a competitive R^2. Degree 1 and 2 are both kept in
the output specifically because they turned out close to equally good here;
seeing both side by side is the point, not assuming one wins.

## The blend ("DJ mixer") weighting

The blend is a piecewise-linear function of current, giving a weight to the
amplified channel (the unamplified channel gets the remainder):

| current | amplified weight | unamplified weight |
|---|---|---|
| 0A | 1.00 | 0.00 |
| 1A | 0.75 | 0.25 |
| 2A | 0.50 | 0.50 |
| 4A | 0.00 | 1.00 |
| >4A | 0.00 | 1.00 |

Interpolation between anchor points is linear (a straight crossfade, not an
eased curve) — chosen for simplicity over a smooth/sigmoid transition.

The 0A weight was originally tested at three floor variants (100%/90%/75%
amplified at 0A, i.e. 0%/10%/25% unamplified floor) to see whether keeping a
little unamplified signal mixed in even at the very bottom of the range
helped. It didn't move the R^2 in any meaningful way, so the script now
just uses the simplest variant — 100% amplified at 0A — rather than carrying
three near-identical variants through the output.

**Important caveat carried through the code:** the blend weight above is
computed from the *true/reference* current (`avg_amps`), which is only
available here because this is offline calibration analysis with known
answers. At runtime, the firmware has no such value — it only has the two
raw ADC-RMS readings, which is what it's trying to convert *into* a current
in the first place. Turning this weight function into something the
firmware can actually evaluate (e.g. driven by one channel's own
provisional current estimate, or by the raw amplified ADC count relative to
its saturation point) is a deliberately separate follow-up, not addressed
by this script.

## Known data gap (original data)

The original `measurments.csv` (September 2026) has no measurements between
~1A and ~6.7A. The entire 1A/2A/4A blend crossover region had no supporting
data — the blended-region R^2 `fit_calibration.py` reports is extrapolation, not
validated fit quality. The script detects this gap directly from the data
(not hardcoded) and prints a warning when it's still present. The November
2026 measurements (`measurments_2026-11.csv`) fill this range with 8 points
between 2.77A and 5.98A; see the re-fit section below for what they showed.

## What the script computes, in order

1. Load `measurments.csv`, drop the baseline row.
2. Fit degree 1 and 2 polynomials: `avg_amps ~ unamp_adc_rms` (full range)
   and `avg_amps ~ amp_adc_rms` (<=4A subset only).
3. Check for the data gap described above and warn if present.
4. Evaluate the blended prediction against the <=4A subset (where both
   channels contribute) and report R^2.
5. Evaluate a "stitched" full-range prediction — blended below 4A, plain
   unamplified fit above — against every non-baseline row, and report R^2.
6. Print firmware follow-up notes (noise floor, amplified-channel cutoff,
   the runtime-weight-trigger problem) as reminders, without implementing
   them.

## Output

Everything the script prints is also written to
`output/calibration_results.txt` — that file is the literal captured stdout
of the run, not a separately hand-formatted report, so it will never drift
from what the script actually computed. The combined plot (both channels,
their degree-1/2 fit curves, amplified data clipped to <=4A) is written to
`output/calibration_plot.png`. Both are regenerated fresh — in full — every
time the script runs; nothing is appended or merged across runs.

## Accuracy analysis (`accuracy_analysis.py`) — original (September) data only

> **Read this with the re-fit section below.** This script scores the
> September fit on the September data, which had no points between 0.96A and
> 6.67A. Its good-looking numbers (e.g. max 5.1% above 0.5A, 1.7% above 5A)
> therefore say nothing about 1–6.5A, where the November data shows the same
> equations reading 16–26% low.

The calibration measurements are also used to state how accurate the
resulting amps readings are. The circuit and the raw ADC readings are the
same ones the curves were fit to, so the firmware's own equations (hard-coded
in the script, copied from `src/calibration.cpp` — keep them in sync) are
applied to each stored `unamp_adc_rms` / `amp_adc_rms` pair and compared with
the reference current (`avg_amps`).

- **Pipeline:** the same logic as `calculateAmps()` — noise floor, unamplified
  curve, and the blend with the amplified curve below 4A. The noise floor is
  set to 20 (the quiet-bench value) because this data was taken with WiFi off.
- **In-sample error:** percent error of that pipeline on the same points it
  was fit to.
- **Leave-one-out error:** each point is dropped in turn, both degree-2 curves
  are refit without it, and that point is predicted. This still uses only the
  calibration data but is less flattering than in-sample, since each point
  wasn't part of the fit that predicts it. Both numbers are printed.
- **Reference-meter disagreement:** the Kill-A-Watt and the second meter are
  compared point by point. Any accuracy figure can't be tighter than the
  reference it is measured against.
- **Range:** summaries cover points with reference >= 0.5A (that threshold is
  a choice, set by `MIN_AMPS` in the script), plus a separate >= 5A line.
  Individual percent errors below 0.5A are listed but large, because the
  reference is small and the two meters disagree most there. The script also
  reports the gap in the data (currently 0.96A to 6.67A), where accuracy has
  not been measured.

Output is written to `output/accuracy_results.txt`.

## November 2026 re-fit (`refit_analysis.py`)

**Status: a new model has been fit and put into `src/calibration.cpp`. The
C++ was checked against the Python model (identical estimates at the test
points) and builds; it has not yet been run on the device.**

### Why it was redone

The September fit had no data between 0.96A and 6.67A (see "Known data gap"
above), so the curve in that range was extrapolated. In November, 20 new
WiFi-off measurements were taken (`measurments_2026-11.csv`, plus a 0A
baseline row that is excluded as before), including 11 between 1.07A and
5.98A (the 1.07A, 1.26A and 1.48A points were added later the same day). The
loads were a wider mix than before (resistive loads, a fan, and an electric
yard-tool battery charger for the mid-range points); the per-point load type
was not recorded. Nothing was removed as a typo: every point has a consistent
amp/unamp ratio and a consistent amps-per-count with its neighbors.

### The original fit was a bad fit for the mid-range

Because the original fit had no mid-range data, it was extrapolated across
the gap, and the new data shows it was wrong there. The original equations
(exactly what is in the firmware) applied to the new WiFi-off data:

| Range | Points | Mean error | Max error |
|---|---|---|---|
| >= 6.5 A | 5 | 1.6% | 3.9% |
| 1–6.5 A | 11 | 16.9% | 25.7% (the 2.8–6 A points all read **low**) |

Readings at 0.65A to 1.07A are within 7%; 1.26A and 1.48A read 7–8% low. The
numbers come from `output/refit_results.txt`, section A.

### How far off was it with WiFi on?

Seven loads were also read with WiFi on (`wifi_on_observations_2026-11.csv`;
amps readings only, raw RMS not recorded; these show how far off WiFi makes
things and are not used as calibration data). Compared with the nearest
WiFi-off point (same original fit):

- **2.8A and above:** essentially the same error with WiFi on or off — within a
  few percentage points (e.g. 5.18A read 4.26A with WiFi on, 5.2A read 4.23A
  with WiFi off). So the 1–6A under-read is **not** a WiFi effect; it is
  the fit.
- **0.61A:** read about **1.05A** with WiFi on (+71%) vs. 0.606A with WiFi
  off — the WiFi noise floor shows up as roughly +0.4A (~100W) at low
  current. See the noise section of `../firmware_state_adc_calibration.md`.

### The amplified channel: linear to about 1.5A, compressed by 2.8A

The ratio of amplified RMS to unamplified RMS is about 11.1–11.4 for every
point up to 1.48A (amp RMS 999), then falls: 10.6 at 2.77A, 8.8 at 3.53A,
5.6 at 5.98A. So the amplified channel is verified linear through 1.48A and
compressed by 2.77A, with **no measurements in between** (1.48A–2.77A). The
earlier assumption that it was usable to 4A (and saturated at 6A) was wrong.
(`refit_results.txt`, section C; right panel of `output/refit_plot.png`.)

### Does the original data agree with the new data?

Yes, where they overlap. Comparing amps-per-ADC-count at nearly the same
unamplified reading, the 8 overlapping points (below ~1A and at 6.5A and
above) differ by 2.2% on average, 5.6% at most — including 6.69A new vs.
6.67A old at 471 vs. 472 counts (0.5% apart). That is also evidence the
circuit hasn't drifted over the months between the two data sets. The
original points were therefore **kept** in the combined data set (42 points).

### Is a 3rd-degree polynomial needed?

For the unamplified curve alone over the whole range (original + new, error
for reference >= 0.5A):

| Degree | In-sample mean / max | Leave-one-out mean / max |
|---|---|---|
| 1 | 9.2% / 30.8% | 9.6% / 32.2% |
| 2 | 6.2% / 14.6% | 6.5% / 15.2% |
| 3 | 5.1% / 11.7% | 5.8% / 14.2% |

Degree 2 is a big improvement over degree 1. In the crossover model below
(where the unamplified curve is only used above ~1.5A), a cubic that is fit
for percent error helps modestly over a quadratic fit the same way: leave-one-out
worst case 12.6% (degree 2) vs 9.9% (degree 3), mean 5.6% vs 4.5%. So the
cubic is not strictly necessary, but with the mid-range data it earns a small
gain, and it was checked to be increasing everywhere from 0 to 1500 counts. (A cubic fit to
the *new data alone* extrapolated badly — off by up to 53% at 16.65A — which
is why the original data was kept in the fit.)

### The unexplained problem: two groups of points that don't lie on one curve

The points between about 2.8A and 6A need about 25% more amps per ADC count
(0.0174–0.0191 A/count) than the points at 6.7A and above (0.0141–0.0146
A/count); the 1–1.5A points sit in between (0.0159–0.0169). The original and
new data agree with each other where they overlap, so this is not a stale-data
issue. The cause has **not** been determined (the load type of each point was
not recorded). A single polynomial of degree 1, 2 or 3 fits both groups only
poorly: any fit trades accuracy in one group for the other. Resistive loads
in the 1.5–6.5A range, with their type recorded, would be the way to find
out whether the difference follows the load type or the current level;
that measurement hasn't been made.

### Decision: keep both channels, crossover at about 1.5–2A

The amplified channel is kept (the project describes a two-channel,
unamplified/amplified design), used where it is demonstrably linear, with
the unamplified channel handling everything above. The model
(`refit_results.txt`, section H; implemented in `src/calibration.cpp`):

- **Amplified curve:** a straight line fit on the points up to 1.5A,
  `amps = 0.00148822 * amp_rms - 0.0138492`. (A quadratic adds nothing: the
  channel is linear in this range.)
- **Unamplified curve:** a cubic fit on all points, weighted to minimize
  *percent* error (references below 0.5A count as 0.5A, so the largest
  currents don't dominate), `amps = 4.71728e-09*u^3 - 1.03711e-05*u^2 +
  0.0193302*u - 0.0887038`.
- **Crossover:** chosen by the amplified channel's own RMS rather than by an
  amps estimate. At or below **1000 counts (about 1.5A)** use the amplified
  curve only; between 1000 and 1400 counts (about 1.5–2.1A) fade linearly to
  the unamplified curve; above 1400 counts use the unamplified curve only. The
  handoff is smooth: at 1000 / 1200 / 1400 counts the two curves differ by
  0.07 / 0.08 / 0.09A.
- **Why that threshold:** it is as high as the data supports. The amplified
  channel is verified linear at 1.48A (999 counts) and compressed at 2.77A
  (1535 counts); nothing was measured in between, so the 1000–1400 fade zone
  is an interpolation between a verified-linear point and a compressed one and
  **its width could not be tuned from data** (no point falls in it). A
  measurement at about 2A would settle it.
- **Noise floor:** `amp RMS <= 20` for WiFi-off data (the firmware currently
  uses 200 because of WiFi noise).
- Differences from the September structure: the crossover is keyed off the
  amplified RMS instead of an unamplified-derived amps estimate, the
  amplified curve is a line, and the unamplified curve is a weighted cubic.

Error of the model by reference range (mean / max, %), 42 points:

| | 0.3–1.5 A | 1.5–6.5 A | >= 6.5 A | all >= 0.3 A |
|---|---|---|---|---|
| New model, in-sample | 2.7 / 6.4 | 7.5 / 9.4 | 3.2 / 8.3 | 4.1 / 9.4 |
| New model, leave-one-out | 3.0 / 7.1 | 7.9 / 9.9 | 3.7 / 8.9 | 4.5 / 9.9 |
| September fit (the previous firmware) | 3.9 / 8.4 | 21.2 / 25.7 | 1.0 / 3.9 | 6.7 / 25.7 |

The new model cuts the worst mid-range error from about 26% to about 10%, at
the cost of being less accurate above 6.5A (mean 1.0% → 3.2%, max 3.9% →
8.3%): the unexplained two-group problem means points just below 6A read about
8% low and points just above 6.5A read 7–8% high. For comparison, simply
refitting the September structure (quadratics, amplified curve fit out to 4A,
original blend weights) on all the data gave all >= 0.5A mean 5.1% / max
12.8% in-sample (13.4% leave-one-out), and it fits the amplified curve partly
on compressed points (section G of `refit_results.txt`).

**Still to do:** measure resistive loads between 1.5A and 6.5A (recording the
load type), and a load near 2A to validate the fade zone.

### Re-running

```
cd callibration
python3 fit_calibration.py        # original (September) fit and plot
python3 accuracy_analysis.py      # accuracy of the September fit on September data
python3 refit_analysis.py         # November comparison described above
```

`fit_calibration.py` and `accuracy_analysis.py` are unchanged so the
September results stay reproducible.

Requires `pandas`, `numpy`, and `matplotlib` (the accuracy and re-fit scripts
need only `numpy`, plus `matplotlib` for the re-fit plot). Reads CSVs from
the current directory; writes into `./output/`.
