# AC power draw vs. outdoor temperature

Data: [`ac_power_vs_outdoor_temp.csv`](ac_power_vs_outdoor_temp.csv) — 15 points
(outdoor temperature in °F, AC power draw in watts) read off Home Assistant
graphs by eye, so each value is approximate and a few may be misread. This is a
quick look, not a rigorous study.

## Result

Power draw rises steadily with outdoor temperature.

| | outdoor temp | power draw |
|---|---|---|
| lowest reading | 51 °F | 1504 W |
| highest reading | 85 °F | 1911 W |

- Going from 51 °F to 85 °F, the draw increased by about **27%** (+407 W).
- A straight-line fit through all 15 points gives about **+12 W per °F**
  (roughly +120 W, or about 8%, per 10 °F), with R² = 0.87.
- The 66 °F / 1809 W point is the clearest outlier — about 117 W above the fit
  line, while every other point is within about ±75 W. Without it the fit
  tightens to R² = 0.94 and the slope is essentially unchanged (+12.2 W per °F).
- The ranking is consistent too: temperature and power are ordered almost the
  same way (rank correlation 0.95).

## Efficiency

The lowest-temperature reading (51 °F, 1504 W) is the lowest-power point, and
power draw climbs as it gets hotter, which fits with the AC becoming less
efficient at higher outdoor temperatures (the compressor works harder).

That is an interpretation, not a measurement: efficiency is cooling delivered
per watt, and cooling delivered was not recorded here. Power draw alone only
shows efficiency falling if the cooling output was roughly the same at each
point. An AC's cooling capacity generally also drops as outdoor temperature
rises, so if that holds here, the real efficiency drop would be larger than the
power increase alone suggests. Getting an actual efficiency figure would need
the cooling output (for example airflow and the temperature drop across the
coil) alongside the power.

## Caveats

- Values were read from graphs by eye; expect some error and possible misreads.
- If these power values come from this monitor's MQTT power topic, they are
  estimated voltage times measured current, not true power: the power factor is
  not measured (so it is closer to apparent power than real watts), and the
  voltage comes from a single-point sag model (see
  `../firmware_state_adc_calibration.md`). The temperature trend is likely
  still real, but the absolute watt values carry that uncertainty.
- Only 15 points, and outdoor temperature isn't the only thing that varies:
  indoor setpoint, humidity, time of day, and sun load all affect how hard the
  AC works.
