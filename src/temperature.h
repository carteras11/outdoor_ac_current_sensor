#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Initialize temperature sensors
void initTemperatureSensors();

// Get temperatures from specific sensors
float getIntakeTemp();
float getDischargeTemp();

// Get sensor count
int getSensorCount();

// Print sensor info for debugging
void printSensorInfo();

// Get sensor addresses as strings (for web display)
String getIntakeAddress();
String getDischargeAddress();

#endif