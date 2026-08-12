## Project structure

espfryer/
 - espfryer.ino   – main sketch & state machine
 - config.h       – all pin definitions and tuneable constants
 - temperature.h  – NTC → °C conversion (Beta equation)
 - control.h      – heater control: predictive cutoff + duty modulation
 - display.h      – LCD screen functions for each state

## Pinout

| Function | GPIO | Alias | Notes |
|---|---|---|---|
| Button UP | 12 | D6 | to GND, `INPUT_PULLUP`, active LOW |
| Button DOWN | 13 | D7 | to GND, `INPUT_PULLUP`, active LOW |
| Button SELECT | 14 | D5 | to GND, `INPUT_PULLUP`, active LOW |
| Relay HEAT | 15 | D8 | **active HIGH** |
| Relay FAN | 16 | D0 | **active HIGH** |
| LCD SDA | 4 | D2 | I2C |
| LCD SCL | 5 | D1 | I2C |
| NTC | A0 | A0 | 3.3V → NTC → A0 → 10kΩ → GND |

Buttons sit on GPIO12/13/14 — the only pins with no boot-strapping role — so a
press during reset can't stop the board booting. The relays take GPIO15/16
precisely *because* those pins are held LOW through reset, brownout and crash:
with active-HIGH drive, LOW means the heating element is off.

## State machine flow

    SET_TEMP → SET_HOURS → SET_MINS → PREHEAT → RUNNING → DONE
        ▲                       │        │         │        │
        │                       │        │ SEL     │ SEL    │ SEL
        └───────────────────────┼────────┴─────────┴────────┘
        │                       │        │
        │                       ▼        ▼
        └──────── SEL ────── [ ERROR ] ◄─┘   sensor reads < TEMP_FAULT_MIN_C

PREHEAT advances to RUNNING on its own once the measured temperature reaches
`target − TEMP_HYSTERESIS`; the cook countdown starts at that moment, so
preheating does not eat into cook time. RUNNING advances to DONE when the
countdown expires. Both drop the relays on the way out.

| State | UP/DOWN | SELECT |
|---|---|---|
| SET_TEMP | ±5 °C | advance to SET_HOURS |
| SET_HOURS | ±1 h, 0–`TIME_MAX_HOURS` | advance to SET_MINS |
| SET_MINS | variable step, see below | start preheat (or → ERROR) |
| PREHEAT | – | abort → SET_TEMP |
| RUNNING | – | abort → SET_TEMP |
| DONE | – | back to SET_TEMP |
| ERROR | – | acknowledge → SET_TEMP |

### Heater control

Plain hysteresis does not work on this machine. The element holds far more heat
than the chamber air, so cutting power the instant the setpoint is reached still
dumps the element's stored energy into the chamber afterwards — a 50 °C setpoint
overshot to 70 °C. Hysteresis has no anticipation, so tightening
`TEMP_HYSTERESIS` does nothing about it.

`control.h` replaces it with two mechanisms:

**Predictive cutoff.** The switching decision runs on a projected temperature —
the current reading extrapolated `PREDICT_LEAD_S` seconds along the measured rate
of rise — rather than on the reading itself. The element goes off while still
climbing and coasts into the setpoint. Because the projection uses the *measured*
rate, it adapts to load on its own: a full basket heats slower, so the cutoff
comes later.

**Duty taper.** Power is not binary. Above `target − APPROACH_BAND_C` (projected)
the duty falls linearly from 100% to `DUTY_HOLD`, so there is less stored energy
left in the element to coast on in the first place. Throttling lowers the rate of
rise, which shortens the projection and lets the duty back up — the loop settles
itself. Together the two are a PD controller with the derivative gain expressed
as a lead time.

Rate of rise is measured across `DERIV_WINDOW_MS` (5 s), not between adjacent
samples: one 500 ms interval on the ESP8266 ADC is all noise, and with a 45 s
lead a 0.1 °C/s error in the rate becomes 4.5 °C of error in the projection.

**Relay modulation.** The duty demand drives a mechanical relay, held at least
`RELAY_MIN_ON_MS` / `RELAY_MIN_OFF_MS` per switch. Rather than truncate pulses
too short to be worth firing, the modulator banks on-time owed and stretches the
period: a 5% demand comes out as ~2.5 s on / ~50 s off, a 50% demand as ~6 s on /
~6 s off. Average duty is honoured at any demand. The cost is up to
`RELAY_MIN_ON_MS` of latency on a cutoff, which the predictive lead absorbs.

`TEMP_HYSTERESIS` survives as the hard backstop: if the *measured* temperature
ever exceeds `target + TEMP_HYSTERESIS`, heat is cut immediately and the minimum
on-time is ignored — safety outranks contact wear.

#### Tuning

Watch the serial log (115200) through a preheat; it prints the measured rate, the
projection and the demanded duty every sample:

    Temp: 41.5 / 50 C  rate:+0.38 C/s  proj:58.6  duty: 15%  Heat:0 Fan:1

Then, in order:

| Symptom | Change |
|---|---|
| Still overshoots | raise `PREDICT_LEAD_S` |
| Stalls short of the setpoint, or preheat crawls | lower `PREDICT_LEAD_S` |
| Reaches the setpoint but then creeps upward | lower `DUTY_HOLD` |
| Sags a few °C below the setpoint and stays there | raise `DUTY_HOLD` |
| Rate reading is visibly jumpy | raise `DERIV_WINDOW_MS`, or lower `DERIV_SMOOTH_ALPHA` |

Calibrate the thermistor before tuning any of this — a control loop cannot be
tuned against a wrong measurement. Check `NTC_R0` and `NTC_BETA` against the
actual part (`NTC_R0` is currently set for a 100 kΩ thermistor) and verify the
reading in ice water and boiling water.

### Sensor fault detection

A disconnected or broken NTC lead reads as raw ADC 0, which `temperature.h`
reports as 0.0 °C. The thermostat would read that as "freezing, heat harder" and
hold the element on indefinitely — so any reading below `TEMP_FAULT_MIN_C`
(default 10 °C) is treated as a broken sensor rather than a cold fryer.

It is checked in two places:

- **Before starting**, on the SET_MINS → PREHEAT transition, so a dead sensor
  never energises anything in the first place.
- **On every sample** during PREHEAT and RUNNING, which cuts both relays and
  latches into ERROR.

ERROR shows the offending reading on the LCD (`SENSOR FAULT!` / `reads 0.0C`)
so a stuck 0.0 from an open lead can be told apart from a plausible-but-cold
value, re-asserts `relaysOff()` on every pass rather than only on entry, and
holds until SEL acknowledges it. If the sensor is still faulty, the next attempt
to start trips it again immediately.

Note the high side needs no equivalent check: an open ADC input reads full
scale, `readTemperatureC()` returns 999.0 °C, and the thermostat already turns
the heater off. Only the cold direction is dangerous.

### Minute step ladder

Cook time is entered as hours first, then minutes. The minute step depends on
what is already on the clock:

| Condition | Step | Range |
|---|---|---|
| hours ≥ 1 | 15 min | 0 – 45 |
| hours = 0, minutes < 10 | 1 min | 1 – 10 |
| hours = 0, minutes 10–29 | 5 min | 10 – 30 |
| hours = 0, minutes ≥ 30 | 10 min | 30 – `TIME_MINS_MAX` (50) |

So with no hours set the ladder runs 1, 2 … 10, 15, 20, 25, 30, 40, 50; with an
hour or more it is just 0, 15, 30, 45. Stepping down uses the step of the band
being entered, so up-then-down always returns to the value you started from.

Minutes cannot reach 0 unless at least one hour is set — that would make the
total cook time zero. All the bands and limits are in `config.h`.

## Before uploading
Required library (Library Manager): LiquidCrystal_I2C by Frank de Brabander.

Check/adjust in config.h:

LCD_I2C_ADDR — try 0x3F if the screen stays blank
NTC_BETA — get this from your specific thermistor datasheet (common values: 3435, 3950)
NTC_SERIES_R — match your actual series resistor value
Wiring: NTC divider is 3.3V → NTC → A0 → 10kΩ → GND (ADC reads voltage across the series resistor)

RELAY_ON / RELAY_OFF — set to HIGH / LOW, i.e. active-HIGH drive. This is not
free choice: GPIO15's 10kΩ pulldown and GPIO16's LOW default are what hold the
heating element off through reset, brownout and crash, and that only means "off"
while RELAY_ON is HIGH. Inverting these two defines would make GPIO15's pulldown
hold the heater **on** through every reset. If your hardware needs active-LOW
drive, invert it outside the ESP — a single NPN inverter per channel, or an
optocoupler — and leave these defines alone.

## Relay interface

The fryer's own board exposes GND, 5V, and one control pin per relay. Those pins
are buffered inputs, not raw coils: each measures ~9.4kΩ to 5V, so pulling one to
ground needs only ~0.5mA. Each is driven by a PC817 optocoupler:

    GPIO ──[470Ω]──► pin 1 (anode)      pin 4 (collector) ──► fryer relay pin
                     pin 2 (cathode) ── ESP GND
                                        pin 3 (emitter)   ──► fryer GND

Keep the two grounds separate — that isolation is the point of the optocoupler,
and it also keeps relay switching noise off the ground the NTC divider
references. Confirm the fryer's 5V rail is isolated from mains before wiring
anything to it.


# load
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"; FQBN="esp8266:esp8266:generic:xtal=80,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=dout,eesz=4M1M,baud=115200"; "$CLI" compile --fqbn "$FQBN" /Users/javierf/Proyects/espfryer 2>&1 | tail -15 && echo "=== UPLOADING ===" && "$CLI" upload -p /dev/cu.usbserial-A5069RR4 --fqbn "$FQBN" /Users/javierf/Proyects/espfryer 2>&1 | tail -12