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

    // Line 0: "NOW:123C TGT:180C"
    snprintf(buf, sizeof(buf), "NOW:%3dC TGT:%3dC", (int)currentTemp, targetTemp);
    lcd.setCursor(0, 0);
    lcd.print(buf);

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
void displayDone(LiquidCrystal_I2C &lcd) {
    lcd.setCursor(0, 0);
    lcd.print(padRight("   DONE!  :)    ", LCD_COLS));
    lcd.setCursor(0, 1);
    lcd.print(padRight(" Press SEL again", LCD_COLS));
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
