"""
November 2026 re-fit analysis.

Compares the original (September) calibration, which is what the firmware
currently contains, against the new WiFi-off measurements in
measurments_2026-11.csv, and checks whether the original measurements agree
with the new ones. Nothing here changes the firmware.

    python3 refit_analysis.py

Writes output/refit_results.txt (the literal printed output) and
output/refit_plot.png. See CALIBRATION_METHODOLOGY.md ("November 2026 re-fit").
"""

import csv
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OLD_CSV = "measurments.csv"
NEW_CSV = "measurments_2026-11.csv"
WIFI_CSV = "wifi_on_observations_2026-11.csv"
OUTPUT_DIR = "output"
AMP_CUTOFF = 4.0
MIN_AMPS = 0.5
NOISE_FLOOR_AMP_ADC = 20.0   # WiFi-off data

# Section H (crossover model)
AMP_FIT_MAX = 1.5            # amp curve is fit only up to this reference current
AMP_FADE = (1000.0, 1400.0)  # amp RMS (counts): pure amp curve below, pure unamp curve above
WEIGHT_FLOOR = 0.5           # percent-weighted fit; references below this count as this
CHOSEN_UNAMP_DEG = 3

# Coefficients currently in src/calibration.cpp (highest degree first).
FW_UNAMP = [-1.17767e-06, 0.014949, -0.00831701]
FW_AMP = [-2.11747e-07, 0.00162551, -0.0275632]
BLEND_I = [0.0, 1.0, 2.0, AMP_CUTOFF]
BLEND_W = [1.0, 0.75, 0.50, 0.0]

GROUPS = [("< 1 A", 0.0, 1.0), ("1-6.5 A", 1.0, 6.5), (">= 6.5 A", 6.5, 1e9)]

_lines = []


def log(msg=""):
    print(msg)
    _lines.append(str(msg))


def load_old():
    out = []
    with open(OLD_CSV, newline="") as f:
        for r in csv.DictReader(f):
            if r["device_name"] == "none":
                continue
            out.append((float(r["avg_amps"]), float(r["unamp_adc_rms"]), float(r["amp_adc_rms"])))
    return np.array(out)


def load_new():
    out = []
    with open(NEW_CSV, newline="") as f:
        for r in csv.DictReader(f):
            if float(r["avg_amps"]) == 0.0:   # baseline row
                continue
            out.append((float(r["avg_amps"]), float(r["unamp_adc_rms"]), float(r["amp_adc_rms"])))
    return np.array(out)


def pipeline(u, a, uc, ac, floor=NOISE_FLOOR_AMP_ADC):
    """Same logic as calculateAmps() in src/calibration.cpp."""
    if a <= floor:
        return 0.0
    ua = float(np.polyval(uc, u))
    if ua <= 0:
        return 0.0
    if ua >= AMP_CUTOFF:
        return ua
    aa = float(np.polyval(ac, a))
    w = float(np.interp(ua, BLEND_I, BLEND_W))
    return w * aa + (1.0 - w) * ua


def pct(ref, est):
    return (est - ref) / ref * 100.0


def group_summary(label, ref, est):
    log(f"  {label}")
    for name, lo, hi in GROUPS:
        m = (ref >= lo) & (ref < hi) & (ref >= MIN_AMPS)
        if m.sum() == 0:
            continue
        e = np.abs(pct(ref[m], est[m]))
        log(f"    {name:9s} n={m.sum():2d}  mean|err|={e.mean():5.1f}%  max|err|={e.max():5.1f}%")
    m = ref >= MIN_AMPS
    e = np.abs(pct(ref[m], est[m]))
    log(f"    {'all >= ' + format(MIN_AMPS, 'g') + ' A':9s} n={m.sum():2d}  mean|err|={e.mean():5.1f}%  max|err|={e.max():5.1f}%")


def loo_unamp(D, deg):
    ref, u = D[:, 0], D[:, 1]
    est = np.empty(len(D))
    for i in range(len(D)):
        m = np.arange(len(D)) != i
        est[i] = np.polyval(np.polyfit(u[m], ref[m], deg), u[i])
    return est


def loo_mixer(D, deg=2):
    est = np.empty(len(D))
    for i in range(len(D)):
        m = np.arange(len(D)) != i
        R = D[m]
        uc = np.polyfit(R[:, 1], R[:, 0], deg)
        sub = R[R[:, 0] <= AMP_CUTOFF]
        ac = np.polyfit(sub[:, 2], sub[:, 0], deg)
        est[i] = pipeline(D[i, 1], D[i, 2], uc, ac)
    return est


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    old, new = load_old(), load_new()
    both = np.vstack([old, new])

    log("November 2026 re-fit analysis")
    log(f"  original data: {len(old)} points ({OLD_CSV}, excluding its no-load row)")
    log(f"  new data:      {len(new)} points ({NEW_CSV}, excluding its 0 A baseline row; "
        f"no other points removed)")
    log(f"  error = (estimate - reference) / reference; summaries cover reference >= {MIN_AMPS:g} A")
    log()

    # ---- A. original fit on the new data
    log("A. The original (September) calibration, as currently in the firmware, applied to the new WiFi-off data")
    est_fw = np.array([pipeline(u, a, FW_UNAMP, FW_AMP) for _, u, a in new])
    log(f"   {'ref A':>7} {'reads':>7} {'err%':>7}  {'unamp':>6} {'amp':>6}")
    for (ref, u, a), e in sorted(zip(new.tolist(), est_fw), key=lambda t: t[0][0]):
        log(f"   {ref:7.3f} {e:7.3f} {pct(ref, e):7.1f}  {u:6.1f} {a:6.0f}")
    group_summary("summary:", new[:, 0], est_fw)
    log()

    # ---- B. WiFi-on observations
    log("B. WiFi-on observations (amps readings only) vs. the nearest WiFi-off point, both with the original fit")
    wifi = []
    with open(WIFI_CSV, newline="") as f:
        for r in csv.DictReader(f):
            wifi.append((float(r["avg_amps"]), float(r["fw_reading_old_fit"])))
    log(f"   {'ref A':>7} {'WiFi-on reads':>14} {'err%':>7}   nearest WiFi-off: {'ref A':>6} {'reads':>7} {'err%':>7}")
    for ref, rd in sorted(wifi):
        j = int(np.argmin(np.abs(new[:, 0] - ref)))
        log(f"   {ref:7.3f} {rd:14.3f} {pct(ref, rd):7.1f}   {'':17}{new[j, 0]:6.2f} {est_fw[j]:7.3f} {pct(new[j, 0], est_fw[j]):7.1f}")
    log()

    # ---- C. amplified channel compression
    log("C. Amplified-channel linearity: amp/unamp RMS ratio vs. current (about 11.1 while the channel is linear)")
    pts = sorted(both.tolist(), key=lambda t: t[1])
    for ref, u, a in pts:
        if 30 < u < 400:
            tag = "" if a / u > 10.9 else "   <-- compressed"
            log(f"   unamp {u:6.1f}  amp {a:7.1f}  ratio {a / u:5.2f}  ref {ref:5.2f} A{tag}")
    log()

    # ---- D. do old and new data agree where they overlap?
    log("D. Do the original and new data agree where they overlap? (A per ADC count at nearly the same unamp reading)")
    log(f"   {'new unamp':>9} {'new A/count':>12}   {'old unamp':>9} {'old A/count':>12}   difference")
    diffs = []
    for ref, u, a in sorted(new.tolist(), key=lambda t: t[1]):
        j = int(np.argmin(np.abs(old[:, 1] - u)))
        if abs(old[j, 1] - u) / u <= 0.10 and u > 30:
            rn, ro = ref / u, old[j, 0] / old[j, 1]
            diffs.append(abs(rn / ro - 1) * 100)
            log(f"   {u:9.1f} {rn:12.5f}   {old[j, 1]:9.1f} {ro:12.5f}   {(rn / ro - 1) * 100:+6.1f}%")
    log(f"   -> {len(diffs)} overlapping points, mean |difference| {np.mean(diffs):.1f}%, max {np.max(diffs):.1f}%")
    log("   Overlap only exists below ~1 A and at 6.5 A and above; the original data has nothing between.")
    log()

    # ---- E. is the mismatch in the 1-6.5 A range a curve-shape problem?
    log("E. A per ADC count (ref / unamp) by current range, new data")
    for name, lo, hi in GROUPS:
        m = (new[:, 0] >= lo) & (new[:, 0] < hi) & (new[:, 1] > 30)
        if m.sum():
            r = new[m, 0] / new[m, 1]
            log(f"   {name:9s} n={m.sum():2d}  A/count: {r.min():.4f} to {r.max():.4f}  (mean {r.mean():.4f})")
    log("   Points between 2.8 and 6 A need about 25% more amps per count than the points at 6.7 A and above;")
    log("   see the next section for what that does to any single fitted curve.")
    log()

    # ---- F. polynomial degree, all data and new data only
    log("F. Unamplified curve alone (full range): degree 1 / 2 / 3, in-sample and leave-one-out (LOO)")
    for name, D in (("original + new", both), ("new only", new)):
        log(f"  {name} (n={len(D)})")
        for deg in (1, 2, 3):
            c = np.polyfit(D[:, 1], D[:, 0], deg)
            est = np.polyval(c, D[:, 1])
            loo = loo_unamp(D, deg)
            m = D[:, 0] >= MIN_AMPS
            ei, el = np.abs(pct(D[m, 0], est[m])), np.abs(pct(D[m, 0], loo[m]))
            log(f"    degree {deg}: in-sample mean {ei.mean():5.1f}% max {ei.max():5.1f}%   "
                f"LOO mean {el.mean():5.1f}% max {el.max():5.1f}%")
    log("  (the original data alone, for reference: degree 2 max 12.8% / LOO 13.9% from the September run)")
    log()
    c3 = np.polyfit(new[:, 1], new[:, 0], 3)
    pred = np.polyval(c3, old[:, 1])
    k = old[:, 0] >= MIN_AMPS
    log(f"   Check on extrapolation: the cubic fit to the new data alone, applied to the original points, has a max error of "
        f"{np.abs(pct(old[k, 0], pred[k])).max():.0f}% (the new data stops at 11.65 A; the original reaches 16.65 A).")
    log()

    # ---- G. the DJ-mixer structure refit on everything
    log("G. The existing 'DJ mixer' structure refit on original + new data (degree 2 for both curves,")
    log("   unamp curve on all points, amp curve on points <= 4 A, blend weights unchanged)")
    uc = np.polyfit(both[:, 1], both[:, 0], 2)
    sub = both[both[:, 0] <= AMP_CUTOFF]
    ac = np.polyfit(sub[:, 2], sub[:, 0], 2)
    log(f"   unamp: amps = {uc[0]:.6g}*u^2 + {uc[1]:.6g}*u + {uc[2]:.6g}")
    log(f"   amp  : amps = {ac[0]:.6g}*a^2 + {ac[1]:.6g}*a + {ac[2]:.6g}   (fit on {len(sub)} points)")
    est_in = np.array([pipeline(u, a, uc, ac) for _, u, a in both])
    est_loo = loo_mixer(both)
    group_summary("in-sample:", both[:, 0], est_in)
    group_summary("leave-one-out:", both[:, 0], est_loo)
    log("   The amp curve here is fit partly on points where the amplified channel is compressed (section C),")
    log("   so it is asked to map the same amp RMS values (1500-1750) to different currents.")
    log()
    log("   Per-point, in-sample, for the points at 1 A and above:")
    for (ref, u, a), e in sorted(zip(both.tolist(), est_in), key=lambda t: t[0][0]):
        if ref >= 1.0:
            log(f"     ref {ref:6.2f}  reads {e:6.2f}  err {pct(ref, e):6.1f}%")
    log()

    # ---- H. unamp/amp crossover model keeping both channels
    log("H. Crossover model that keeps both channels (original + new data, all points)")
    log(f"   amp curve: straight line, fit on points <= {AMP_FIT_MAX:g} A (where the amplified channel is linear)")
    log(f"   unamp curve: polynomial fit on all points")
    log(f"   selection by the amplified channel's own RMS: amp curve alone up to {AMP_FADE[0]:.0f} counts,")
    log(f"   linear fade to the unamp curve by {AMP_FADE[1]:.0f} counts, unamp curve alone above")
    log("   (no data points fall between 999 and 1535 amp counts, so the fade width cannot be tuned from data)")
    log()

    def fit_model(D, deg, floor):
        ref_, u_, a_ = D[:, 0], D[:, 1], D[:, 2]
        wu = None if floor is None else 1.0 / np.maximum(ref_, floor)
        uc_ = np.polyfit(u_, ref_, deg, w=wu)
        m = ref_ <= AMP_FIT_MAX
        wa = None if floor is None else 1.0 / np.maximum(ref_[m], floor)
        ac_ = np.polyfit(a_[m], ref_[m], 1, w=wa)
        return uc_, ac_

    def model_est(u_, a_, uc_, ac_):
        if a_ <= NOISE_FLOOR_AMP_ADC:
            return 0.0
        if a_ <= AMP_FADE[0]:
            w = 1.0
        elif a_ >= AMP_FADE[1]:
            w = 0.0
        else:
            w = (AMP_FADE[1] - a_) / (AMP_FADE[1] - AMP_FADE[0])
        ua_ = float(np.polyval(uc_, u_))
        aa_ = float(np.polyval(ac_, a_))
        return w * aa_ + (1.0 - w) * ua_

    def loo_model(D, deg, floor):
        est = np.empty(len(D))
        for i in range(len(D)):
            m = np.arange(len(D)) != i
            uc_, ac_ = fit_model(D[m], deg, floor)
            est[i] = model_est(D[i, 1], D[i, 2], uc_, ac_)
        return est

    def rng(ref_, est_, lo, hi):
        k = (ref_ >= lo) & (ref_ < hi)
        e = np.abs(pct(ref_[k], est_[k]))
        return f"{e.mean():4.1f}/{e.max():4.1f}"

    log("   Candidates, leave-one-out error, mean/max % (unweighted = least squares in amps; weighted = least")
    log(f"   squares in percent, with references below {WEIGHT_FLOOR:g} A treated as {WEIGHT_FLOOR:g} A):")
    log(f"   {'unamp curve':26s} {'0.3-1.5 A':>11} {'1.5-6.5 A':>11} {'>=6.5 A':>11} {'all >=0.3':>11}")
    for deg in (1, 2, 3):
        for floor, tag in ((None, "unweighted"), (WEIGHT_FLOOR, "weighted")):
            est_ = loo_model(both, deg, floor)
            log(f"   degree {deg}, {tag:11s}     {rng(both[:, 0], est_, 0.3, 1.5):>11} "
                f"{rng(both[:, 0], est_, 1.5, 6.5):>11} {rng(both[:, 0], est_, 6.5, 1e9):>11} "
                f"{rng(both[:, 0], est_, 0.3, 1e9):>11}")
    log()

    uc_f, ac_f = fit_model(both, CHOSEN_UNAMP_DEG, WEIGHT_FLOOR)
    log(f"   Chosen: unamp degree {CHOSEN_UNAMP_DEG}, percent-weighted; amp straight line")
    log("   unamp: amps = " + " + ".join(f"{c:.6g}*u^{CHOSEN_UNAMP_DEG - i}" for i, c in enumerate(uc_f)).replace("*u^0", "").replace("u^1", "u"))
    log(f"   amp  : amps = {ac_f[0]:.6g}*a + {ac_f[1]:.6g}")
    xs = np.linspace(0, 1500, 1501)
    dmin = np.polyval(np.polyder(uc_f), xs).min()
    log(f"   unamp curve is increasing everywhere from 0 to 1500 counts (smallest slope {dmin:.4f} A/count); "
        f"{np.polyval(uc_f, 1244):.2f} A at 1244 counts, {np.polyval(uc_f, 1350):.2f} A at 1350")
    log()
    log("   Handoff region (a signal with amp RMS about 11.3 x unamp RMS): both curves should agree")
    log(f"   {'amp RMS':>8} {'amp curve':>10} {'unamp curve':>12}")
    for a_ in (800, 1000, 1200, 1400):
        log(f"   {a_:8d} {np.polyval(ac_f, a_):9.2f}A {np.polyval(uc_f, a_ / 11.3):11.2f}A")
    log()

    est_in = np.array([model_est(u_, a_, uc_f, ac_f) for _, u_, a_ in both])
    est_lo = loo_model(both, CHOSEN_UNAMP_DEG, WEIGHT_FLOOR)
    est_sep = np.array([pipeline(u_, a_, FW_UNAMP, FW_AMP) for _, u_, a_ in both])
    log("   Error of the chosen model by range, mean/max %:")
    log(f"   {'':26s} {'0.3-1.5 A':>11} {'1.5-6.5 A':>11} {'>=6.5 A':>11} {'all >=0.3':>11}")
    for label, est_ in (("in-sample", est_in), ("leave-one-out", est_lo), ("September fit (previous firmware)", est_sep)):
        log(f"   {label:26s} {rng(both[:, 0], est_, 0.3, 1.5):>11} {rng(both[:, 0], est_, 1.5, 6.5):>11} "
            f"{rng(both[:, 0], est_, 6.5, 1e9):>11} {rng(both[:, 0], est_, 0.3, 1e9):>11}")
    log()
    log("   Per point (reference >= 0.3 A), chosen model, in-sample and leave-one-out:")
    log(f"   {'ref A':>7} {'estimate':>9} {'err%':>6} {'LOO est':>8} {'err%':>6}  {'unamp':>6} {'amp':>6}")
    for (ref_, u_, a_), ei, el in zip(both.tolist(), est_in, est_lo):
        if ref_ >= 0.3:
            log(f"   {ref_:7.3f} {ei:9.3f} {pct(ref_, ei):6.1f} {el:8.3f} {pct(ref_, el):6.1f}  {u_:6.1f} {a_:6.0f}")
    log()
    log("   Not validated by any measurement: the fade zone (amp RMS 1000-1400, about 1.5-2.1 A), because there are")
    log("   no points between 1.48 A and 2.77 A. The amplified channel is verified linear at 1.48 A (999 counts) and")
    log("   compressed at 2.77 A (1535 counts).")
    log()

    log("Notes")
    log("  - This analysis does not change the firmware; the section H model was copied into src/calibration.cpp separately.")
    log("  - load_type is not recorded in the new CSV, so the cause of the 1-6.5 A discrepancy can't be examined here.")

    with open(os.path.join(OUTPUT_DIR, "refit_results.txt"), "w") as f:
        f.write("\n".join(_lines) + "\n")

    # ---- plot
    fig, ax = plt.subplots(1, 2, figsize=(13, 5.5))
    ax[0].scatter(old[:, 1], old[:, 0], c="tab:blue", label="original data (Sept)", zorder=3)
    ax[0].scatter(new[:, 1], new[:, 0], c="tab:orange", marker="^", label="new data (Nov, WiFi off)", zorder=3)
    xs = np.linspace(0, 1300, 300)
    ax[0].plot(xs, np.polyval(FW_UNAMP, xs), "--", color="gray", label="curve currently in firmware")
    ax[0].set_xlabel("unamp_adc_rms")
    ax[0].set_ylabel("reference amps")
    ax[0].set_title("Reference current vs. unamplified RMS")
    ax[0].legend(fontsize=8)
    ax[0].grid(alpha=0.3)
    for D, c, mk in ((old, "tab:blue", "o"), (new, "tab:orange", "^")):
        m = D[:, 1] > 30
        ax[1].scatter(D[m, 1], D[m, 2] / D[m, 1], c=c, marker=mk)
    ax[1].axhline(11.1, color="gray", ls=":")
    ax[1].set_xscale("log")
    ax[1].set_xlabel("unamp_adc_rms")
    ax[1].set_ylabel("amp RMS / unamp RMS")
    ax[1].set_title("Amplified channel gain vs. signal level (flat = linear)")
    ax[1].grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUTPUT_DIR, "refit_plot.png"), dpi=150)
    print(f"\nWrote {OUTPUT_DIR}/refit_results.txt and {OUTPUT_DIR}/refit_plot.png")


if __name__ == "__main__":
    main()
