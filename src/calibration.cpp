#include "calibration.h"

// Below this raw ADC RMS on the AMPLIFIED line, treat the reading as no
// measurable load rather than running it through the calibration equations
// below. The amplified line is used here (not unamplified) because it's
// the more sensitive of the two at small currents -- it moves more per amp
// near zero, so it separates "no load" from "a little load" better than
// the unamplified line would.
//
// This is set much higher than the bench no-load baseline (unamp=6.4,
// amp=10) because with WiFi running on the actual installed device, both
// channels sit on a noise floor around amp_adc_rms=100-160 (unamp ~12-18)
// even with nothing plugged in -- both channels elevated in roughly the
// analog gain ratio (~8-10x), which points to the noise entering on the
// analog signal itself (RF/EMI pickup or supply noise), not ADC/software
// timing jitter, so it isn't something firmware can average or time away.
// 200 corresponds to ~70W (~0.28A) at no-load voltage -- below that, treat
// it as unmeasurable rather than report a fake near-zero wattage. See
// firmware_state_adc_calibration.md ("Noise floor / WiFi interference")
// for the full writeup.
static const float NOISE_FLOOR_AMP_ADC = 200.0f;

// The amplified line has no basis above this current -- it saturates
// around 6A and was only fit against measurements up to 4A during
// calibration, so it is never evaluated past this point.
static const float AMP_CHANNEL_CUTOFF_AMPS = 4.0f;

// Degree-2 fits from callibration/fit_calibration.py against
// callibration/measurments.csv. See
// callibration/output/calibration_results.txt for the exact run these came
// from.
static float unampAdcToAmps(float unamp_adc_rms) {
    // amps = -1.17767e-06*unamp^2 + 0.014949*unamp - 0.00831701
    return -1.17767e-06f * unamp_adc_rms * unamp_adc_rms
         + 0.014949f * unamp_adc_rms
         - 0.00831701f;
}

static float ampAdcToAmps(float amp_adc_rms) {
    // amps = -2.11747e-07*amp^2 + 0.00162551*amp - 0.0275632
    return -2.11747e-07f * amp_adc_rms * amp_adc_rms
         + 0.00162551f * amp_adc_rms
         - 0.0275632f;
}

// Piecewise-linear weight on the AMPLIFIED channel (the unamplified channel
// gets 1 - this), keyed off the unamplified channel's own current estimate
// -- the firmware has no other "true current" signal available at runtime.
// Anchors: 0A=1.0, 1A=0.75, 2A=0.50, 4A=0.0, flat 0.0 beyond 4A.
static float amplifiedWeight(float estimated_amps) {
    if (estimated_amps <= 0.0f) return 1.0f;
    if (estimated_amps >= AMP_CHANNEL_CUTOFF_AMPS) return 0.0f;

    static const float anchor_currents[] = {0.0f, 1.0f, 2.0f, AMP_CHANNEL_CUTOFF_AMPS};
    static const float anchor_weights[]  = {1.0f, 0.75f, 0.50f, 0.0f};
    static const int anchor_count = 4;

    for (int i = 0; i < anchor_count - 1; i++) {
        if (estimated_amps <= anchor_currents[i + 1]) {
            float span = anchor_currents[i + 1] - anchor_currents[i];
            float t = (estimated_amps - anchor_currents[i]) / span;
            return anchor_weights[i] + t * (anchor_weights[i + 1] - anchor_weights[i]);
        }
    }
    return 0.0f; // unreachable
}

float calculateAmps(float unamp_adc_rms, float amp_adc_rms) {
    if (amp_adc_rms <= NOISE_FLOOR_AMP_ADC) {
        return 0.0f;
    }

    float unamp_amps = unampAdcToAmps(unamp_adc_rms);
    if (unamp_amps <= 0.0f) {
        return 0.0f;
    }
    if (unamp_amps >= AMP_CHANNEL_CUTOFF_AMPS) {
        // Amplified channel has no basis up here -- unamplified only.
        return unamp_amps;
    }

    float amp_amps = ampAdcToAmps(amp_adc_rms);
    float weight_amp = amplifiedWeight(unamp_amps);
    return weight_amp * amp_amps + (1.0f - weight_amp) * unamp_amps;
}
