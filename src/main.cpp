#include <Arduino.h>
#include "config.h"
#include "rms_current.h"
#include <WiFi.h>
#include "mqtt_handler.h"

// Serial-only ADC calibration build: prints raw ADC RMS for both analog
// front-end lines so current can be measured by hand against known loads
// and the ADC-rms -> amps equation derived. WiFi/MQTT connection code is
// kept but commented out below for now - not needed for serial calibration.

WiFiClient espClient;

unsigned long lastMeasurement = 0;
unsigned long lastMQTTReconnect = 0;
unsigned long lastHeartbeat = 0;
bool heartbeatState = false;

#define AVERAGING_WINDOW_SECONDS 10
float history_unamp_sq[AVERAGING_WINDOW_SECONDS] = {0};
float history_amp_sq[AVERAGING_WINDOW_SECONDS] = {0};
int history_index = 0;
int history_count = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); //LED off at first

    Serial.println("\n=== ESP32 HVAC Outdoor Monitor - ADC RMS Calibration ===\n");

    // WiFi/MQTT disabled for serial-only calibration - re-enable when ready
    // to publish current readings again.
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

    // Heartbeat blink, independent of the measurement cadence below -
    // if this stops toggling, the firmware is actually hung. If it keeps
    // blinking while the serial monitor goes quiet, the serial/USB link
    // dropped instead.
    if (millis() - lastHeartbeat >= 100) {
        lastHeartbeat = millis();
        heartbeatState = !heartbeatState;
        digitalWrite(LED_PIN, heartbeatState);
    }

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
        float avg10_unamp_v = (avg10_unamp_raw / 4095.0) * 3.3;
        float avg10_amp_v = (avg10_amp_raw / 4095.0) * 3.3;

        char line[100];
        Serial.println("---");
        sprintf(line, "1s  | Unamp ADC RMS: %6.1f (%.3fV)  |  Amp ADC RMS: %6.1f (%.3fV)",
                result.adc_rms_raw_unamplified, result.adc_rms_voltage_unamplified,
                result.adc_rms_raw_amplified, result.adc_rms_voltage_amplified);
        Serial.println(line);
        sprintf(line, "10s | Unamp ADC RMS: %6.1f (%.3fV)  |  Amp ADC RMS: %6.1f (%.3fV)",
                avg10_unamp_raw, avg10_unamp_v, avg10_amp_raw, avg10_amp_v);
        Serial.println(line);
        sprintf(line, "Sample rate: %.0f Hz", result.sample_rate_hz);
        Serial.println(line);
    }
}
