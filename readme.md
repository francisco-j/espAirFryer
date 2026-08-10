## Project structure

espfryer/
 - espfryer.ino   – main sketch & state machine
 - config.h       – all pin definitions and tuneable constants
 - temperature.h  – NTC → °C conversion (Beta equation)
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