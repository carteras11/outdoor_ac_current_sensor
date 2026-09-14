#include "power.h"
#include "config.h"

float calculateVoltage(float amps) {
    float voltage = NO_LOAD_VOLTAGE - VOLTAGE_SAG_PER_AMP * amps;
    if (voltage < 0.0f) voltage = 0.0f;
    return voltage;
}

float calculatePower(float amps) {
    return calculateVoltage(amps) * amps;
}
