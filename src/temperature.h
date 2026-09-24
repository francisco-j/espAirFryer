#pragma once
#include <Arduino.h>
#include "config.h"

// Returns temperature in °C using the simplified Beta equation.
// Wiring: 3.3V → NTC → A0 → 10kΩ → GND
//   (ADC reads voltage across the series resistor)
float readTemperatureC() {
    int raw = analogRead(PIN_NTC);

    // Avoid division by zero at rail voltages
    if (raw <= 0)   return 0.0f;
    if (raw >= NTC_ADC_MAX) return 999.0f;

    // Resistance of NTC
    float vRatio = (float)raw / NTC_ADC_MAX;          // V_series / V_supply
    float rNTC   = NTC_SERIES_R * (1.0f / vRatio - 1.0f);

    // Beta equation: 1/T = 1/T0 + (1/Beta)*ln(R/R0)
    float tempK = 1.0f / (1.0f / NTC_T0_K + (1.0f / NTC_BETA) * log(rNTC / NTC_R0));
    return tempK - 273.15f;
}

// Smoothed reading – call repeatedly in loop, returns running average of N samples
float readTemperatureSmoothed(uint8_t samples = 8) {
    float sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += readTemperatureC();
        delay(2);
    }
    return sum / samples;
}
