#include "calibration.h"

// Below this raw ADC RMS on the AMPLIFIED line, treat the reading as no
// measurable load. The amplified line is used here (not unamplified) because
// it's the more sensitive of the two at small currents.
//
// 200 is set well above the quiet bench no-load baseline (unamp=6.4,
// amp=10) because with WiFi running on the installed device the no-load
// readings sit on a noise floor (amp RMS roughly 100-160 at first, higher
// farther from the router). 200 is about 0.28A / 70W; the floor has since
// been seen as high as ~150-200W, so it may need raising. See
// firmware_state_adc_calibration.md ("Noise floor / WiFi interference").
static const float NOISE_FLOOR_AMP_ADC = 200.0f;

// The two channels are combined using the amplified channel's own RMS:
//   at or below AMP_FADE_START_RMS   -> amplified curve only
//   between START and END            -> linear fade to the unamplified curve
//   at or above AMP_FADE_END_RMS     -> unamplified curve only
// The amplified line is verified linear at 1.48A (amp RMS 999) and is
// compressed by 2.77A (amp RMS 1535); nothing was measured in between, so the
// 1000-1400 fade zone (about 1.5-2.1A) is an interpolation, not validated.
static const float AMP_FADE_START_RMS = 1000.0f;
static const float AMP_FADE_END_RMS = 1400.0f;

// Curves fit in callibration/refit_analysis.py (section H) to
// callibration/measurments.csv + measurments_2026-11.csv; see
// callibration/output/refit_results.txt and
// callibration/CALIBRATION_METHODOLOGY.md ("November 2026 re-fit").

// Unamplified line: cubic, fit to minimize percent error over all points.
static float unampAdcToAmps(float u) {
    // amps = 4.71728e-09*u^3 - 1.03711e-05*u^2 + 0.0193302*u - 0.0887038
    return ((4.71728e-09f * u - 1.03711e-05f) * u + 0.0193302f) * u - 0.0887038f;
}

// Amplified line: straight line, fit on points up to 1.5A where the channel
// is linear.
static float ampAdcToAmps(float a) {
    // amps = 0.00148822*amp - 0.0138492
    return 0.00148822f * a - 0.0138492f;
}

// Fraction of the result taken from the amplified curve (the rest comes from
// the unamplified curve).
static float amplifiedWeight(float amp_adc_rms) {
    if (amp_adc_rms <= AMP_FADE_START_RMS) return 1.0f;
    if (amp_adc_rms >= AMP_FADE_END_RMS) return 0.0f;
    return (AMP_FADE_END_RMS - amp_adc_rms) / (AMP_FADE_END_RMS - AMP_FADE_START_RMS);
}

float calculateAmps(float unamp_adc_rms, float amp_adc_rms) {
    if (amp_adc_rms <= NOISE_FLOOR_AMP_ADC) {
        return 0.0f;
    }

    float w = amplifiedWeight(amp_adc_rms);
    float amps = 0.0f;
    if (w > 0.0f) amps += w * ampAdcToAmps(amp_adc_rms);
    if (w < 1.0f) amps += (1.0f - w) * unampAdcToAmps(unamp_adc_rms);

    return amps > 0.0f ? amps : 0.0f;
}
