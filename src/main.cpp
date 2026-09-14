#include <Arduino.h>
#include "config.h"
#include "rms_current.h"
#include "calibration.h"
#include "power.h"
#include <WiFi.h>
#include "mqtt_handler.h"

// Serial-only build: samples both analog front-end lines, converts each
// second's (and each rolling 10-second) ADC RMS into calibrated amps via
// calculateAmps(), then derives voltage (sag model, see config.h) and power
// from the 10-second amps, printing all three (amps/voltage/power) over
// Serial. See callibration/CALIBRATION_METHODOLOGY.md for how the current
// calibration equations were derived, and firmware_state_adc_calibration.md
// for how the voltage/power pieces fit in. WiFi/MQTT connection code is
// kept but commented out below - MQTT wiring (publishing power, tuning
// reconnect behavior, etc.) is being set up separately later.

WiFiClient espClient;

unsigned long lastMeasurement = 0;
unsigned long lastMQTTReconnect = 0;

#define AVERAGING_WINDOW_SECONDS 10
float history_unamp_sq[AVERAGING_WINDOW_SECONDS] = {0};
float history_amp_sq[AVERAGING_WINDOW_SECONDS] = {0};
int history_index = 0;
int history_count = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); //LED off at first (still MQTT-controllable, see mqtt_handler.cpp)

    Serial.println("\n=== ESP32 HVAC Outdoor Monitor - Serial Readout ===\n");

    // WiFi/MQTT disabled for now - re-enable when ready to publish
    // readings again.
    // Serial.println("connecting to wifi");
    // WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    //
    // while (WiFi.status() != WL_CONNECTED) {
    //     delay(500);
    //     Serial.print(".");
    // }
    //
    // Serial.println("\nWiFi connected!");
    // Serial.print("IP address: ");
    // Serial.println(WiFi.localIP());
    // Serial.print("MAC address: ");
    // Serial.println(WiFi.macAddress());
    //
    // initMQTT(espClient);
    // reconnectMQTT();

    initCurrentSensor();
}

void loop() {

    // Maintain MQTT connection (disabled - see setup())
    // if (!isMQTTConnected()) {
    //     if (millis() - lastMQTTReconnect > MQTT_RECONNECT_INTERVAL) {
    //         lastMQTTReconnect = millis();
    //         reconnectMQTT();
    //     }
    // } else {
    //     getMQTTClient().loop();
    // }

    // Measure and print every second
    if (millis() - lastMeasurement >= 1000) {
        lastMeasurement = millis();

        CurrentMeasurement result = measureCurrent(CURRENT_READINGS_PER_MEASUREMENT);

        // Roll the last 10 one-second readings into a true 10s RMS: average
        // their mean-squares (not the RMS values themselves), then sqrt.
        history_unamp_sq[history_index] =
            result.adc_rms_raw_unamplified * result.adc_rms_raw_unamplified;
        history_amp_sq[history_index] =
            result.adc_rms_raw_amplified * result.adc_rms_raw_amplified;
        history_index = (history_index + 1) % AVERAGING_WINDOW_SECONDS;
        if (history_count < AVERAGING_WINDOW_SECONDS) history_count++;

        float sum_unamp_sq = 0;
        float sum_amp_sq = 0;
        for (int i = 0; i < history_count; i++) {
            sum_unamp_sq += history_unamp_sq[i];
            sum_amp_sq += history_amp_sq[i];
        }
        float avg10_unamp_raw = sqrt(sum_unamp_sq / history_count);
        float avg10_amp_raw = sqrt(sum_amp_sq / history_count);

        float amps_1s = calculateAmps(result.adc_rms_raw_unamplified,
                                       result.adc_rms_raw_amplified);
        float amps_10s = calculateAmps(avg10_unamp_raw, avg10_amp_raw);

        // Voltage/power use the smoothed 10s amps rather than the noisier
        // 1s reading.
        float voltage_10s = calculateVoltage(amps_10s);
        float power_10s = calculatePower(amps_10s);

        char line[140];
        Serial.println("---");
        sprintf(line, "1s  | Unamp RMS: %6.1f  |  Amp RMS: %6.1f  |  Amps: %6.3f A",
                result.adc_rms_raw_unamplified, result.adc_rms_raw_amplified, amps_1s);
        Serial.println(line);
        sprintf(line, "10s | Unamp RMS: %6.1f  |  Amp RMS: %6.1f  |  Amps: %6.3f A  |  Voltage: %6.1f V  |  Power: %7.1f W",
                avg10_unamp_raw, avg10_amp_raw, amps_10s, voltage_10s, power_10s);
        Serial.println(line);
        sprintf(line, "Sample rate: %.0f Hz", result.sample_rate_hz);
        Serial.println(line);

        // MQTT publishing (publishPower(power_10s)) intentionally not
        // wired up yet - see setup().
    }
}
