#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFi.h>

// Initialize MQTT client
void initMQTT(WiFiClient& espClient);

// Connect/reconnect to MQTT broker
void reconnectMQTT();

// Check if connected
bool isMQTTConnected();

// Publish temperature data
void publishPower(float power);

// Get MQTT client for loop() processing
PubSubClient& getMQTTClient();

// MQTT callback for incoming messages
void mqttCallback(char* topic, byte* payload, unsigned int length);

#endif