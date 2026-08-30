#ifndef RMS_CURRENT_H
#define RMS_CURRENT_H

#include <Arduino.h>

// Measurement result
struct CurrentMeasurement {
    float rms_voltage_mv;
    float current_amps;
    float power_watts;
    float sample_rate_hz;
};

// Initialize the current sensor
void initCurrentSensor();

// Take measurement (averages multiple RMS calculations)
CurrentMeasurement measureCurrent(int num_readings);

#endif