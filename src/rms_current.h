#ifndef RMS_CURRENT_H
#define RMS_CURRENT_H

#include <Arduino.h>

// Raw ADC RMS measurement result for both analog front-end lines.
// Current/power conversion happens separately, in calibration.h/.cpp.
struct CurrentMeasurement {
    float adc_rms_raw_unamplified;     // counts, 0-4095
    float adc_rms_voltage_unamplified; // volts
    float adc_rms_raw_amplified;       // counts, 0-4095
    float adc_rms_voltage_amplified;   // volts
    float sample_rate_hz;
};

// Initialize both ADC lines
void initCurrentSensor();

// Take a measurement window (averages num_readings independent RMS
// calculations, sampling both lines together each pass)
CurrentMeasurement measureCurrent(int num_readings);

#endif
