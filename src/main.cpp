#include <Arduino.h>
#include "config.h"
#include "rms_current.h"
#include "calibration.h"
#include "power.h"
#include <WiFi.h>
#include "mqtt_handler.h"

// Samples both analog front-end lines, converts each second's (and each
// rolling 5-second) ADC RMS into calibrated amps via calculateAmps(), then
// derives voltage (sag model, see config.h) and power from the 5-second
// amps. Prints all of that over Serial every second (for live debugging
// with a computer plugged in) and separately publishes the 5-second power
// over MQTT every 5 seconds. See callibration/CALIBRATION_METHODOLOGY.md
// for how the current calibration equations were derived, and
// firmware_state_adc_calibration.md for how the voltage/power pieces fit
// in.

WiFiClient espClient;

unsigned long lastMeasurement = 0;
unsigned long lastMQTTReconnect = 0;
unsigned long lastMQTTPublish = 0;
#define MQTT_PUBLISH_INTERVAL_MS 5000

#define AVERAGING_WINDOW_SECONDS 5
float history_unamp_sq[AVERAGING_WINDOW_SECONDS] = {0};
float history_amp_sq[AVERAGING_WINDOW_SECONDS] = {0};
int history_index = 0;
int history_count = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); //LED off at first (still MQTT-controllable, see mqtt_handler.cpp)

    Serial.println("\n=== ESP32 HVAC Outdoor Monitor ===\n");

    Serial.print("connecting to wifi SSID: ");
    Serial.println(WIFI_SSID);
    // Explicitly force station mode and drop any stale state before
    // connecting -- the ESP32 sometimes carries over AP/STA state from a
    // previous run in a way that blocks a clean connect otherwise.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long wifiAttemptStart = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");

        // Without this, a real failure (wrong password, SSID not found,
        // 5GHz-only network, etc.) looks identical to "still connecting" --
        // it just prints dots forever. Report the status code and retry
        // instead of hanging silently.
        if (millis() - wifiAttemptStart > 15000) {
            Serial.println();
            Serial.print("Still not connected after 15s. WiFi.status() = ");
            Serial.print(WiFi.status());
            switch (WiFi.status()) {
                case WL_NO_SSID_AVAIL:
                    Serial.println(" (WL_NO_SSID_AVAIL -- SSID not found; check it's 2.4GHz, in range, and spelled/cased exactly right)");
                    break;
                case WL_CONNECT_FAILED:
                    Serial.println(" (WL_CONNECT_FAILED -- likely wrong password)");
                    break;
                case WL_DISCONNECTED:
                    Serial.println(" (WL_DISCONNECTED)");
                    break;
                case WL_IDLE_STATUS:
                    Serial.println(" (WL_IDLE_STATUS)");
                    break;
                default:
                    Serial.println();
                    break;
            }
            Serial.println("Retrying...");
            WiFi.disconnect(true);
            delay(100);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            wifiAttemptStart = millis();
        }
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());

    initMQTT(espClient);
    reconnectMQTT();

    initCurrentSensor();
}

void loop() {

    // Maintain MQTT connection
    if (!isMQTTConnected()) {
        if (millis() - lastMQTTReconnect > MQTT_RECONNECT_INTERVAL) {
            lastMQTTReconnect = millis();
            reconnectMQTT();
        }
    } else {
        getMQTTClient().loop();
    }

    // Measure and print every second
    if (millis() - lastMeasurement >= 1000) {
        lastMeasurement = millis();

        CurrentMeasurement result = measureCurrent(CURRENT_READINGS_PER_MEASUREMENT);

        // Roll the last 5 one-second readings into a true 5s RMS: average
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
        float avg5_unamp_raw = sqrt(sum_unamp_sq / history_count);
        float avg5_amp_raw = sqrt(sum_amp_sq / history_count);

        float amps_1s = calculateAmps(result.adc_rms_raw_unamplified,
                                       result.adc_rms_raw_amplified);
        float amps_5s = calculateAmps(avg5_unamp_raw, avg5_amp_raw);

        // Voltage/power use the smoothed 5s amps rather than the noisier
        // 1s reading.
        float voltage_5s = calculateVoltage(amps_5s);
        float power_5s = calculatePower(amps_5s);

        char line[140];
        Serial.println("---");
        sprintf(line, "1s | Unamp RMS: %6.1f  |  Amp RMS: %6.1f  |  Amps: %6.3f A",
                result.adc_rms_raw_unamplified, result.adc_rms_raw_amplified, amps_1s);
        Serial.println(line);
        sprintf(line, "5s | Unamp RMS: %6.1f  |  Amp RMS: %6.1f  |  Amps: %6.3f A  |  Voltage: %6.1f V  |  Power: %7.1f W",
                avg5_unamp_raw, avg5_amp_raw, amps_5s, voltage_5s, power_5s);
        Serial.println(line);
        sprintf(line, "Sample rate: %.0f Hz", result.sample_rate_hz);
        Serial.println(line);

        // Publish separately from the (1s-cadence) serial print above -
        // MQTT only gets the smoothed 5s power, once every 5 seconds.
        if (millis() - lastMQTTPublish >= MQTT_PUBLISH_INTERVAL_MS) {
            lastMQTTPublish = millis();
            publishPower(power_5s);
        }
    }
}
