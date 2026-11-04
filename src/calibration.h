#ifndef CALIBRATION_H
#define CALIBRATION_H

// Converts ADC RMS readings (raw counts, 0-4095) from both current-sense
// lines into a single calibrated amps value.
//
// - Amplified line: a straight-line curve, used while the amplified channel
//   is in its linear range (up to ~1.5A, amplified RMS <= 1000 counts).
// - Unamplified line: a cubic curve over the full range, used for everything
//   above ~2.1A (amplified RMS >= 1400 counts).
// - In between, the result fades linearly from one curve to the other,
//   selected by the amplified channel's own RMS.
// - Below a noise floor on the amplified RMS, the result is 0.
//
// See callibration/CALIBRATION_METHODOLOGY.md ("November 2026 re-fit") and
// callibration/output/refit_results.txt for how these curves and the
// crossover were derived.
float calculateAmps(float unamp_adc_rms, float amp_adc_rms);

#endif
