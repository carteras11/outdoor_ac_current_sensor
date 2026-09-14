#ifndef CALIBRATION_H
#define CALIBRATION_H

// Converts ADC RMS readings (raw counts, 0-4095) from both current-sense
// lines into a single calibrated amps value.
//
// - Unamplified line: degree-2 polynomial fit across the full measured
//   range.
// - Amplified line: degree-2 polynomial fit valid only up to ~4A -- it has
//   no basis and is never evaluated above that current.
// - Below ~4A (as estimated by the unamplified line, the only signal the
//   firmware has to go on), the two channels are blended with a
//   piecewise-linear "DJ mixer" weighting that favors the amplified line at
//   low current, where it has more ADC resolution, and fades it out by 4A.
//
// See callibration/CALIBRATION_METHODOLOGY.md and
// callibration/output/calibration_results.txt for how these coefficients
// and the blend weighting were derived.
float calculateAmps(float unamp_adc_rms, float amp_adc_rms);

#endif
