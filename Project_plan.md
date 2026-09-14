# ESP32 HVAC Outdoor Current Monitor - Project Plan

## Current State (2026-08-30)

- WiFi + MQTT connectivity: working (`main.cpp`, `mqtt_handler.cpp/h`).
- Temperature sensing (DS18B20) and the on-device web server: removed for now
  to focus on getting current sensing right. Code is recoverable from git
  history (`temperature.cpp/h`, `simple_webserver.cpp/h`) if revisited later.
- Current sensing analog front-end: being reworked from a single CT + burden
  resistor + analog rectifier chain to a two-line integrator design. Full
  detail in [ac_current_monitor_handoff_v2.md](ac_current_monitor_handoff_v2.md).
  Summary:
  - CT signal is integrated in analog (recovers true `i(t)`, not just dI/dt).
  - Two parallel output lines feed the ADC, both centered on a 2.5V virtual
    ground: an unamplified line (GPIO34, for higher currents) and a ~10x
    amplified line (GPIO35, for lower currents needing more ADC resolution).
  - RMS is computed digitally (`sqrt(mean(i(t)^2))`), not via analog
    rectification, so it's accurate for non-sinusoidal loads (VFDs,
    switch-mode supplies, etc.), not just pure sine waves.
- Current calibration: done, using `callibration/measurments.csv`. Degree-2
  polynomial fits for both lines, blended below ~4A with a piecewise-linear
  weighting, implemented in `src/calibration.cpp`. See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Calibration / amps conversion") and
  [callibration/CALIBRATION_METHODOLOGY.md](callibration/CALIBRATION_METHODOLOGY.md)
  for how it was derived. Note the calibration data has no measurements
  between ~1A and ~6.7A, so the blend crossover region is extrapolated, not
  validated — worth revisiting if more calibration data is collected.
- Voltage sag / power: linear model (`src/power.cpp`,
  `calculateVoltage`/`calculatePower`) — `voltage = NO_LOAD_VOLTAGE -
  VOLTAGE_SAG_PER_AMP * amps`, `power = voltage * amps`. Assumes constant
  wire/source resistance. `NO_LOAD_VOLTAGE`/`VOLTAGE_SAG_PER_AMP` in
  `src/config.h` are **placeholders (240V, 1V/amp), not yet measured**. See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Voltage sag / power estimate").
- Firmware (`src/rms_current.cpp/h`, `src/calibration.cpp/h`,
  `src/power.cpp/h`, `src/main.cpp`): serial-only build. Samples both ADC
  lines, converts to calibrated amps via `calculateAmps()`, derives
  voltage/power from the smoothed 10-second amps, and prints amps/voltage/
  power over serial. WiFi/MQTT connection code is present (commented out)
  and `publishPower()` is ready to call, but publishing isn't wired up yet
  — being set up separately later.

## Immediate Next Steps

1. **Wire MQTT publishing back up**: uncomment the WiFi/MQTT code in
   `main.cpp` (`setup()` and `loop()`) and call `publishPower(power_10s)`.
2. **Measure and tune the voltage sag model.** Record no-load voltage and
   at least one (amps, voltage-under-load) pair to solve for
   `VOLTAGE_SAG_PER_AMP`, replacing the current 240V/1A-per-volt
   placeholders in `src/config.h`.
3. **Verify against varied load types** (linear resistive, switch-mode, LED
   driver, VFD) to confirm the true-RMS approach holds up outside pure
   sinusoids.
4. **Collect calibration data in the ~1-6A range** to validate (or correct)
   the current blend crossover, which is currently extrapolated across a gap
   in the measured data.
5. Re-evaluate whether/when to bring back temperature sensing and any web
   display, once current sensing is solid.

## Longer-Term / Parked Ideas

- Power factor measurement: peak-timing method using a resistor-divider
  voltage sense, combined with the already-integrated CT signal (see handoff
  doc's "Next steps"). The LM741 freed up by moving the integrator to the
  TL272 is available for this.
- Home Assistant dashboard/integration for the current + power MQTT topics
  (entities card, history graph, gauge card) once publishing is live again.
- Long-term data logging / statistics once the system has run for a while.

## Reference

- Analog circuit design/debugging history: `ac_current_monitor_handoff_v2.md`
- Hardware: ESP32 (nodemcu-32s board), 2x TL272 dual op-amps (analog chain),
  1x LM741 (currently free/unused), CT clamp sensor.
