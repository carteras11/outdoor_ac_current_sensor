"""
Accuracy of the deployed calibration pipeline (src/calibration.cpp), scored
against the same measurements it was calibrated from.

See CALIBRATION_METHODOLOGY.md ("Accuracy analysis"). Run from callibration/:

    python3 accuracy_analysis.py

Reports two things:
  - in-sample:     the firmware equations scored on the points they were fit to.
  - leave-one-out: each point is dropped, both curves are refit without it, and
                   that point is predicted -- a less optimistic estimate that
                   uses only the same data.

Writes output/accuracy_results.txt (the literal printed output).
"""

import csv
import os
import numpy as np

CSV_PATH = "measurments.csv"
OUTPUT_DIR = "output"
BASELINE_DEVICE = "none"
AMP_CUTOFF = 4.0
MIN_AMPS = 0.5  # lower edge of the range the summaries cover

# Must match src/calibration.cpp (coefficients highest degree first).
FW_UNAMP = [-1.17767e-06, 0.014949, -0.00831701]
FW_AMP = [-2.11747e-07, 0.00162551, -0.0275632]
# The calibration data was taken with WiFi off, so use the quiet-bench noise
# floor (amp_adc_rms <= 20), not the 200 used on the WiFi-on firmware.
NOISE_FLOOR_AMP_ADC = 20.0
BLEND_I = [0.0, 1.0, 2.0, AMP_CUTOFF]
BLEND_W = [1.0, 0.75, 0.50, 0.0]

_lines = []


def log(msg=""):
    print(msg)
    _lines.append(str(msg))


def pipeline(unamp, amp, uc, ac):
    """Same logic as calculateAmps() in src/calibration.cpp."""
    if amp <= NOISE_FLOOR_AMP_ADC:
        return 0.0
    ua = float(np.polyval(uc, unamp))
    if ua <= 0:
        return 0.0
    if ua >= AMP_CUTOFF:
        return ua
    aa = float(np.polyval(ac, amp))
    w = float(np.interp(ua, BLEND_I, BLEND_W))
    return w * aa + (1.0 - w) * ua


def load():
    rows = []
    with open(CSV_PATH, newline="") as f:
        for r in csv.DictReader(f):
            if r["device_name"] == BASELINE_DEVICE:
                continue
            rows.append({
                "name": r["device_name"] or "(unnamed)",
                "u": float(r["unamp_adc_rms"]),
                "a": float(r["amp_adc_rms"]),
                "ref": float(r["avg_amps"]),
                "kaw": float(r["kaw_amps"]),
                "ss": float(r["ss_amps"]),
            })
    return rows


def summary(label, rows, est, lo):
    sel = [(r, e) for r, e in zip(rows, est) if r["ref"] >= lo]
    err = np.array([(e - r["ref"]) / r["ref"] * 100 for r, e in sel])
    ab = np.abs(err)
    log(f"  {label}: n={len(err)}  mean|err|={ab.mean():.2f}%  max|err|={ab.max():.2f}%  "
        f"within 3%: {(ab <= 3).sum()}/{len(err)}  within 5%: {(ab <= 5).sum()}/{len(err)}")


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    rows = load()

    est_in = [pipeline(r["u"], r["a"], FW_UNAMP, FW_AMP) for r in rows]

    est_loo = []
    for i, r in enumerate(rows):
        rest = [x for j, x in enumerate(rows) if j != i]
        uc = np.polyfit([x["u"] for x in rest], [x["ref"] for x in rest], 2)
        sub = [x for x in rest if x["ref"] <= AMP_CUTOFF]
        ac = np.polyfit([x["a"] for x in sub], [x["ref"] for x in sub], 2)
        est_loo.append(pipeline(r["u"], r["a"], uc, ac))

    log("Accuracy of the firmware calibration vs. the measurements it was calibrated from")
    log(f"(WiFi-off bench data, {len(rows)} points, noise floor amp_adc_rms <= "
        f"{NOISE_FLOOR_AMP_ADC:g}; reference = average of the two meters)")
    log()
    log(f"  {'ref A':>7} {'in-sample':>10} {'err%':>7} {'LOO':>8} {'err%':>7}  {'meters differ':>13}  device")
    for r, ei, el in sorted(zip(rows, est_in, est_loo), key=lambda t: t[0]["ref"]):
        spread = abs(r["kaw"] - r["ss"]) / r["ref"] * 100
        log(f"  {r['ref']:7.3f} {ei:10.3f} {(ei - r['ref']) / r['ref'] * 100:7.1f} "
            f"{el:8.3f} {(el - r['ref']) / r['ref'] * 100:7.1f}  {spread:12.1f}%  {r['name']}")
    log()

    log(f"Summaries (error = (estimate - reference) / reference):")
    log(f"  reference >= {MIN_AMPS:g} A:")
    summary("  in-sample    ", rows, est_in, MIN_AMPS)
    summary("  leave-one-out", rows, est_loo, MIN_AMPS)
    log("  reference >= 5 A (unamplified curve only):")
    summary("  in-sample    ", rows, est_in, 5.0)
    summary("  leave-one-out", rows, est_loo, 5.0)

    spreads = [abs(r["kaw"] - r["ss"]) / r["ref"] * 100 for r in rows if r["ref"] >= MIN_AMPS]
    log(f"  the two reference meters disagree with each other by a median of "
        f"{np.median(spreads):.1f}% (max {max(spreads):.1f}%) over the >= {MIN_AMPS:g} A points,")
    log("  which limits how precisely any accuracy figure can be stated.")
    log()

    refs = sorted(r["ref"] for r in rows)
    gap = max(zip(refs, refs[1:]), key=lambda p: p[1] - p[0])
    log(f"Coverage: the calibration data has no points between {gap[0]:.2f} A and {gap[1]:.2f} A, "
        f"so accuracy in that range has not been measured.")
    log("Percent error is large for very small currents partly because the reference is "
        "small and partly because the meters themselves disagree most there.")

    path = os.path.join(OUTPUT_DIR, "accuracy_results.txt")
    with open(path, "w") as f:
        f.write("\n".join(_lines) + "\n")
    print(f"\nWrote {path}")


if __name__ == "__main__":
    main()
