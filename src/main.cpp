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
 * Libraries required (resolved by PlatformIO from lib_deps):
 *   - LiquidCrystal_I2C  (by Frank de Brabander)
 *
 * State machine:
 *   SET_TEMP → SET_HOURS → SET_MINS → PREHEAT → RUNNING → DONE → SET_TEMP
 *   Any implausibly cold reading diverts to ERROR with the relays forced off, as
 *   does a preheat that has not reached its setpoint within PREHEAT_TIMEOUT_MS.
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"
#include "temperature.h"
#include "control.h"
#include "display.h"

// ── State machine ─────────────────────────────────────────────────────────────
enum State {
    STATE_SET_TEMP,
    STATE_SET_HOURS,
    STATE_SET_MINS,
    STATE_PREHEAT,
    STATE_RUNNING,
    STATE_DONE,
    STATE_ERROR
};

// ── Globals ───────────────────────────────────────────────────────────────────
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

State state         = STATE_SET_TEMP;
int   targetTemp    = TEMP_DEFAULT;
int   cookHours     = TIME_DEFAULT_HOURS;
int   cookMins      = TIME_DEFAULT_MINS;

unsigned long cookEndMs     = 0;  // millis() when cooking ends
unsigned long preheatEndMs  = 0;  // millis() when preheat gives up (watchdog)
unsigned long cooldownEndMs = 0;  // millis() when the DONE fan run-on ends
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

// Cuts the element and leaves the fan alone — the cook-finished path needs
// exactly this, so the fan can run on through the cooldown. The controller's rate
// history and duty accumulator are stale the moment anything other than
// updateThermostat() drives the relay, so clear them here rather than let the
// next run inherit them.
void heaterOff() {
    setHeat(false);
    controlReset();
}

// Every other exit from an active state — abort, sensor fault — drops both.
void relaysOff() {
    heaterOff();
    setFan(false);
}

// ── Cook time entry ───────────────────────────────────────────────────────────
// Step size for the minutes field. Once there is an hour or more on the clock,
// single minutes stop being worth scrolling through, so everything moves in
// quarter hours. Below an hour the step grows with the value.
int minuteStep(int mins, int hours) {
    if (hours >= 1)             return TIME_STEP_QUARTER;
    if (mins < TIME_BAND_MED)   return TIME_STEP_FINE;
    if (mins < TIME_BAND_COARSE) return TIME_STEP_MED;
    return TIME_STEP_COARSE;
}

// Minutes may only reach 0 when at least one hour is set — otherwise the total
// cook time would be zero.
int minMinutes(int hours) { return hours >= 1 ? 0 : 1; }
int maxMinutes(int hours) { return hours >= 1 ? TIME_MINS_MAX_HRS : TIME_MINS_MAX; }

int totalCookMins() { return cookHours * 60 + cookMins; }

// ── Sensor plausibility ───────────────────────────────────────────────────────
// An open or broken NTC lead reads as raw 0, which readTemperatureC() reports as
// 0.0 °C. Left unchecked the thermostat reads that as "freezing, heat harder"
// and holds the element on indefinitely, so treat implausibly cold as a fault.
bool sensorFault(float t) { return t < TEMP_FAULT_MIN_C; }

// ── Temperature control ───────────────────────────────────────────────────────
// The switching decision lives in control.h — predictive cutoff plus a duty
// taper, because plain hysteresis cannot cope with the element's thermal inertia
// (it overshot a 50 °C setpoint by ~20 °C). All this does is push the reading in
// and drive the relay with whatever comes back.
void updateThermostat() {
    controlUpdate(currentTemp, (float)targetTemp);
    setHeat(controlHeatDemand());
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

            if (sensorFault(currentTemp)) {
                // Cut power before the thermostat can act on a bad reading.
                relaysOff();
                state = STATE_ERROR;
                displayError(lcd, currentTemp);
                sel = false;   // don't let this pass's press dismiss it instantly
                Serial.printf("SENSOR FAULT: %.1f C – relays off\n", currentTemp);
            } else {
                updateThermostat();
                // Rate and duty are logged because they are what you tune
                // PREDICT_LEAD_S and APPROACH_BAND_C against.
                Serial.printf("Temp: %.1f / %d C  rate:%+.2f C/s  proj:%.1f  "
                              "duty:%3.0f%%  Heat:%d Fan:%d\n",
                              currentTemp, targetTemp, controlRate(),
                              currentTemp + controlRate() * PREDICT_LEAD_S,
                              controlDuty() * 100.0f, heatOn, fanOn);
            }
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
            state = STATE_SET_HOURS;
            displaySetHours(lcd, targetTemp, cookHours);
        }
        break;

    // ── Select cook time: hours ───────────────────────────────────────────────
    case STATE_SET_HOURS:
        if (up || dn) {
            cookHours = up ? min(cookHours + 1, TIME_MAX_HOURS)
                           : max(cookHours - 1, 0);
            // Crossing the 1-hour boundary changes both bounds on the minutes
            // field, so re-clamp it before it can be shown or edited.
            cookMins = constrain(cookMins, minMinutes(cookHours), maxMinutes(cookHours));
            displaySetHours(lcd, targetTemp, cookHours);
        }
        if (sel) {
            state = STATE_SET_MINS;
            displaySetMins(lcd, targetTemp, cookHours, cookMins);
        }
        break;

    // ── Select cook time: minutes ─────────────────────────────────────────────
    case STATE_SET_MINS:
        if (up) {
            cookMins = min(cookMins + minuteStep(cookMins, cookHours),
                           maxMinutes(cookHours));
            displaySetMins(lcd, targetTemp, cookHours, cookMins);
        }
        if (dn) {
            // Step by the band the value is moving *into*, so that pressing up
            // then down returns to the value you started from.
            cookMins = max(cookMins - minuteStep(cookMins - 1, cookHours),
                           minMinutes(cookHours));
            displaySetMins(lcd, targetTemp, cookHours, cookMins);
        }
        if (sel) {
            // Prove the sensor works *before* energising anything.
            currentTemp = readTemperatureSmoothed(4);
            if (sensorFault(currentTemp)) {
                state = STATE_ERROR;
                displayError(lcd, currentTemp);
                Serial.printf("SENSOR FAULT: %.1f C – refusing to start\n", currentTemp);
            } else {
                // Start preheat: fan ON, wait until target reached. The heater is
                // left to the controller — from cold it demands full power, so
                // this fires the element on the spot, but starting it through
                // the controller keeps the duty accounting honest.
                state = STATE_PREHEAT;
                preheatEndMs = millis() + PREHEAT_TIMEOUT_MS;
                setFan(true);
                controlReset();
                updateThermostat();
                displayPreheat(lcd, currentTemp, targetTemp);
            }
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
            cookEndMs  = millis() + (unsigned long)totalCookMins() * 60UL * 1000UL;
            lastDisplayMs = 0;
        } else if ((long)(preheatEndMs - millis()) <= 0) {
            // Watchdog: still short of the setpoint after PREHEAT_TIMEOUT_MS. The
            // element is either broken or fighting something it cannot win, and
            // the only other option is to keep demanding heat indefinitely — so
            // latch the same fault a bad sensor produces. Checked in the else
            // branch so a preheat that completes on the deadline still wins.
            relaysOff();
            state = STATE_ERROR;
            displayTimeout(lcd, currentTemp, targetTemp);
            sel = false;   // don't let this pass's press dismiss it instantly
            Serial.printf("PREHEAT TIMEOUT: %.1f of %d C after %lu s – relays off\n",
                          currentTemp, targetTemp, PREHEAT_TIMEOUT_MS / 1000UL);
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
            // Cook finished. Element off, fan left running for COOLDOWN_MS so the
            // heat still stored in the element is carried out rather than left to
            // soak into the chamber and the basket.
            heaterOff();
            state         = STATE_DONE;
            cooldownEndMs = millis() + COOLDOWN_MS;
            lastDisplayMs = millis();
            displayDone(lcd, COOLDOWN_MS / 1000UL);
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
    case STATE_DONE: {
        // Fan-only cooldown. Signed difference, like the cook countdown above, so
        // the comparison still works across the millis() rollover.
        long coolLeft = (long)(cooldownEndMs - millis()) / 1000L;

        // fanOn doubles as "cooldown still in progress", so this stops running
        // once the fan is off and the screen is not repainted on every pass.
        if (fanOn) {
            if (coolLeft <= 0) {
                setFan(false);
                displayDone(lcd);            // drops the countdown line
            } else if (millis() - lastDisplayMs >= DISPLAY_REFRESH_MS) {
                lastDisplayMs = millis();
                displayDone(lcd, (int)coolLeft);
            }
        }

        // SEL ends the cooldown early rather than having to be waited out.
        if (sel) {
            relaysOff();
            state = STATE_SET_TEMP;
            displaySetTemp(lcd, targetTemp);
        }
        break;
    }

    // ── Fault (bad sensor or preheat timeout) – latched until acknowledged ────
    // The screen was painted by whichever check latched the fault and is never
    // repainted here, so it keeps naming the cause until SEL clears it.
    case STATE_ERROR:
        relaysOff();   // re-asserted every pass, not just on entry
        if (sel) {
            state = STATE_SET_TEMP;
            displaySetTemp(lcd, targetTemp);
        }
        break;
    }
}
