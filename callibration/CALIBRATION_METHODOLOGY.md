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

The amplified line gives better resolution at low currents but saturates
around 6A; the unamplified line turned out to be more accurate at low
current than expected, and stays valid across the whole range. The goal is
a calibration equation for each line, plus a way to blend the two so the
amplified line contributes at low current (where it's most useful) and gets
phased out before it saturates.

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

## Known data gap

As of the current `measurments.csv`, there are no measurements between
~1A and ~6.7A. The entire 1A/2A/4A blend crossover region has no supporting
data — the blended-region R^2 the script reports is extrapolation, not
validated fit quality. The script detects this gap directly from the data
(not hardcoded) and prints a warning when it's still present. If more
measurements get added in that range, the warning should stop firing on its
own.

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

## Accuracy analysis (`accuracy_analysis.py`)

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

## Re-running

```
cd callibration
python3 fit_calibration.py
python3 accuracy_analysis.py
```

Requires `pandas`, `numpy`, and `matplotlib` (the accuracy script needs only
`numpy`). Reads `measurments.csv` from the current directory; writes into
`./output/`.
