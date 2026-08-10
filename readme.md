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

    SET_TEMP → SET_TIME → PREHEAT → RUNNING → DONE
        ▲                    │         │        │
        │                    │ SEL     │ SEL    │ SEL
        └────────────────────┴─────────┴────────┘
               (abort / reset — all return to SET_TEMP)

PREHEAT advances to RUNNING on its own once the measured temperature reaches
`target − TEMP_HYSTERESIS`; the cook countdown starts at that moment, so
preheating does not eat into cook time. RUNNING advances to DONE when the
countdown expires. Both drop the relays on the way out.

| State | UP/DOWN | SELECT |
|---|---|---|
| SET_TEMP | ±5 °C | advance to SET_TIME |
| SET_TIME | ±15 min | start preheat |
| PREHEAT | – | abort → SET_TEMP |
| RUNNING | – | abort → SET_TEMP |
| DONE | – | back to SET_TEMP |

## Before uploading
Required library (Library Manager): LiquidCrystal_I2C by Frank de Brabander.

Check/adjust in config.h:

LCD_I2C_ADDR — try 0x3F if the screen stays blank
NTC_BETA — get this from your specific thermistor datasheet (common values: 3435, 3950)
NTC_SERIES_R — match your actual series resistor value
Wiring: NTC divider is 3.3V → NTC → A0 → 10kΩ → GND (ADC reads voltage across the series resistor)

RELAY_ACTIVE_HIGH — defaults to 1 (HIGH = relay ON). Check your relay board
before wiring mains: power the board with the input pin left floating. If the
relay clicks in, it is an active-LOW module. In that case do **not** just set
RELAY_ACTIVE_HIGH to 0 — you must also move the relays off GPIO15/16, because
GPIO15's 10kΩ pulldown would then hold the heating element ON through every
reset. Either drive an active-LOW board through a transistor inverter and keep
the pins, or move the relays to GPIO12/13 and the buttons to GPIO0/2/14 (the
old layout — and then add 10kΩ pull-ups to 3.3V on both relay inputs, or they
pulse ON at boot).


# load
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"; FQBN="esp8266:esp8266:generic:xtal=80,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=dout,eesz=4M1M,baud=115200"; "$CLI" compile --fqbn "$FQBN" /Users/javierf/Proyects/espfryer 2>&1 | tail -15 && echo "=== UPLOADING ===" && "$CLI" upload -p /dev/cu.usbserial-A5069RR4 --fqbn "$FQBN" /Users/javierf/Proyects/espfryer 2>&1 | tail -12