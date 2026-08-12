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
countdown expires — cutting the element but *not* the fan, which runs on through
the cooldown described below. Every other exit from an active state, abort or
sensor fault, drops both relays.

| State | UP/DOWN | SELECT |
|---|---|---|
| SET_TEMP | ±5 °C | advance to SET_HOURS |
| SET_HOURS | ±1 h, 0–`TIME_MAX_HOURS` | advance to SET_MINS |
| SET_MINS | variable step, see below | start preheat (or → ERROR) |
| PREHEAT | – | abort → SET_TEMP |
| RUNNING | – | abort → SET_TEMP |
| DONE | – | end cooldown, back to SET_TEMP |
| ERROR | – | acknowledge → SET_TEMP |

### Fan cooldown

The element holds far more heat than the air around it — the same thermal inertia
the predictive controller exists to fight. Cutting both relays the instant the
countdown expires leaves that stored heat to soak into the chamber and the
basket, so RUNNING → DONE cuts only the element and lets the fan run on for
`COOLDOWN_MS` (default 90 s) to carry it out.

`heaterOff()` exists for exactly this transition: it drops the element and
resynchronises the controller while leaving the fan untouched. `relaysOff()` is
that plus the fan, and is what every other exit uses.

DONE does not sample the NTC, so the cooldown is a fixed duration rather than a
wait for a temperature — it is not a safety interlock and nothing depends on it
finishing. Pressing SEL ends it immediately and returns to SET_TEMP with both
relays off. `fanOn` doubles as the "still cooling" flag, so once it clears, the
state stops doing any work.

### Display screens

One screen per state, all in `display.h`, all 16×2. Every line goes through
`padRight()` to the full 16 columns, so a screen always overwrites the one
before it — no `lcd.clear()` and no leftover characters from a longer previous
line. Values below are the power-on defaults (`TEMP_DEFAULT` 100 °C,
`TIME_DEFAULT_HOURS` 0, `TIME_DEFAULT_MINS` 30).

| State | Function | Redrawn |
|---|---|---|
| SET_TEMP | `displaySetTemp()` | on each UP/DOWN press |
| SET_HOURS | `displaySetHours()` | on each UP/DOWN press |
| SET_MINS | `displaySetMins()` | on each UP/DOWN press |
| PREHEAT | `displayPreheat()` | every `DISPLAY_REFRESH_MS` |
| RUNNING | `displayRunning()` | every `DISPLAY_REFRESH_MS` |
| DONE | `displayDone()` | every `DISPLAY_REFRESH_MS` while cooling, then once more |
| ERROR | `displayError()` | once, on entry |

The three setup screens redraw only on a keypress rather than on a timer —
there is nothing on them that changes on its own. PREHEAT and RUNNING carry a
live temperature, so they are on the refresh timer instead; note that the timer
only controls when the LCD is *repainted*, while the value itself is resampled
every `TEMP_SAMPLE_MS`.

    SET_TEMP                SET_HOURS               SET_MINS
    ┌────────────────┐      ┌────────────────┐      ┌────────────────┐
    │Set Temperature:│      │Set Hrs (100C): │      │Set Min (100C): │
    │  100 C  [^v OK]│      │  0 h    [^v OK]│      │  0h 30m [^v OK]│
    └────────────────┘      └────────────────┘      └────────────────┘

The target temperature stays in the header of both time screens, and SET_MINS
shows hours and minutes together, so the full setting is always visible while
you edit the last field of it.

    PREHEAT                 RUNNING                 DONE (cooling)
    ┌────────────────┐      ┌────────────────┐      ┌────────────────┐
    │Preheating...   │      │118C  TRGT:120C │      │   DONE!  :)    │
    │  52C -> 100C   │      │02:45  [H] [F]  │      │Cooling 45s [F] │
    └────────────────┘      └────────────────┘      └────────────────┘

DONE has two forms, selected by the `coolSecs` argument to `displayDone()`. The
one above counts the cooldown down; once the fan stops, line 1 becomes the
prompt. Without that split the screen would announce the cook was over while the
fan was audibly still going, which reads as a fault:

    DONE (finished)
    ┌────────────────┐
    │   DONE!  :)    │
    │ Press SEL again│
    └────────────────┘

`[H]` and `[F]` mirror the heat and fan relays, each blanked to three spaces
when its relay is off — so `[H]` blinking in and out through a cook is the duty
modulation in `control.h` made visible. The fan runs for the whole cook, so
`[F]` is steady; `[H]` cycling on a period of tens of seconds at low duty is
normal and is the relay modulator stretching its period, not a fault.

The countdown drops the hours field once it is no longer needed, so a short cook
shows `02:45` rather than a permanent leading `0:`. With an hour or more left it
becomes `1:30:00`, which fills the line exactly.

    ERROR
    ┌────────────────┐
    │SENSOR FAULT!   │
    │reads 0.0C      │
    └────────────────┘

ERROR prints the offending reading rather than just the fault, so a stuck `0.0C`
from an open lead can be told apart from a plausible-but-too-cold value. See
[Sensor fault detection](#sensor-fault-detection).

Only the target is labelled on RUNNING line 0; the bare number is the current
temperature. Labelling both ran the line to 17 characters, one past the 16 the
LCD can show, and the overflow fell on the target's `C` — so the label was
dropped from the value that needs it least.

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
tuned against a wrong measurement.

#### Thermistor calibration

Do not trust the part's markings. The thermistor in this build was sold as a
100 kΩ/3950 and measured 120 kΩ with a beta of 4212 — a 20 kΩ and 260-unit
error, far outside any real tolerance. Both constants come out of two
measurements instead.

Measure the thermistor **out of circuit**, on a meter, at two temperatures that
straddle the range you actually cook in:

1. Room temperature. Record the resistance *and a real thermometer reading* —
   the anchor is whatever the room actually is, not an assumed 25 °C.
2. Boiling water. Record the resistance. The temperature is set by your
   altitude, not by the pot: 100 °C only at sea level, about 95 °C at 1500 m.
   Use the altitude figure rather than a thermometer, since water boils at that
   temperature by definition and a thermometer only adds its own error.

Then solve for beta and fill in all three constants together:

    B = ln(R1/R2) / (1/T1_K - 1/T2_K)          T in kelvin

    NTC_R0   = R1        resistance at the low point
    NTC_T0_K = T1 + 273.15
    NTC_BETA = B

`NTC_R0`, `NTC_T0_K` and `NTC_BETA` are one fitted triple, not three
independent knobs. Changing `NTC_R0` while leaving `NTC_T0_K` at the
conventional 298.15 silently skews every reading. If you re-measure, redo all
three. The current values are fitted from 120 kΩ at 24 °C and 7.8 kΩ at 95 °C,
and the curve passes exactly through both points.

Ice water is a poorer second point than boiling water here: 0 °C is far below
anything the fryer does, so it forces the fit to extrapolate across the whole
working range instead of interpolating within it.

A wrong curve and a badly placed probe look identical on the display but are not
the same fault, and no constant fixes the second one. To tell them apart,
compare the reading against a reference thermometer at steady state, not during
a climb — the sensor has real thermal mass and lags badly while the temperature
is still rising.

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

NTC_R0 / NTC_T0_K / NTC_BETA — do not copy these from a datasheet, and do not
change one without the other two. Measure your own part at two temperatures and
fit all three together; see [Thermistor calibration](#thermistor-calibration).
The values in `config.h` are fitted to one specific thermistor and one specific
altitude, and are not a sensible starting guess for a different build.

NTC_SERIES_R — match your actual series resistor value. 10 kΩ is deliberate and
worth keeping: the theoretical optimum for a 40–120 °C span is ~15 kΩ, which
buys 608 ADC counts instead of 593. At 10 kΩ the quantisation is 0.18 °C per
count across the whole range, well under the sensor noise floor.
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


## Build and load

    ./build.sh      # compile only
    ./flash.sh      # compile, then upload
    ./monitor.sh    # serial monitor

Paths, FQBN, port and monitor baud live in `build.conf` — the only file to edit
if the arduino-cli location or the board's serial port changes. The port can also
be overridden per-run, and if it is left empty the scripts pick the first
`/dev/cu.usbserial-*` device they find:

    PORT=/dev/cu.usbserial-XXXX ./flash.sh

Extra arguments are passed straight through to arduino-cli, e.g. `./build.sh -v`
for a verbose compile.