#pragma once
#include <LiquidCrystal_I2C.h>
#include "config.h"

// Pad a string to exactly `width` chars (truncates if longer)
static String padRight(String s, uint8_t width) {
    while (s.length() < width) s += ' ';
    if (s.length() > width) s = s.substring(0, width);
    return s;
}

// ── Menu screens ─────────────────────────────────────────────────────────────

void displaySetTemp(LiquidCrystal_I2C &lcd, int targetTemp) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("Set Temperature:", LCD_COLS));
    lcd.setCursor(0, 1);
    lcd.print(padRight("  " + String(targetTemp) + " C  [^v OK]", LCD_COLS));
}

void displaySetHours(LiquidCrystal_I2C &lcd, int targetTemp, int hours) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("Set Hrs (" + String(targetTemp) + "C):", LCD_COLS));
    lcd.setCursor(0, 1);
    lcd.print(padRight("  " + String(hours) + " h    [^v OK]", LCD_COLS));
}

// Shows both fields so the running total stays visible while editing minutes.
void displaySetMins(LiquidCrystal_I2C &lcd, int targetTemp, int hours, int mins) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("Set Min (" + String(targetTemp) + "C):", LCD_COLS));
    lcd.setCursor(0, 1);
    lcd.print(padRight("  " + String(hours) + "h " + String(mins) + "m [^v OK]", LCD_COLS));
}

// ── Running screen ────────────────────────────────────────────────────────────
// Shows current vs target temp and remaining time with heater/fan indicators.
void displayRunning(LiquidCrystal_I2C &lcd, float currentTemp, int targetTemp,
                    int remainingSecs, bool heatOn, bool fanOn) {
    int hrs  = remainingSecs / 3600;
    int mins = (remainingSecs % 3600) / 60;
    int secs = remainingSecs % 60;

    char buf[LCD_COLS + 1];

    // Line 0: "123C TRGT:180C". The current temperature carries no label
    snprintf(buf, sizeof(buf), "%3dC  TRGT:%3dC", (int)currentTemp, targetTemp);
    lcd.setCursor(0, 0);
    lcd.print(padRight(String(buf), LCD_COLS));

    // Countdown drops the hours field once it is no longer needed, so short
    // cooks keep the familiar MM:SS instead of a permanent leading "0:".
    char clock[10];
    if (hrs > 0) snprintf(clock, sizeof(clock), "%d:%02d:%02d", hrs, mins, secs);
    else         snprintf(clock, sizeof(clock), "%02d:%02d", mins, secs);

    // Line 1: "02:45  [H] [F]"  indicators toggle with relay state
    String status = String(clock) + "  " +
                    String(heatOn ? "[H]" : "   ") + " " +
                    String(fanOn  ? "[F]" : "   ");
    lcd.setCursor(0, 1);
    lcd.print(padRight(status, LCD_COLS));
}

// ── Done screen ───────────────────────────────────────────────────────────────
// While the fan is still running out the cooldown, line 1 counts it down instead
// of inviting a keypress — otherwise the screen says the cook is over while the
// fan is audibly still going, which reads as a fault. SEL works either way.
void displayDone(LiquidCrystal_I2C &lcd, int coolSecs = 0) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("   DONE!  :)    ", LCD_COLS));
    lcd.setCursor(0, 1);
    if (coolSecs > 0) {
        char buf[LCD_COLS + 1];
        snprintf(buf, sizeof(buf), "Cooling %ds [F]", coolSecs);
        lcd.print(padRight(String(buf), LCD_COLS));
    } else {
        lcd.print(padRight(" Press SEL again", LCD_COLS));
    }
}

// ── Sensor fault screen ───────────────────────────────────────────────────────
// Shows the offending reading so a stuck 0.0 (open lead) can be told apart from
// a plausible-but-too-cold value.
void displayError(LiquidCrystal_I2C &lcd, float currentTemp) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("SENSOR FAULT!", LCD_COLS));
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), "reads %.1fC", currentTemp);
    lcd.setCursor(0, 1);
    lcd.print(padRight(String(buf), LCD_COLS));
}

// ── Preheat timeout screen ────────────────────────────────────────────────────
// Shares STATE_ERROR with the sensor fault, so it has to name which fault it is.
// Line 1 is how far the preheat actually got, which is the diagnostic: a reading
// close to target points at an underpowered element or a lid left open, one
// barely off ambient at a dead element or a relay that never closed.
void displayTimeout(LiquidCrystal_I2C &lcd, float currentTemp, int targetTemp) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("PREHEAT TIMEOUT!", LCD_COLS));
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), " %3dC of %3dC", (int)currentTemp, targetTemp);
    lcd.setCursor(0, 1);
    lcd.print(padRight(String(buf), LCD_COLS));
}

// ── Preheat screen ────────────────────────────────────────────────────────────
void displayPreheat(LiquidCrystal_I2C &lcd, float currentTemp, int targetTemp) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("Preheating...", LCD_COLS));
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), " %3dC -> %3dC", (int)currentTemp, targetTemp);
    lcd.setCursor(0, 1);
    lcd.print(padRight(String(buf), LCD_COLS));
}
