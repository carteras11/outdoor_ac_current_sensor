# ESP32 HVAC Outdoor Current Monitor - Project Plan

**Resumed 2026-11-03** after being on hold since 2026-09-14. Current focus:
fixing the calibration (new measurements showed the firmware reads 16–26% low
between 2.8A and 6A; a better model is now in the firmware but not yet run on the device) and understanding the WiFi-related noise floor (first
scope captures taken). Tests are laid out in [Test_plan.md](Test_plan.md); a
full project summary for interview prep is in
[PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md).

## Current State (2026-11-03)

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
- **Current calibration: new model in the firmware, not yet run on the
  device.** The September firmware used degree-2 fits for both lines, blended
  below ~4A, from `callibration/measurments.csv`, which had no data between
  ~1A and ~6.7A. New WiFi-off measurements
  (`callibration/measurments_2026-11.csv`, 20 points) showed those equations
  read **16–26% low between 2.8A and 6A** (within ~4% at 6.5A and above). The
  amplified channel is verified linear to 1.48A and compressed by 2.77A. The
  original data agrees with the new data where they overlap (kept in the
  combined set). `src/calibration.cpp` now keeps both channels: a
  straight-line amplified curve up to 1000 amplified-RMS counts (~1.5A), a fade
  to a weighted cubic unamplified curve by 1400 counts (~2.1A), unamplified
  only above. Leave-one-out error over all points: mean 4.5%, worst case 9.9%
  (vs. mean 6.7%, worst 25.7% for the September fit), at the cost of being a
  little less accurate above 6.5A. The C++ matches the Python fit and builds.
  The fade zone is unvalidated, and the cause of the mid-range two-group
  discrepancy is unknown. See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Calibration / amps conversion") and
  [callibration/CALIBRATION_METHODOLOGY.md](callibration/CALIBRATION_METHODOLOGY.md)
  ("November 2026 re-fit").
- Voltage sag / power: linear model (`src/power.cpp`,
  `calculateVoltage`/`calculatePower`) — `voltage = NO_LOAD_VOLTAGE -
  VOLTAGE_SAG_PER_AMP * amps`, `power = voltage * amps`. Assumes constant
  wire/source resistance. `NO_LOAD_VOLTAGE`/`VOLTAGE_SAG_PER_AMP` in
  `src/config.h` are now tuned from one real measurement (246.0V no-load,
  243.7V at 7.5A -> 0.3067 V/A) — a **single-point fit**, still worth
  validating with more (amps, voltage) pairs. See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Voltage sag / power estimate").
- **Known limitation: WiFi noise floor.** On-device (with WiFi running,
  not on the bench), no-load readings sit on a noise floor of
  ~amp_adc_rms=100-160 instead of near-zero, with both channels elevated;
  the cause (analog-side pickup vs. supply noise vs. something else) is
  **not yet confirmed**. `NOISE_FLOOR_AMP_ADC` (`src/calibration.cpp`) was
  raised from 20 to 200 (~70W) to filter it, so loads under ~70W read as 0.
  After moving the device farther from the router the floor rose to ~150-200W
  and the 200-count threshold no longer covers it (not yet changed, pending
  more data). Scope captures (2026-11-03) show the 5V line dipping to
  ~4.8–4.9V in spikes ~5 times a second with WiFi on, with matching spikes
  on both signal lines (amplified line 3.1V peak-to-peak vs. a clean 60 Hz
  waveform with WiFi off); the cause is still a hypothesis. See
  [firmware_state_adc_calibration.md](firmware_state_adc_calibration.md)
  ("Noise floor / WiFi interference") and [Test_plan.md](Test_plan.md).
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

0. **Upload the new calibration and check it on the device** against the
   reference meters (`callibration/CALIBRATION_METHODOLOGY.md`, "November 2026
   re-fit"), then run the remaining tests in [Test_plan.md](Test_plan.md): resistive loads
   between 1.5A and 6.5A, a load near 2A, recording load type for every
   point, and the remaining scope probing.
1. **Verify MQTT publishing end-to-end**: confirm `MQTT_TOPIC_POWER`
   payloads arrive as expected and look correct (e.g. via `mosquitto_sub`
   or Home Assistant) — WiFi connects and the device is running, but the
   actual published values haven't been checked against a subscriber yet.
2. **Collect more voltage-sag data points** to firm up
   `VOLTAGE_SAG_PER_AMP` beyond the current single-point fit.
3. **Verify against varied load types** (linear resistive, switch-mode, LED
   driver, VFD) to confirm the true-RMS approach holds up outside pure
   sinusoids.
4. **Fill the remaining calibration gap (about 1A to 2.7A).** The 2.8–6A
   range was measured in November; there are still no points between 0.96A
   and 2.77A.
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
  doing if measuring loads under 70W becomes a real requirement. Note the
  amplified channel already clips above roughly 1–2A at its current ~11x
  gain, so more gain would shrink its usable range further.
- **Investigate the root cause of the ~70W noise floor** — two candidate
  mechanisms not yet distinguished (shared power-rail noise from WiFi TX
  current bursts, vs. RF field coupling into the analog traces); trying
  different external power supplies made no difference to the noise, and
  the next step is probing the analog lines with WiFi on vs. off
  (`Test_plan.md`). Candidate fixes range from cheap
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
