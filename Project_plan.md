# ESP32 HVAC Outdoor Current Monitor - Project Plan

## Current State (2026-09-13)

- WiFi + MQTT connectivity: working (`main.cpp`, `mqtt_handler.cpp/h`).
- Temperature sensing (DS18B20) and the on-device web server: removed for now
  to focus on getting current sensing right. Code is recoverable from git
  history (`temperature.cpp/h`, `simple_webserver.cpp/h`) if revisited later.
- Current sensing analog front-end: being reworked from a single CT + burden
  resistor + analog rectifier chain to a two-line integrator design. Full
  detail in [ac_current_monitor_handoff_v2.md](ac_current_monitor_handoff_v2.md).
  Summary:
  - CT signal is integrated in analog (recovers true `i(t)`, not just dI/dt).
  - Two parallel output lines feed the ADC (analog front-end centered on a
    2.5V virtual ground, landing at ~1.65V -- the ADC's own mid-scale --
    after the resistor divider): an unamplified line (GPIO34, for higher
    currents) and a ~10x amplified line (GPIO35, for lower currents
    needing more ADC resolution). Whether the amplified line is still
    worth keeping is now in question -- see
    [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
    ("Retrospective: is the amplified channel actually still needed?").
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
  `src/config.h` are now tuned from one real measurement (246.0V no-load,
  243.7V at 7.5A -> 0.3067 V/A) — a **single-point fit**, still worth
  validating with more (amps, voltage) pairs. See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Voltage sag / power estimate").
- Firmware (`src/rms_current.cpp/h`, `src/calibration.cpp/h`,
  `src/power.cpp/h`, `src/main.cpp`): serial-only build. Samples both ADC
  lines, converts to calibrated amps via `calculateAmps()`, derives
  voltage/power from the smoothed 10-second amps, and prints the raw
  unamp/amp ADC RMS plus amps/voltage/power over serial (both the 1s and
  10s windows). WiFi/MQTT connection code is present (commented out) and
  `publishPower()` is ready to call, but publishing isn't wired up yet —
  being set up separately later. The heartbeat LED blink has been removed;
  `LED_PIN` is now only driven by the MQTT `hvac/led` command.

## Immediate Next Steps

1. **Wire MQTT publishing back up**: uncomment the WiFi/MQTT code in
   `main.cpp` (`setup()` and `loop()`) and call `publishPower(power_10s)`.
   Pending final go-ahead once the current changes are reviewed/committed.
2. **Collect more voltage-sag data points** to firm up
   `VOLTAGE_SAG_PER_AMP` beyond the current single-point fit.
3. **Verify against varied load types** (linear resistive, switch-mode, LED
   driver, VFD) to confirm the true-RMS approach holds up outside pure
   sinusoids.
4. **Collect calibration data in the ~1-6A range** to validate (or correct)
   the current blend crossover, which is currently extrapolated across a gap
   in the measured data.
5. Re-evaluate whether/when to bring back temperature sensing and any web
   display, once current sensing is solid.

## Longer-Term / Parked Ideas

- Reconsider whether the amplified ADC line is still needed. The
  unamplified line's analog front end is now buffered/high-impedance
  (unlike the original passive level-shift design), and appears to resolve
  low current well on its own. Dropping the amplified line would simplify
  both the hardware (one fewer gain stage) and the firmware (one fewer
  calibration curve, no blend logic). See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Retrospective: is the amplified channel actually still needed?").
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
