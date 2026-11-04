# Test Plan

*Written 2026-11-03. This is a plan only — no results are recorded here. Record
results in your own data files and add the conclusions to the docs afterward.*

Two goals:

1. **Calibration accuracy.** Back-check how accurate the amps readings really
   are, so the accuracy figure can be justified (the figure previously quoted
   was 5%).
2. **WiFi noise floor.** Find out where the WiFi-related no-load noise enters
   (analog path vs. ADC/ESP32) and what changes it.

---

## 0. Which firmware to flash

`config.h` is gitignored (`.gitignore` line 7), so it is **not** part of any
commit: switching commits changes `src/*.cpp` but leaves your on-disk
`config.h` (WiFi credentials, `NO_LOAD_VOLTAGE`, `VOLTAGE_SAG_PER_AMP`)
exactly as it is.

| Commit | Date | WiFi | Serial output | Noise floor | Notes |
|---|---|---|---|---|---|
| `8532fc3` and earlier (`8651654`, `920eec3`, `41ede19`) | 2026-08/09 | off | raw ADC RMS (counts + volts) only, **no amps** | none | the original "calibration build"; 10s window; heartbeat LED (added in `920eec3`, so not in `41ede19`) |
| `58c580f` | 2026-09-13 | off | amps only (1s, 10s) | unamp <= 7 | first calibration math; no raw RMS printed; heartbeat LED |
| `f670a0d` | 2026-09-13 | off | amps + voltage + power | unamp <= 7 | adds placeholder 240V/1V-per-A model; no raw RMS |
| **`ba1514c`** | 2026-09-13 | **off** | **raw unamp/amp RMS + amps + voltage + power (1s and 10s)** | amp <= 20 | **last commit with WiFi off; best choice for accuracy testing** |
| `6320881` | 2026-09-13 | **on** | same, 5s window | amp <= 20 | WiFi/MQTT added |
| `59aab76` (`main`) | 2026-09-14 | on | same | amp <= 200 | noise floor raised for WiFi noise |

Why `ba1514c`: it has the full current pipeline, prints the raw RMS values
next to the calculated amps (needed to back-calibrate), and its noise floor
(20) is the quiet-bench value — appropriate with WiFi off. The 200-count floor
on `main` would force any load under ~70W to read 0 and hide low-current
errors.

**There is no "120V mode" commit.** The firmware never had a separate 120V
build. The only voltage-dependent part is the power estimate
(`NO_LOAD_VOLTAGE`, `VOLTAGE_SAG_PER_AMP` in `config.h`), which is tuned for
the 240V AC circuit (246.0V, 0.3067 V/A). **Amps do not depend on those
constants**, so an amps accuracy test at a 120V outlet works on any commit.
If you want to compare *watts* against a Kill-A-Watt at 120V, set the two
constants for that circuit first, otherwise the watts will be wrong even when
the amps are right.

To switch (not run — shown for reference):

```
git status                      # confirm nothing uncommitted you care about
git switch --detach ba1514c     # detached HEAD at the WiFi-off commit
# build + flash (pio run --target upload)
git switch main                 # come back afterward
```

---

## 1. Calibration accuracy

**Done (2026-11-03):** 20 new WiFi-off measurements were taken, including 11
between 1.07A and 5.98A (`callibration/measurments_2026-11.csv`), plus 7 loads
read with WiFi on (`callibration/wifi_on_observations_2026-11.csv`). They
showed the firmware's current equations read 16–26% low between 2.8A and 6A
(and the original data agrees with the new data where they overlap). A new
model that keeps both channels was fit from them. Results and analysis: `callibration/output/refit_results.txt` and
`callibration/CALIBRATION_METHODOLOGY.md` ("November 2026 re-fit").

An accuracy figure computed from the *September* data alone
(`callibration/accuracy_analysis.py`) does not cover 1–6.5A and shouldn't be
quoted as the overall accuracy.

**Still to do:**
- **Record the load type for every calibration point** (the `load_type`
  column in `measurments_2026-11.csv` is currently "unspecified").
- **Purely resistive loads between 1.5A and 6.5A** (in the 3–5A range
  especially), WiFi off, with their load type recorded. This is the test that shows whether the 2.8–6A discrepancy
  follows the load type or the current level.
- **A load near 2A.** There are still no points between 1.48A and 2.77A,
  which is exactly where the amplified channel stops being linear, so the
  amplified/unamplified fade zone (about 1.5–2.1A) is unvalidated.
- A **held-out set** (loads not used in fitting) once a new calibration is
  chosen, so the accuracy figure comes from data the fit hasn't seen. Compare
  **amps only** — the power estimate is tuned for the 240V circuit. Use WiFi
  off (firmware `ba1514c`, see above).

WiFi-on testing (sections 2-3 below) is about noise, not accuracy.

## 2. Localizing the WiFi noise (scope)

**Done (2026-11-03):** first scope captures with WiFi on vs. off — the 5V
line dips to about 4.8–4.9V in spikes ~5 times a second, and both signal
lines show large spikes at the same rate (amplified line 3.1V peak-to-peak,
vs. a clean 60 Hz waveform with WiFi off). Numbers are in
`firmware_state_adc_calibration.md` ("Scope captures"). Not yet done: probing
the Vref node, the 3.3V rail, and the earlier stages (input tap, integrator
output), the substitution test below, and checking the router's DTIM setting
(a possible source of the ~5 Hz rate). The saved captures (`scope captures/`)
have only been summarized; analyze them in more depth later if needed.

Goal: find whether the noise is present in the analog signal path, or only in
the ADC readings.

**Probe points** (AC-coupled, short ground spring, same trigger/timebase each
time): input divider tap; level-shifted node; integrator output; recentered
node; unity-buffer output; amplified output; each ADC pin (after the 2/3
divider); the Vref (2.5V) node; the 5V and 3.3V rails at both the op-amps and
the ESP32.

**Conditions to compare at each point** (no load connected): WiFi off
(`WiFi.mode(WIFI_OFF)`), WiFi connected and idle, and a trigger on the MQTT
publish (every 5s) to see the transmit burst.

How to read the results:
- Noise at the ADC pin but **not** earlier in the chain → ADC/ESP32-side.
- Noise on Vref or the supply rails, correlated with WiFi activity → supply
  coupling.
- Noise at the input tap / CT wiring but not on the rails → pickup into the
  analog front-end.
- Check whether any noise is periodic (e.g. repeating at roughly the router's
  beacon interval, ~100 ms) or lines up with the 5s MQTT publish.

**Substitution test** (separates the ADC from the analog front-end): with WiFi
on, disconnect the two ADC inputs from the analog board and feed them from a
clean battery-powered divider at ~1.65V. If the reported RMS noise stays,
it is ADC/ESP32-internal; if it disappears, the analog path (or what's
coupling into it) is the source.

Compare the **low-signal and high-signal lines**: earlier recollection is that
they showed a similar amount of noise. Confirm that with the scope and with
logged RMS values rather than relying on memory.

## 3. WiFi variables

Each of these can be tested by a small firmware change, one variable at a time:

- **Distance from the router**: the floor rose to ~150-200W when the device
  was moved farther away. Repeat at both locations and record the readings.
- **Hotspot next to the device**: set up a hotspot right beside the device
  and see whether the floor drops. Keep in mind the ESP32's transmit power is a
  configured setting (not automatically reduced when the access point is
  closer), so a nearby hotspot mostly changes link quality — data rate,
  retries, time on air — rather than peak transmit power. To isolate peak
  power, also try `WiFi.setTxPower(...)` at a lower setting.
- **Modem sleep** (`WiFi.setSleep(true/false)`), and WiFi connected-but-idle vs.
  connected-and-publishing.

## 4. Power-supply isolation — done

Different power supplies were tried and made no difference to the WiFi noise.
(Background and candidate fixes: `firmware_state_adc_calibration.md`,
"Noise floor / WiFi interference".)

## 5. Analog circuit checks still open

From `ac_current_monitor_handoff_v2.md`:

- Multimeter: both lines average 2.50V at the analog stage (~1.65V at the ADC
  pins).
- Scope the lines against the raw integrator output at 60 Hz to confirm the
  waveform shape survives the recentering network.

## 6. Interview-prep checklist

- Every number you plan to quote is traceable to a data file or a doc section.
- The accuracy claim states its range, sample size, that it was computed on the
  calibration data (in-sample and leave-one-out), and the reference meters'
  disagreement.
- You can walk through each analog stage and say what it does and why — see
  `ac_current_monitor_handoff_v2.md`.
- You can say honestly what is **not** known: the WiFi noise mechanism, and
  accuracy in the ~1-6.7A range until the mid-range spot check is done.
