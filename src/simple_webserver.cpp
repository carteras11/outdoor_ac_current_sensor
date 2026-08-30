#include "simple_webserver.h"
#include "temperature.h"
#include "mqtt_handler.h"
#include "config.h"
#include <WiFi.h>

WebServer server(80);

// Global variables for auto-refresh display
float lastIntakeTemp = 0.0;
float lastDischargeTemp = 0.0;

void handleRoot() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='5'>";  // Auto-refresh every 5 seconds
    html += "<style>";
    html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
    html += "h1 { color: #333; }";
    html += ".info { background: white; padding: 15px; margin: 10px 0; border-radius: 5px; }";
    html += ".temp { font-size: 24px; font-weight: bold; color: #0066cc; }";
    html += "button { padding: 10px 20px; font-size: 16px; margin: 5px; cursor: pointer; }";
    html += ".status { color: ";
    html += isMQTTConnected() ? "green" : "red";
    html += "; font-weight: bold; }";
    html += "</style></head><body>";
    
    html += "<h1>HVAC Indoor Monitor</h1>";
    
    // System Info
    html += "<div class='info'>";
    html += "<h2>System Information</h2>";
    html += "<strong>IP Address:</strong> " + WiFi.localIP().toString() + "<br>";
    html += "<strong>MAC Address:</strong> " + WiFi.macAddress() + "<br>";
    html += "<strong>MQTT Status:</strong> <span class='status'>";
    html += isMQTTConnected() ? "Connected" : "Disconnected";
    html += "</span><br>";
    html += "<strong>Sensors Found:</strong> " + String(getSensorCount());
    html += "</div>";
    
    // Sensor Addresses
    html += "<div class='info'>";
    html += "<h2>Sensor Addresses</h2>";
    html += "<strong>Intake Sensor:</strong> " + getIntakeAddress() + "<br>";
    html += "<strong>Discharge Sensor:</strong> " + getDischargeAddress();
    html += "</div>";
    
    // Current Temperatures
    html += "<div class='info'>";
    html += "<h2>Current Temperatures</h2>";
    html += "<div class='temp'>Intake: " + String(lastIntakeTemp, 1) + " °F</div>";
    html += "<div class='temp'>Discharge: " + String(lastDischargeTemp, 1) + " °F</div>";
    html += "<small>Auto-refreshes every 5 seconds</small>";
    html += "</div>";
    
    // LED Control
    html += "<div class='info'>";
    html += "<h2>LED Control</h2>";
    html += "<a href='/led/on'><button style='background:#4CAF50;color:white;'>LED ON</button></a>";
    html += "<a href='/led/off'><button style='background:#f44336;color:white;'>LED OFF</button></a>";
    html += "</div>";
    
    html += "<p><small>Device: " + String(MQTT_CLIENT_ID) + "</small></p>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

void handleLEDOn() {
    digitalWrite(LED_PIN, HIGH);
    String response = "LED turned ON";
    server.send(200, "text/plain", response);
}

void handleLEDOff() {
    digitalWrite(LED_PIN, LOW);
    String response = "LED turned OFF";
    server.send(200, "text/plain", response);
}

void handleNotFound() {
    String message = "Error 404: Page Not Found\n\n";
    message += "URI: " + server.uri() + "\n";
    message += "Method: " + String((server.method() == HTTP_GET) ? "GET" : "POST") + "\n";
    server.send(404, "text/plain", message);
}

void initWebServer() {
    server.on("/", handleRoot);
    server.on("/led/on", handleLEDOn);
    server.on("/led/off", handleLEDOff);
    server.onNotFound(handleNotFound);
}

void startWebServer() {
    server.begin();
    Serial.println("Web server started");
}

void handleWebClient() {
    server.handleClient();
}

WebServer& getWebServer() {
    return server;
}

// Update temperature cache (call from main loop)
void updateDisplayTemps(float intake, float discharge) {
    lastIntakeTemp = intake;
    lastDischargeTemp = discharge;
}