/*
 * ESPFryer – ESP8266 Air Fryer Controller
 *
 * Hardware:
 *   - ESP8266 (NodeMCU / Wemos D1 mini)
 *   - 16x2 I2C LCD (SDA=D2, SCL=D1)
 *   - 3 push buttons to GND: UP (D6), DOWN (D7), SELECT (D5)
 *   - NTC 10kΩ thermistor on A0 (voltage divider with 10kΩ to GND)
 *   - Relay 1 (D8): heating element   – active HIGH, see RELAY_ACTIVE_HIGH
 *   - Relay 2 (D0): fan motor         – active HIGH, see RELAY_ACTIVE_HIGH
 *
 * Libraries required (install via Library Manager):
 *   - LiquidCrystal_I2C  (by Frank de Brabander)
 *
 * State machine:
 *   SET_TEMP → SET_TIME → PREHEAT → RUNNING → DONE → SET_TEMP
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"
#include "temperature.h"
#include "display.h"

// ── State machine ─────────────────────────────────────────────────────────────
enum State {
    STATE_SET_TEMP,
    STATE_SET_TIME,
    STATE_PREHEAT,
    STATE_RUNNING,
    STATE_DONE
};

// ── Globals ───────────────────────────────────────────────────────────────────
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

State state         = STATE_SET_TEMP;
int   targetTemp    = TEMP_DEFAULT;
int   cookTimeMins  = TIME_DEFAULT;

unsigned long cookEndMs     = 0;  // millis() when cooking ends
unsigned long lastTempMs    = 0;  // last NTC sample time
unsigned long lastDisplayMs = 0;  // last display refresh

float currentTemp   = 0.0f;
bool  heatOn        = false;
bool  fanOn         = false;

// ── Button helpers ────────────────────────────────────────────────────────────
struct Button {
    uint8_t  pin;
    bool     lastState;
    unsigned long lastChangeMs;
};

Button btnUp   = { PIN_BTN_UP,   HIGH, 0 };
Button btnDown = { PIN_BTN_DOWN, HIGH, 0 };
Button btnSel  = { PIN_BTN_SEL,  HIGH, 0 };

// Returns true once per press (after debounce). Active LOW.
bool wasPressed(Button &btn) {
    bool reading = digitalRead(btn.pin);
    if (reading == btn.lastState) return false;

    if (millis() - btn.lastChangeMs < BTN_DEBOUNCE_MS) return false;

    btn.lastState    = reading;
    btn.lastChangeMs = millis();
    return reading == LOW;  // fired on falling edge
}

// ── Relay control ─────────────────────────────────────────────────────────────
void setHeat(bool on) {
    heatOn = on;
    digitalWrite(PIN_RELAY_HEAT, on ? RELAY_ON : RELAY_OFF);
}

void setFan(bool on) {
    fanOn = on;
    digitalWrite(PIN_RELAY_FAN, on ? RELAY_ON : RELAY_OFF);
}

void relaysOff() {
    setHeat(false);
    setFan(false);
}

// ── Temperature control (simple hysteresis) ───────────────────────────────────
void updateThermostat() {
    if (currentTemp < targetTemp - TEMP_HYSTERESIS) setHeat(true);
    if (currentTemp > targetTemp + TEMP_HYSTERESIS) setHeat(false);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Buttons
    pinMode(PIN_BTN_UP,   INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
    pinMode(PIN_BTN_SEL,  INPUT_PULLUP);

    // Relays – latch OFF *before* enabling the output driver, so the pin never
    // drives the ON level for even one instruction.
    digitalWrite(PIN_RELAY_HEAT, RELAY_OFF);
    digitalWrite(PIN_RELAY_FAN,  RELAY_OFF);
    pinMode(PIN_RELAY_HEAT, OUTPUT);
    pinMode(PIN_RELAY_FAN,  OUTPUT);
    relaysOff();

    // LCD
    Wire.begin();
    lcd.init();
    lcd.backlight();

    // Initial display
    displaySetTemp(lcd, targetTemp);

    // First temperature reading
    currentTemp = readTemperatureSmoothed();

    Serial.println("ESPFryer ready.");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    bool up  = wasPressed(btnUp);
    bool dn  = wasPressed(btnDown);
    bool sel = wasPressed(btnSel);

    // Periodic NTC sample (all active states)
    if (state == STATE_PREHEAT || state == STATE_RUNNING) {
        if (millis() - lastTempMs >= TEMP_SAMPLE_MS) {
            lastTempMs = millis();
            currentTemp = readTemperatureSmoothed(4);
            updateThermostat();
            Serial.printf("Temp: %.1f / %d C  Heat:%d Fan:%d\n",
                          currentTemp, targetTemp, heatOn, fanOn);
        }
    }

    switch (state) {

    // ── Select temperature ────────────────────────────────────────────────────
    case STATE_SET_TEMP:
        if (up) {
            targetTemp = min(targetTemp + TEMP_STEP_C, TEMP_MAX_C);
            displaySetTemp(lcd, targetTemp);
        }
        if (dn) {
            targetTemp = max(targetTemp - TEMP_STEP_C, TEMP_MIN_C);
            displaySetTemp(lcd, targetTemp);
        }
        if (sel) {
            state = STATE_SET_TIME;
            displaySetTime(lcd, targetTemp, cookTimeMins);
        }
        break;

    // ── Select cook time ──────────────────────────────────────────────────────
    case STATE_SET_TIME:
        if (up) {
            cookTimeMins = min(cookTimeMins + TIME_STEP_MIN, TIME_MAX_MIN);
            displaySetTime(lcd, targetTemp, cookTimeMins);
        }
        if (dn) {
            cookTimeMins = max(cookTimeMins - TIME_STEP_MIN, TIME_MIN_MIN);
            displaySetTime(lcd, targetTemp, cookTimeMins);
        }
        if (sel) {
            // Start preheat: fan ON, heater ON, wait until target reached
            state = STATE_PREHEAT;
            setFan(true);
            setHeat(true);
            currentTemp = readTemperatureSmoothed(4);
            displayPreheat(lcd, currentTemp, targetTemp);
        }
        break;

    // ── Preheat until target reached ──────────────────────────────────────────
    case STATE_PREHEAT:
        if (millis() - lastDisplayMs >= DISPLAY_REFRESH_MS) {
            lastDisplayMs = millis();
            displayPreheat(lcd, currentTemp, targetTemp);
        }
        if (currentTemp >= targetTemp - TEMP_HYSTERESIS) {
            // Target reached – start countdown
            state      = STATE_RUNNING;
            cookEndMs  = millis() + (unsigned long)cookTimeMins * 60UL * 1000UL;
            lastDisplayMs = 0;
        }
        // Allow user to abort by holding SEL
        if (sel) {
            relaysOff();
            state = STATE_SET_TEMP;
            displaySetTemp(lcd, targetTemp);
        }
        break;

    // ── Cooking ───────────────────────────────────────────────────────────────
    case STATE_RUNNING: {
        long remaining = (long)(cookEndMs - millis()) / 1000L;

        if (remaining <= 0) {
            // Done!
            relaysOff();
            state = STATE_DONE;
            displayDone(lcd);
            break;
        }

        if (millis() - lastDisplayMs >= DISPLAY_REFRESH_MS) {
            lastDisplayMs = millis();
            displayRunning(lcd, currentTemp, targetTemp, (int)remaining, heatOn, fanOn);
        }

        // Allow user to abort
        if (sel) {
            relaysOff();
            state = STATE_SET_TEMP;
            displaySetTemp(lcd, targetTemp);
        }
        break;
    }

    // ── Done ──────────────────────────────────────────────────────────────────
    case STATE_DONE:
        if (sel) {
            state = STATE_SET_TEMP;
            displaySetTemp(lcd, targetTemp);
        }
        break;
    }
}
