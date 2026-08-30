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
- Firmware (`src/rms_current.cpp/h`, `src/main.cpp`): currently a serial-only
  calibration build. Samples both ADC lines together, prints raw ADC RMS
  (counts + volts) once per second plus a rolling 10-second RMS, so real
  current can be measured against known loads and an ADC-rms -> amps equation
  derived by hand. WiFi/MQTT still connect but nothing is published yet.

## Immediate Next Steps

1. **Run the calibration build.** Apply known resistive loads, record
   amps (measured independently) vs. the printed ADC RMS for both lines.
2. **Derive the calibration equation(s).** Likely a linear fit
   (`amps = m * adc_rms_volts + b`) per line, plus a crossover point/current
   at which to switch from the amplified line to the unamplified line.
3. **Bring MQTT current publishing back**, using the derived equation(s) in
   place of the placeholder `CURRENT_VOLTS_PER_AMP` constant from the old
   single-line implementation.
4. **Verify against varied load types** (linear resistive, switch-mode, LED
   driver, VFD) to confirm the true-RMS approach holds up outside pure
   sinusoids.
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
