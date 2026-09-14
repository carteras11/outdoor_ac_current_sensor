# ESP32 HVAC Outdoor Current Monitor - Project Plan

**Project on hold as of 2026-09-14** — paused with the noise-floor
workaround in place (see below); pick up at "Immediate Next Steps" or the
noise-source investigation under "Longer-Term / Parked Ideas" when
resumed.

## Current State (2026-09-14)

- WiFi + MQTT connectivity: working, on-device confirmed (`main.cpp`,
  `mqtt_handler.cpp/h`). The WiFi connect step now forces station mode and
  drops stale state before connecting, and reports a decoded
  `WiFi.status()` + retries if not connected within 15s, instead of hanging
  silently — see
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("WiFi connect issue investigated, resolved").
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
- **Known limitation: ~70W noise floor.** On-device (with WiFi running,
  not on the bench), no-load readings sit on a noise floor of
  ~amp_adc_rms=100-160 instead of near-zero -- both channels elevated
  together in roughly the analog gain ratio, pointing to RF/power-supply
  noise entering the analog signal itself rather than an ADC/software
  timing issue. `NOISE_FLOOR_AMP_ADC` (`src/calibration.cpp`) raised from
  20 to 200 (~70W) to filter it, meaning the device currently can't
  reliably report loads under ~70W -- they read as 0. See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Noise floor / WiFi interference").
- Firmware (`src/rms_current.cpp/h`, `src/calibration.cpp/h`,
  `src/power.cpp/h`, `src/main.cpp`): connects to WiFi/MQTT, samples both
  ADC lines, converts to calibrated amps via `calculateAmps()`, derives
  voltage/power from the smoothed 5-second amps, and prints the raw
  unamp/amp ADC RMS plus amps/voltage/power over serial every second (both
  the 1s and 5s windows) for live debugging. Separately, publishes power to
  `MQTT_TOPIC_POWER` every 5 seconds, on its own timer independent of the
  1s serial print. The heartbeat LED blink has been removed; `LED_PIN` is
  now only driven by the MQTT `hvac/led` command.

## Immediate Next Steps

1. **Verify MQTT publishing end-to-end**: confirm `MQTT_TOPIC_POWER`
   payloads arrive as expected and look correct (e.g. via `mosquitto_sub`
   or Home Assistant) — WiFi connects and the device is running, but the
   actual published values haven't been checked against a subscriber yet.
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
  ("Retrospective: is the amplified channel actually still needed?"). Note
  the tension with the next item below -- *increasing* the amplified
  channel's gain is one option for the sub-70W noise floor, which would cut
  the other way; worth deciding between "drop it" and "strengthen it" once
  there's a clearer read on whether small-load measurement matters for this
  use case.
- Increase the amplified channel's gain beyond ~10x to improve
  signal-to-noise for currents under ~0.5A, which would push real small
  loads further above the ~70W noise floor described above. Only worth
  doing if measuring loads under 70W becomes a real requirement.
- **Investigate the root cause of the ~70W noise floor** — two candidate
  mechanisms not yet distinguished (shared power-rail noise from WiFi TX
  current bursts, vs. RF field coupling into the analog traces), a cheap
  diagnostic to tell them apart (run off an isolated power source and see
  if the noise floor changes), and candidate fixes ranging from cheap
  (decoupling capacitors / a dedicated analog regulator, or shielding) to
  a bigger architectural change (split the WiFi radio onto a second ESP32,
  on its own power supply, talking to the sensing board over UART — this
  fixes both candidate mechanisms at once but is a meaningfully bigger
  change than the others). Full writeup in
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Noise floor / WiFi interference" — "Root cause still unconfirmed").
- Power factor measurement: peak-timing method using a resistor-divider
  voltage sense, combined with the already-integrated CT signal (see handoff
  doc's "Next steps"). The LM741 freed up by moving the integrator to the
  TL272 is available for this.
- Home Assistant dashboard/integration for the power MQTT topic (entities
  card, history graph, gauge card) — publishing is live now, this is just
  the dashboard/UI side.
- Long-term data logging / statistics once the system has run for a while.

## Reference

- Analog circuit design/debugging history: `ac_current_monitor_handoff_v2.md`
- Hardware: ESP32 (nodemcu-32s board), 2x TL272 dual op-amps (analog chain),
  1x LM741 (currently free/unused), CT clamp sensor.
