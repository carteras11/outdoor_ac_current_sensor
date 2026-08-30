#include "mqtt_handler.h"
#include "config.h"

PubSubClient mqttClient;

void initMQTT(WiFiClient& espClient) {
    mqttClient.setClient(espClient);
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    // Increase keepalive and socket timeout to avoid disconnects during long blocking ops
    mqttClient.setKeepAlive(60);        // seconds
    mqttClient.setSocketTimeout(30);    // seconds
}

void reconnectMQTT() {
    if (!mqttClient.connected()) {
        Serial.print("Attempting MQTT connection... ");
        
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
            Serial.println("connected");
            
            // Subscribe to topics
            mqttClient.subscribe("hvac/led");
            mqttClient.subscribe("hvac/command");
            
            Serial.println("Subscribed to control topics");
        } else {
            Serial.print("failed, rc=");
            Serial.println(mqttClient.state());
        }
    }
}

bool isMQTTConnected() {
    return mqttClient.connected();
}

void publishPower(float power) {
    if (mqttClient.connected()) {
        char payload[10];
        snprintf(payload, 10, "%.1f", power);
        mqttClient.publish(MQTT_TOPIC_POWER, payload);
    }
}


PubSubClient& getMQTTClient() {
    return mqttClient;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message on topic: ");
    Serial.println(topic);
    
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.print("Message: ");
    Serial.println(message);
    
    // Handle LED control
    if (String(topic) == "hvac/led") {
        if (message == "ON") {
            digitalWrite(LED_PIN, HIGH);
            Serial.println("LED turned ON via MQTT");
        } else if (message == "OFF") {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED turned OFF via MQTT");
        }
    }
}