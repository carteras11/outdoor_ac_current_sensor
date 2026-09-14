"""
ADC-RMS -> amps calibration curve fitting.

See CALIBRATION_METHODOLOGY.md for the full explanation of what this script
does and why. Short version: reads measurments.csv, fits degree 1 and 2
polynomials for the unamplified line (full range) and the amplified line
(<=4A only, since that channel has no basis above 4A), then evaluates a
piecewise-linear low-current blend of the two.

Run it from the `callibration/` directory:

    python3 fit_calibration.py

All output (the printed results and the plot) is written into ./output/.
Nothing here is hand-authored after the fact -- output/calibration_results.txt
is exactly what this script printed, and output/calibration_plot.png is
exactly what this script plotted.
"""

import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

CSV_PATH = "measurments.csv"
OUTPUT_DIR = "output"
BASELINE_DEVICE = "none"
AMP_CUTOFF = 4.0  # amplified channel has no basis above this; omitted entirely
DEGREES = (1, 2)
UNAMP_FLOOR_PCT = 0.0  # weight on unamplified channel at 0A (0% == pure amplified)
ANCHOR_CURRENTS = [0.0, 1.0, 2.0, AMP_CUTOFF]
ANCHOR_WEIGHTS_AMP = [1.0 - UNAMP_FLOOR_PCT, 0.75, 0.50, 0.0]

_log_lines = []


def log(msg=""):
    print(msg)
    _log_lines.append(str(msg))


def r_squared(y_true, y_pred):
    y_true = np.asarray(y_true, dtype=float)
    y_pred = np.asarray(y_pred, dtype=float)
    ss_res = np.sum((y_true - y_pred) ** 2)
    ss_tot = np.sum((y_true - np.mean(y_true)) ** 2)
    return 1.0 - ss_res / ss_tot


def fit_polys(x, y, degrees=DEGREES):
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    out = {}
    for d in degrees:
        coeffs = np.polyfit(x, y, d)
        y_pred = np.polyval(coeffs, x)
        out[d] = {"coeffs": coeffs, "r2": r_squared(y, y_pred), "n": len(x)}
    return out


def format_poly(coeffs, var="x"):
    d = len(coeffs) - 1
    terms = []
    for c in coeffs:
        if d == 0:
            terms.append(f"{c:.6g}")
        elif d == 1:
            terms.append(f"{c:.6g}*{var}")
        else:
            terms.append(f"{c:.6g}*{var}^{d}")
        d -= 1
    return " + ".join(terms)


def amp_weight(current):
    """Fraction of the blended prediction taken from the AMPLIFIED channel,
    piecewise-linear between the DJ-mixer anchor points."""
    if current <= ANCHOR_CURRENTS[0]:
        return ANCHOR_WEIGHTS_AMP[0]
    if current >= ANCHOR_CURRENTS[-1]:
        return ANCHOR_WEIGHTS_AMP[-1]
    return float(np.interp(current, ANCHOR_CURRENTS, ANCHOR_WEIGHTS_AMP))


def print_fits(title, fits, var):
    log(title)
    for d in DEGREES:
        log(f"  deg {d}: R^2={fits[d]['r2']:.5f}  y = {format_poly(fits[d]['coeffs'], var)}")
    log()


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    df = pd.read_csv(CSV_PATH)

    baseline_rows = df[df["device_name"] == BASELINE_DEVICE]
    data = df[df["device_name"] != BASELINE_DEVICE].copy()

    x_unamp = data["unamp_adc_rms"].to_numpy(dtype=float)
    y = data["avg_amps"].to_numpy(dtype=float)

    amp_data = data[data["avg_amps"] <= AMP_CUTOFF]
    x_amp = amp_data["amp_adc_rms"].to_numpy(dtype=float)
    y_amp = amp_data["avg_amps"].to_numpy(dtype=float)

    log(f"Loaded {len(df)} rows, dropped {len(baseline_rows)} baseline row(s) "
        f"(device_name == '{BASELINE_DEVICE}').")
    log(f"Unamplified fit uses all {len(data)} non-baseline rows (full range).")
    log(f"Amplified fit uses only the {len(amp_data)} rows with avg_amps <= "
        f"{AMP_CUTOFF}A (channel has no basis above that).")
    log()

    gap_lo = y[y <= AMP_CUTOFF].max() if (y <= AMP_CUTOFF).any() else 0.0
    gap_hi = y[y > gap_lo].min() if (y > gap_lo).any() else None
    if gap_hi is not None and gap_hi - gap_lo > 1.0:
        log(f"*** DATA GAP: no measurements between {gap_lo:.3f}A and {gap_hi:.3f}A. "
            f"The 1A/2A/4A blend anchors fall entirely inside this gap -- the "
            f"blended R^2 below is NOT validated by any actual data in the "
            f"crossover zone. Treat it as extrapolation, not evidence. ***")
        log()

    unamp_fits = fit_polys(x_unamp, y)
    amp_fits = fit_polys(x_amp, y_amp)

    print_fits("Unamplified, full range:", unamp_fits, "unamp")
    print_fits(f"Amplified, <= {AMP_CUTOFF}A only:", amp_fits, "amp")

    log(f"Blend weight on amplified channel (piecewise-linear): "
        f"0A={ANCHOR_WEIGHTS_AMP[0]:.2f}, 1A=0.75, 2A=0.50, {AMP_CUTOFF}A=0.00, "
        f"flat 0.00 beyond {AMP_CUTOFF}A.")
    log()

    log(f"Blended ('DJ mixer') R^2, evaluated on the <= {AMP_CUTOFF}A subset "
        f"(n={len(y_amp)}):")
    for degree in DEGREES:
        pred_unamp = np.polyval(unamp_fits[degree]["coeffs"], amp_data["unamp_adc_rms"].to_numpy(dtype=float))
        pred_amp = np.polyval(amp_fits[degree]["coeffs"], x_amp)
        w_amp = np.array([amp_weight(c) for c in y_amp])
        pred_blend = w_amp * pred_amp + (1.0 - w_amp) * pred_unamp
        r2 = r_squared(y_amp, pred_blend)
        log(f"  deg {degree}: R^2={r2:.5f}")
    log()

    log("Stitched full-range pipeline (blend below "
        f"{AMP_CUTOFF}A using both channels, plain unamplified fit above, "
        f"evaluated on all {len(y)} non-baseline rows):")
    for degree in DEGREES:
        pred_unamp_all = np.polyval(unamp_fits[degree]["coeffs"], x_unamp)
        stitched = np.array(pred_unamp_all, copy=True)
        below_mask = y <= AMP_CUTOFF
        below_idx = data.index[below_mask]
        amp_adc_below = data.loc[below_idx, "amp_adc_rms"].to_numpy(dtype=float)
        pred_amp_below = np.polyval(amp_fits[degree]["coeffs"], amp_adc_below)
        w_amp_below = np.array([amp_weight(c) for c in y[below_mask]])
        stitched[below_mask] = (
            w_amp_below * pred_amp_below + (1.0 - w_amp_below) * pred_unamp_all[below_mask]
        )
        r2 = r_squared(y, stitched)
        log(f"  deg {degree}: R^2={r2:.5f}")
    log()

    log("Notes / firmware follow-ups (not implemented here):")
    log(f"  - No-load baseline was unamp_adc_rms={baseline_rows['unamp_adc_rms'].iloc[0]}, "
        f"amp_adc_rms={baseline_rows['amp_adc_rms'].iloc[0]}. At runtime, readings at or "
        f"below roughly unamp_adc_rms=7.0 / amp_adc_rms=20 should be treated as "
        f"no measurable load and reported as 0A.")
    log(f"  - The amplified channel should be ignored entirely above {AMP_CUTOFF}A in "
        f"firmware, matching the cutoff used here.")
    log("  - The blend weight above is a function of true/reference amps, which the "
        "firmware does not have at runtime. Translating this into a runtime-usable "
        "trigger is a separate follow-up.")

    results_path = os.path.join(OUTPUT_DIR, "calibration_results.txt")
    with open(results_path, "w") as f:
        f.write("\n".join(_log_lines) + "\n")
    log(f"\nWrote {results_path}")

    # ---------------- combined plot ----------------
    fig, ax = plt.subplots(figsize=(9, 6.5))
    ax.scatter(x_unamp, y, color="tab:blue", label="unamp_adc_rms (measured)", zorder=5)
    ax.scatter(x_amp, y_amp, color="tab:orange", marker="^",
               label=f"amp_adc_rms (measured, <= {AMP_CUTOFF}A only)", zorder=5)

    xs_unamp_plot = np.linspace(0, x_unamp.max() * 1.05, 200)
    xs_amp_plot = np.linspace(0, x_amp.max() * 1.05, 200)
    styles = {1: "--", 2: "-."}
    for d in DEGREES:
        ax.plot(xs_unamp_plot, np.polyval(unamp_fits[d]["coeffs"], xs_unamp_plot),
                styles[d], color="tab:blue",
                label=f"unamp deg {d} (R^2={unamp_fits[d]['r2']:.4f})")
        ax.plot(xs_amp_plot, np.polyval(amp_fits[d]["coeffs"], xs_amp_plot),
                styles[d], color="tab:orange",
                label=f"amp deg {d} (R^2={amp_fits[d]['r2']:.4f})")

    ax.set_title("ADC RMS vs amps -- unamplified and amplified channels")
    ax.set_xlabel("ADC RMS (counts)")
    ax.set_ylabel("avg_amps")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    fig.tight_layout()

    plot_path = os.path.join(OUTPUT_DIR, "calibration_plot.png")
    fig.savefig(plot_path, dpi=150)
    print(f"Wrote {plot_path}")


if __name__ == "__main__":
    main()
