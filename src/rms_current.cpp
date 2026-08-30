#include "rms_current.h"
#include "config.h"

static int* samples_unamplified = nullptr;
static int* samples_amplified = nullptr;

void initCurrentSensor() {
    // Allocate sample buffers
    samples_unamplified = new int[CURRENT_SAMPLES];
    samples_amplified = new int[CURRENT_SAMPLES];

    // Configure ADC
    pinMode(CURRENT_SENSOR_PIN_UNAMPLIFIED, INPUT);
    pinMode(CURRENT_SENSOR_PIN_AMPLIFIED, INPUT);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    Serial.println("Current sensor initialized");
    Serial.print("Unamplified line: GPIO");
    Serial.println(CURRENT_SENSOR_PIN_UNAMPLIFIED);
    Serial.print("Amplified line: GPIO");
    Serial.println(CURRENT_SENSOR_PIN_AMPLIFIED);
    Serial.print("Samples per reading: ");
    Serial.println(CURRENT_SAMPLES);
}

// Both lines are centered on ~2.5V (Vref), not 0V, so RMS is computed on the
// deviation from each buffer's own mean rather than on the raw ADC counts.
static float calculateRMS(int samples[], int count) {
    long sum = 0;
    for (int i = 0; i < count; i++) {
        sum += samples[i];
    }
    float mean = sum / (float)count;

    float sum_squares = 0;
    for (int i = 0; i < count; i++) {
        float deviation = samples[i] - mean;
        sum_squares += deviation * deviation;
    }

    return sqrt(sum_squares / count);
}

CurrentMeasurement measureCurrent(int num_readings) {
    float total_rms_raw_unamplified = 0;
    float total_rms_raw_amplified = 0;
    float total_sample_rate = 0;

    for (int reading = 0; reading < num_readings; reading++) {
        unsigned long start_time = micros();

        // Interleave reads so both lines are sampled over the same span
        for (int i = 0; i < CURRENT_SAMPLES; i++) {
            samples_unamplified[i] = analogRead(CURRENT_SENSOR_PIN_UNAMPLIFIED);
            samples_amplified[i] = analogRead(CURRENT_SENSOR_PIN_AMPLIFIED);
        }

        unsigned long duration = micros() - start_time;
        float sample_rate = (CURRENT_SAMPLES * 1000000.0) / duration;

        total_rms_raw_unamplified += calculateRMS(samples_unamplified, CURRENT_SAMPLES);
        total_rms_raw_amplified += calculateRMS(samples_amplified, CURRENT_SAMPLES);
        total_sample_rate += sample_rate;
    }

    CurrentMeasurement result;
    result.adc_rms_raw_unamplified = total_rms_raw_unamplified / num_readings;
    result.adc_rms_raw_amplified = total_rms_raw_amplified / num_readings;
    result.adc_rms_voltage_unamplified = (result.adc_rms_raw_unamplified / 4095.0) * 3.3;
    result.adc_rms_voltage_amplified = (result.adc_rms_raw_amplified / 4095.0) * 3.3;
    result.sample_rate_hz = total_sample_rate / num_readings;

    return result;
}
