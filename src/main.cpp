#include <Arduino.h>
#include "config.h"
#include "rms_current.h"
#include <WiFi.h>
#include "mqtt_handler.h"
#include "simple_webserver.h"

WiFiClient espClient;

unsigned long lastCurrentPublish = 0;
unsigned long lastMQTTReconnect = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); //LED off at first

    Serial.println("\n=== ESP32 HVAC Outdoor Monitor ===\n");

    //Connect to wifi
    Serial.println("connecting to wifi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());

    //MQTT
    initMQTT(espClient);
    reconnectMQTT();

    //web server
    initWebServer();
    startWebServer();

    initCurrentSensor();
}


void loop() {

    handleWebClient();

    // Maintain MQTT connection
    if (!isMQTTConnected()) {
        if (millis() - lastMQTTReconnect > MQTT_RECONNECT_INTERVAL) {
            lastMQTTReconnect = millis();
            reconnectMQTT();
        }
    } else {
        getMQTTClient().loop();
    }


    
    // Publish current every 10 seconds
    if (millis() - lastCurrentPublish > PUBLISH_INTERVAL_MS) {
        lastCurrentPublish = millis();
        
        //getMQTTClient().loop();

        getMQTTClient().loop();  // Before
        CurrentMeasurement result = 
            measureCurrent(CURRENT_READINGS_PER_MEASUREMENT);
        getMQTTClient().loop();  // Before

        //getMQTTClient().loop();
        
        char string[50];
        sprintf(string, "Measured Current: %.2f\n", result.current_amps);
        Serial.print(string);
        sprintf(string, "Measured Power: %.2f\n", result.power_watts);
        Serial.print(string);
        sprintf(string, "Sample Rate: %.1f\n", result.sample_rate_hz);
        Serial.print(string);
        sprintf(string, "Measured adc_voltage: %.2f\n", result.rms_voltage_mv);
        Serial.print(string);        
        
        // Publish to MQTT
        publishPower(result.power_watts);

        
        // Update web display cache
        // updateDisplayPower(result.power_watts);
    }
}