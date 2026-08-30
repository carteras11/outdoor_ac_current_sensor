#include "rms_current.h"
#include "config.h"

static int* samples = nullptr;

void initCurrentSensor() {
    // Allocate sample buffer
    samples = new int[CURRENT_SAMPLES];
    
    // Configure ADC
    pinMode(CURRENT_SENSOR_PIN, INPUT);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);
    
    Serial.println("Current sensor initialized");
    Serial.print("Pin: GPIO");
    Serial.println(CURRENT_SENSOR_PIN);
    Serial.print("Samples per reading: ");
    Serial.println(CURRENT_SAMPLES);
}

float calculateRMS(int samples[], int count) {
    // Calculate mean
    long sum = 0;
    for (int i = 0; i < count; i++) {
        sum += samples[i];
    }
    float mean = sum / (float)count;

    //TODO: add functionality to "trim" sides
    
    // Calculate RMS
    float sum_squares = 0;
    for (int i = 0; i < count; i++) {
        float deviation = samples[i] - mean;
        sum_squares += deviation * deviation;
    }
    
    float rms_raw = sqrt(sum_squares / count);
    float rms_voltage = (rms_raw / 4095.0) * 3.3;
    
    return rms_voltage;
}

CurrentMeasurement measureCurrent(int num_readings) {
    float total_rms_voltage = 0;
    float total_sample_rate = 0;
    
    // Take multiple readings and average
    for (int reading = 0; reading < num_readings; reading++) {
        // Collect samples
        unsigned long start_time = micros();
        
        for (int i = 0; i < CURRENT_SAMPLES; i++) {
            samples[i] = analogRead(CURRENT_SENSOR_PIN);
        }
        
        unsigned long duration = micros() - start_time;
        float sample_rate = (CURRENT_SAMPLES * 1000000.0) / duration;
        
        // Calculate RMS for this reading
        float rms_voltage = calculateRMS(samples, CURRENT_SAMPLES);
        
        total_rms_voltage += rms_voltage;
        total_sample_rate += sample_rate;
    }
    
    // Average the readings
    float avg_rms_voltage = total_rms_voltage / num_readings;
    float avg_sample_rate = total_sample_rate / num_readings;
    
    // Convert to current and power
    float current_amps = avg_rms_voltage / CURRENT_VOLTS_PER_AMP;
    float power_watts = current_amps * AC_VOLTAGE * POWER_FACTOR;
    
    // Apply noise threshold (very small measurements not accurate)
    if (current_amps < NOISE_THRESHOLD_AMPS) {
        current_amps = 0.0;
        power_watts = 0.0;
    }
    
    // Return results
    CurrentMeasurement result;
    result.rms_voltage_mv = avg_rms_voltage * 1000;
    result.current_amps = current_amps;
    result.power_watts = power_watts;
    result.sample_rate_hz = avg_sample_rate;
    
    return result;
}