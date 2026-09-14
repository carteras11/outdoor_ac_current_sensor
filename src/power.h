#ifndef POWER_H
#define POWER_H

// Estimated line voltage at the load, accounting for voltage sag under
// current draw. Assumes a linear drop with current (constant wire/source
// resistance) -- see NO_LOAD_VOLTAGE / VOLTAGE_SAG_PER_AMP in config.h,
// which are placeholder values pending real measurement.
float calculateVoltage(float amps);

// Real power at the load: calculateVoltage(amps) * amps.
float calculatePower(float amps);

#endif
