#include "temperature.h"
#include "config.h"

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress intakeAddr;
DeviceAddress dischargeAddr;

void initTemperatureSensors() {
    sensors.begin();
    
    // Copy addresses from config
    memcpy(intakeAddr, INTAKE_ADDR, 8);
    memcpy(dischargeAddr, DISCHARGE_ADDR, 8);
    
    Serial.print("Found ");
    Serial.print(sensors.getDeviceCount());
    Serial.println(" temperature sensor(s)");
}

float getIntakeTemp() {
    sensors.requestTemperatures();
    return sensors.getTempF(intakeAddr);;
}

float getDischargeTemp() {
    sensors.requestTemperatures();
    return sensors.getTempF(dischargeAddr);
}

int getSensorCount() {
    return sensors.getDeviceCount();
}

void printSensorInfo() {
    Serial.println("\n=== Temperature Sensor Info ===");
    Serial.print("Intake address: ");
    for (int i = 0; i < 8; i++) {
        if (intakeAddr[i] < 16) Serial.print("0");
        Serial.print(intakeAddr[i], HEX);
        if (i < 7) Serial.print(":");
    }
    Serial.println();
    
    Serial.print("Discharge address: ");
    for (int i = 0; i < 8; i++) {
        if (dischargeAddr[i] < 16) Serial.print("0");
        Serial.print(dischargeAddr[i], HEX);
        if (i < 7) Serial.print(":");
    }
    Serial.println("\n");
}

String getIntakeAddress() {
    String addr = "";
    for (int i = 0; i < 8; i++) {
        if (intakeAddr[i] < 16) addr += "0";
        addr += String(intakeAddr[i], HEX);
        if (i < 7) addr += ":";
    }
    return addr;
}

String getDischargeAddress() {
    String addr = "";
    for (int i = 0; i < 8; i++) {
        if (dischargeAddr[i] < 16) addr += "0";
        addr += String(dischargeAddr[i], HEX);
        if (i < 7) addr += ":";
    }
    return addr;
}