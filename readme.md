# ESPFryer

**A replacement controller for an air fryer, built on an ESP8266.**

ESPFryer swaps out a commercial air fryer's stock control panel for an ESP8266,
a 16×2 LCD and three buttons. It keeps the fryer's own heating element, fan and
relay board, and replaces the logic that drives them. You set a temperature and
a cook time. The controller preheats, holds the temperature, counts the cook
down, then runs the fan for a while to clear the leftover heat.

The main reason to build it is temperature accuracy. The heating element stores
a lot of heat, so a plain on/off thermostat overshoots badly: a 50 °C setpoint
climbed to 70 °C. ESPFryer uses a predictive controller that cuts power *before*
the setpoint and coasts into it. This makes it usable for low-temperature jobs
the stock fryer handles poorly, like dehydrating, proofing and slow cooks of up
to six hours.

> [!WARNING]
> This project modifies an appliance that runs on mains power and drives a
> heating element. Work on it unplugged. Confirm that any low-voltage rail you
> connect to is isolated from mains, and never leave a modified appliance
> running unattended until you trust it. You build and use it at your own risk.

## Features

- **Predictive temperature control.** A PD-style controller projects the
  temperature ahead along its measured rate of rise, then tapers the heater's
  duty cycle as it approaches the setpoint, so it arrives with little
  overshoot. See [Heater control](#heater-control).
- **Relay-friendly modulation.** Duty cycle is delivered through a mechanical
  relay with minimum on/off times, so the contacts don't chatter.
- **Fail-safe by design:**
  - The relays sit on GPIO pins that are held LOW through reset, brownout and
    crash, and LOW means the heater is off.
  - A broken or disconnected temperature sensor is detected and cuts the
    heater, both before a cook starts and on every sample during it.
  - Preheat gives up with an error if the setpoint isn't reached within 10
    minutes, rather than demanding heat forever.
  - A hard over-temperature cutoff backs up the controller.
- **Preheat that doesn't eat cook time.** The countdown starts only once the
  setpoint is reached.
- **Fan cooldown** after each cook to carry the element's stored heat out of
  the chamber.
- **Simple three-button interface**, 40–200 °C in 5 °C steps and up to 6 hours
  of cook time, with a live status screen.
- **Serial telemetry** (temperature, rate, projection, duty) for tuning.

## Contents

- [Hardware](#hardware)
- [Getting started](#getting-started)
- [Using it](#using-it)
- [How it works](#how-it-works)
- [Project structure](#project-structure)

---

## Hardware

### What you need

| Part | Notes |
|---|---|
| ESP8266 module | Any 4 MB ESP-12E/F board (NodeMCU, Wemos D1 mini, bare module) |
| 16×2 character LCD with I2C backpack | PCF8574 backpack, address 0x27 or 0x3F |
| 3 momentary push buttons | UP, DOWN, SELECT |
| NTC thermistor | ~100 kΩ class; you will [calibrate it](#thermistor-calibration) anyway |
| 10 kΩ resistor | NTC divider series resistor |
| 2 × PC817 optocoupler + 2 × 470 Ω resistor | One per relay channel, see [Relay interface](#relay-interface) |
| An air fryer | Its relay board must expose a low-voltage control input per relay |

The design assumes the fryer's own board has a relay for the heater and one for
the fan, driven by low-voltage logic inputs. If your fryer switches mains
directly from its control panel, you will need your own relay module and a
different interface.

### Pinout

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

The buttons are on GPIO12/13/14 because those are the only pins with no
boot-strapping role, so a press during reset can't stop the board booting. The
relays are on GPIO15/16 *because* those pins are held LOW through reset,
brownout and crash. With active-HIGH drive, LOW means the heating element is
off.

### Relay interface

The fryer's own board exposes GND, 5V, and one control pin per relay. Those pins
are buffered inputs, not raw coils: each measures ~9.4kΩ to 5V, so pulling one to
ground needs only ~0.5mA. Each is driven by a PC817 optocoupler:

    GPIO ──[470Ω]──► pin 1 (anode)      pin 4 (collector) ──► fryer relay pin
                     pin 2 (cathode) ── ESP GND
                                        pin 3 (emitter)   ──► fryer GND

Keep the two grounds separate. That isolation is the point of the optocoupler,
and it also keeps relay switching noise off the ground the NTC divider
references. Confirm the fryer's 5V rail is isolated from mains before wiring
anything to it.

---

## Getting started

### 1. Install PlatformIO

This is a [PlatformIO](https://platformio.org/) project. Install the CLI
(`brew install platformio`, or the PlatformIO IDE extension for VS Code). The
only external library, LiquidCrystal_I2C by Frank de Brabander, is installed
automatically on the first build from `lib_deps` in `platformio.ini`.

### 2. Adjust `config.h` for your build

**`LCD_I2C_ADDR`**: try `0x3F` if the screen stays blank.

**`NTC_R0` / `NTC_T0_K` / `NTC_BETA`**: do not copy these from a datasheet, and
do not change one without the other two. Measure your own part at two
temperatures and fit all three together; see
[Thermistor calibration](#thermistor-calibration). The values in `config.h` are
fitted to one specific thermistor at one specific altitude. They are not a
sensible starting guess for a different build.

**`NTC_SERIES_R`**: match your actual series resistor value. 10 kΩ is
deliberate and worth keeping. The theoretical optimum for a 40–120 °C span is
~15 kΩ, which buys 608 ADC counts instead of 593. At 10 kΩ the quantisation is
0.18 °C per count across the whole range, well under the sensor noise floor.
The divider is wired 3.3V → NTC → A0 → 10kΩ → GND, so the ADC reads the voltage
across the series resistor.

**`RELAY_ON` / `RELAY_OFF`**: set to HIGH / LOW, i.e. active-HIGH drive. This is
not a free choice. GPIO15's 10kΩ pulldown and GPIO16's LOW default are what hold
the heating element off through reset, brownout and crash, and that only means
"off" while `RELAY_ON` is HIGH. Inverting these two defines would make GPIO15's
pulldown hold the heater **on** through every reset. If your hardware needs
active-LOW drive, invert it outside the ESP (a single NPN inverter per channel,
or an optocoupler) and leave these defines alone.

### 3. Build and flash

    pio run              # compile only
    pio run -t upload    # compile, then upload
    pio device monitor   # serial monitor

Add `-v` for a verbose compile.

Board, flash layout, port and monitor baud live in `platformio.ini`, the only
file to edit if the board or its serial port changes. It pins the settings the
project has always been built with: 80 MHz CPU, 40 MHz dout flash, 4M1M layout,
nodemcu reset method. The `upload_port`/`monitor_port` lines are set to the
author's USB adapter. Comment them out to let PlatformIO auto-detect your board,
or override the port for one run:

    pio run -t upload --upload-port /dev/cu.usbserial-XXXX

### 4. Calibrate and tune

Calibrate the thermistor first ([Thermistor calibration](#thermistor-calibration)),
then watch a preheat on the serial monitor and adjust the controller
([Tuning](#tuning)).

---

## Using it

1. **Set the temperature** with UP/DOWN (5 °C steps, 40–200 °C), then press
   SELECT.
2. **Set the hours** (0–6), then press SELECT.
3. **Set the minutes**, then press SELECT to start.
4. The fryer **preheats**. The countdown starts once it reaches temperature.
5. When the time is up, the heater stops and the fan **cools down** for a
   minute. Press SELECT to finish.

SELECT during preheat or cooking aborts and turns everything off. If something
goes wrong, the screen shows an error and a reading. Press SELECT to
acknowledge it.

### State machine

    SET_TEMP → SET_HOURS → SET_MINS → PREHEAT → RUNNING → DONE
        ▲                       │        │         │        │
        │                       │        │ SEL     │ SEL    │ SEL
        └───────────────────────┼────────┴─────────┴────────┘
        │                       │        │
        │                       ▼        ▼
        └──────── SEL ────── [ ERROR ] ◄─┘   sensor reads < TEMP_FAULT_MIN_C,
                                             or preheat exceeds
                                             PREHEAT_TIMEOUT_MS

PREHEAT advances to RUNNING on its own once the measured temperature reaches
`target − TEMP_HYSTERESIS`. The cook countdown starts at that moment, so
preheating does not eat into cook time. RUNNING advances to DONE when the
countdown expires. That cuts the element but *not* the fan, which runs on
through the cooldown described below. Every other exit from an active state,
abort or fault, drops both relays.

PREHEAT is also the one state with a deadline on it: if the setpoint is not
reached within `PREHEAT_TIMEOUT_MS`, it gives up into ERROR rather than
demanding heat indefinitely. See [Preheat timeout](#preheat-timeout).

| State | UP/DOWN | SELECT |
|---|---|---|
| SET_TEMP | ±5 °C | advance to SET_HOURS |
| SET_HOURS | ±1 h, 0–`TIME_MAX_HOURS` | advance to SET_MINS |
| SET_MINS | variable step, see below | start preheat (or → ERROR) |
| PREHEAT | – | abort → SET_TEMP |
| RUNNING | – | abort → SET_TEMP |
| DONE | – | end cooldown, back to SET_TEMP |
| ERROR | – | acknowledge → SET_TEMP |

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

Minutes cannot reach 0 unless at least one hour is set, because that would make
the total cook time zero. All the bands and limits are in `config.h`.

### Display screens

There is one screen per state, all in `display.h`, all 16×2. Every line goes
through `padRight()` to the full 16 columns, so a screen always overwrites the
one before it, with no `lcd.clear()` and no leftover characters from a longer
previous line. Values below are the power-on defaults (`TEMP_DEFAULT` 80 °C,
`TIME_DEFAULT_HOURS` 0, `TIME_DEFAULT_MINS` 30).

| State | Function | Redrawn |
|---|---|---|
| SET_TEMP | `displaySetTemp()` | on each UP/DOWN press |
| SET_HOURS | `displaySetHours()` | on each UP/DOWN press |
| SET_MINS | `displaySetMins()` | on each UP/DOWN press |
| PREHEAT | `displayPreheat()` | every `DISPLAY_REFRESH_MS` |
| RUNNING | `displayRunning()` | every `DISPLAY_REFRESH_MS` |
| DONE | `displayDone()` | every `DISPLAY_REFRESH_MS` while cooling, then once more |
| ERROR | `displayError()` / `displayTimeout()` | once, by whichever check latched it |

The three setup screens redraw only on a keypress rather than on a timer, since
nothing on them changes on its own. PREHEAT and RUNNING carry a live
temperature, so they are on the refresh timer instead. The timer only controls
when the LCD is *repainted*; the value itself is resampled every
`TEMP_SAMPLE_MS`.

    SET_TEMP                SET_HOURS               SET_MINS
    ┌────────────────┐      ┌────────────────┐      ┌────────────────┐
    │Set Temperature:│      │Set Hrs (80C):  │      │Set Min (80C):  │
    │  80 C  [^v OK] │      │  0 h    [^v OK]│      │  0h 30m [^v OK]│
    └────────────────┘      └────────────────┘      └────────────────┘

The target temperature stays in the header of both time screens, and SET_MINS
shows hours and minutes together, so the full setting is always visible while
you edit the last field of it.

    PREHEAT                 RUNNING                 DONE (cooling)
    ┌────────────────┐      ┌────────────────┐      ┌────────────────┐
    │Preheating...   │      │ 78C  TRGT: 80C │      │   DONE!  :)    │
    │  52C ->  80C   │      │02:45  [H] [F]  │      │Cooling 45s [F] │
    └────────────────┘      └────────────────┘      └────────────────┘

DONE has two forms, selected by the `coolSecs` argument to `displayDone()`. The
one above counts the cooldown down; once the fan stops, line 1 becomes the
prompt. Without that split, the screen would announce the cook was over while
the fan was audibly still going, which reads as a fault:

    DONE (finished)
    ┌────────────────┐
    │   DONE!  :)    │
    │ Press SEL again│
    └────────────────┘

`[H]` and `[F]` mirror the heat and fan relays, each blanked to three spaces
when its relay is off. So `[H]` blinking in and out through a cook is the duty
modulation in `control.h` made visible. The fan runs for the whole cook, so
`[F]` is steady. `[H]` cycling on a period of tens of seconds at low duty is
normal: that is the relay modulator stretching its period, not a fault.

The countdown drops the hours field once it is no longer needed, so a short cook
shows `02:45` rather than a permanent leading `0:`. With an hour or more left it
becomes `1:30:00`, which fills the line exactly.

ERROR is shared by both faults, so its screen has to name the cause. Neither is
ever repainted. Whichever check latched the fault paints it once, and it stands
until SEL clears it:

    ERROR (sensor)          ERROR (timeout)
    ┌────────────────┐      ┌────────────────┐
    │SENSOR FAULT!   │      │PREHEAT TIMEOUT!│
    │reads 0.0C      │      │  78C of 120C   │
    └────────────────┘      └────────────────┘

Both spend line 1 on a measurement rather than on advice, because that is what
tells the failure modes apart. A stuck `0.0C` is an open lead, while a
plausible-but-too-cold value is a real reading. On a timeout, a temperature
close to target suggests an underpowered element or a lid left open, while one
barely above ambient suggests a dead element or a relay that never closed. See
[Sensor fault detection](#sensor-fault-detection) and
[Preheat timeout](#preheat-timeout).

Only the target is labelled on RUNNING line 0; the bare number is the current
temperature. Labelling both ran the line to 17 characters, one past the 16 the
LCD can show, and the overflow fell on the target's `C`. So the label was
dropped from the value that needs it least.

---

## How it works

### Heater control

Plain hysteresis does not work on this machine. The element holds far more heat
than the chamber air, so cutting power the instant the setpoint is reached still
dumps the element's stored energy into the chamber afterwards. A 50 °C setpoint
overshot to 70 °C. Hysteresis has no anticipation, so tightening
`TEMP_HYSTERESIS` does nothing about it.

`control.h` replaces it with two mechanisms:

**Predictive cutoff.** The switching decision runs on a projected temperature
(the current reading extrapolated `PREDICT_LEAD_S` seconds along the measured
rate of rise) rather than on the reading itself. The element goes off while
still climbing and coasts into the setpoint. Because the projection uses the
*measured* rate, it adapts to load on its own: a full basket heats slower, so
the cutoff comes later.

**Duty taper.** Power is not binary. Above `target − APPROACH_BAND_C`
(projected), the duty falls linearly from 100% to `DUTY_HOLD`, so there is less
stored energy left in the element to coast on in the first place. Throttling
lowers the rate of rise, which shortens the projection and lets the duty back
up, so the loop settles itself. Together the two are a PD controller with the
derivative gain expressed as a lead time.

Rate of rise is measured across `DERIV_WINDOW_MS` (5 s), not between adjacent
samples. One 500 ms interval on the ESP8266 ADC is all noise, and with a 45 s
lead, a 0.1 °C/s error in the rate becomes 4.5 °C of error in the projection.

**Relay modulation.** The duty demand drives a mechanical relay, held at least
`RELAY_MIN_ON_MS` / `RELAY_MIN_OFF_MS` per switch. Rather than truncate pulses
too short to be worth firing, the modulator banks the on-time owed and stretches
the period: a 5% demand comes out as ~2.5 s on / ~50 s off, a 50% demand as
~6 s on / ~6 s off. Average duty is honoured at any demand. The cost is up to
`RELAY_MIN_ON_MS` of latency on a cutoff, which the predictive lead absorbs.

`TEMP_HYSTERESIS` survives as the hard backstop. If the *measured* temperature
ever exceeds `target + TEMP_HYSTERESIS`, heat is cut immediately and the minimum
on-time is ignored. Safety outranks contact wear.

#### Tuning

Watch the serial log (115200 baud) through a preheat. It prints the measured
rate, the projection and the demanded duty every sample:

    Temp: 41.5 / 50 C  rate:+0.38 C/s  proj:58.6  duty: 15%  Heat:0 Fan:1

Then work through these in order:

| Symptom | Change |
|---|---|
| Still overshoots | raise `PREDICT_LEAD_S` |
| Stalls short of the setpoint, or preheat crawls | lower `PREDICT_LEAD_S` |
| Reaches the setpoint but then creeps upward | lower `DUTY_HOLD` |
| Sags a few °C below the setpoint and stays there | raise `DUTY_HOLD` |
| Rate reading is visibly jumpy | raise `DERIV_WINDOW_MS`, or lower `DERIV_SMOOTH_ALPHA` |

Calibrate the thermistor before tuning any of this. A control loop cannot be
tuned against a wrong measurement.

#### Thermistor calibration

Do not trust the part's markings. The thermistor in this build was sold as a
100 kΩ/3950 and measured 120 kΩ with a beta of 4212, a 20 kΩ and 260-unit
error, far outside any real tolerance. Both constants come from two
measurements instead.

Measure the thermistor **out of circuit**, on a meter, at two temperatures that
straddle the range you actually cook in:

1. Room temperature. Record the resistance *and a real thermometer reading*.
   The anchor is whatever the room actually is, not an assumed 25 °C.
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

Ice water is a poorer second point than boiling water here. 0 °C is far below
anything the fryer does, so it forces the fit to extrapolate across the whole
working range instead of interpolating within it.

A wrong curve and a badly placed probe look identical on the display but are
different faults, and no constant fixes the second one. To tell them apart,
compare the reading against a reference thermometer at steady state, not during
a climb. The sensor has real thermal mass and lags badly while the temperature
is still rising.

### Sensor fault detection

A disconnected or broken NTC lead reads as raw ADC 0, which `temperature.h`
reports as 0.0 °C. The thermostat would read that as "freezing, heat harder" and
hold the element on indefinitely. So any reading below `TEMP_FAULT_MIN_C`
(default 10 °C) is treated as a broken sensor rather than a cold fryer.

It is checked in two places:

- **Before starting**, on the SET_MINS → PREHEAT transition, so a dead sensor
  never energises anything in the first place.
- **On every sample** during PREHEAT and RUNNING, which cuts both relays and
  latches into ERROR.

ERROR shows the offending reading on the LCD (`SENSOR FAULT!` / `reads 0.0C`),
so a stuck 0.0 from an open lead can be told apart from a plausible-but-cold
value. It re-asserts `relaysOff()` on every pass rather than only on entry, and
holds until SEL acknowledges it. If the sensor is still faulty, the next attempt
to start trips it again immediately.

The high side needs no equivalent check. An open ADC input reads full scale,
`readTemperatureC()` returns 999.0 °C, and the thermostat already turns the
heater off. Only the cold direction is dangerous.

### Preheat timeout

Entering PREHEAT stamps a deadline `PREHEAT_TIMEOUT_MS` (default 10 minutes)
ahead. If the measured temperature has not reached `target − TEMP_HYSTERESIS` by
then, both relays drop and the machine latches into ERROR. Without it, a fryer
that physically cannot reach its setpoint (dead element, relay that never
closed, lid left open) sits demanding heat for as long as it is powered, which
is the one behaviour the fault states exist to prevent.

The check sits in the `else` branch of the target-reached test, so a preheat that
completes on the deadline still wins the race. Like the other deadlines in the
sketch, it compares a signed difference (`(long)(preheatEndMs - millis()) <= 0`)
rather than two timestamps, so it survives the `millis()` rollover.

This is a **wall-clock limit, not a stall detector**, and that is its weakness:
it cannot distinguish broken hardware from a legitimately slow climb. A full
basket going from cold to a high setpoint is the case to watch. The rate of rise
falls off badly near the top of the range as losses grow, so the last stretch is
much slower than the first. If it ever trips on a cook that was only slow, raise
the constant. The stricter alternative would be to watch `controlRate()` for a
stall while below target. That catches a dead element in seconds and never
false-trips on a slow one, at the cost of needing its own threshold tuned.

### Fan cooldown

The element holds far more heat than the air around it, the same thermal
inertia the predictive controller exists to fight. Cutting both relays the
instant the countdown expires leaves that stored heat to soak into the chamber
and the basket. So RUNNING → DONE cuts only the element and lets the fan run on
for `COOLDOWN_MS` (default 60 s) to carry it out.

`heaterOff()` exists for exactly this transition: it drops the element and
resynchronises the controller while leaving the fan untouched. `relaysOff()` is
that plus the fan, and is what every other exit uses.

DONE does not sample the NTC, so the cooldown is a fixed duration rather than a
wait for a temperature. It is not a safety interlock and nothing depends on it
finishing. Pressing SEL ends it immediately and returns to SET_TEMP with both
relays off. `fanOn` doubles as the "still cooling" flag, so once it clears, the
state stops doing any work.

---

## Project structure

    espfryer/
    ├── platformio.ini     board, flash layout, port and library pins
    └── src/
        ├── main.cpp       main sketch and state machine
        ├── config.h       all pin definitions and tuneable constants
        ├── temperature.h  NTC → °C conversion (Beta equation)
        ├── control.h      heater control: predictive cutoff + duty modulation
        └── display.h      LCD screen functions for each state
