#pragma once

// ── Pin Definitions ──────────────────────────────────────────────────────────
// Raw GPIO numbers, not D0..D8 aliases — those are only defined on the
// NodeMCU/Wemos variants, not on the generic ESP8266 build.
// Buttons (active LOW, internal pull-up enabled — wire each button to GND)
// GPIO12/13/14 are the only fully unrestricted pins left after I2C: no boot
// strapping, no onboard LED, working internal pull-ups. Buttons go here so a
// press during reset can never stop the board booting.
#define PIN_BTN_UP    12   // GPIO12 (D6)
#define PIN_BTN_DOWN  13   // GPIO13 (D7)
#define PIN_BTN_SEL   14   // GPIO14 (D5)

// Relays – on the two strapping-constrained pins, deliberately.
// GPIO15 has a 10k pulldown and GPIO16 defaults LOW, so both sit LOW through
// reset, brownout and crash. With active-HIGH drive that means "off", which is
// the fail-safe state for a heating element.
#define PIN_RELAY_HEAT 15  // GPIO15 (D8) – heating element
#define PIN_RELAY_FAN  16  // GPIO16 (D0) – fan motor

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// NTC thermistor (voltage divider to A0)
#define PIN_NTC       A0

// LCD uses I2C: SDA = GPIO4 (D2), SCL = GPIO5 (D1) – ESP8266 default I2C

// ── LCD ──────────────────────────────────────────────────────────────────────
#define LCD_I2C_ADDR  0x27   // Change to 0x3F if screen stays blank
#define LCD_COLS      16
#define LCD_ROWS      2

// ── Temperature Settings ─────────────────────────────────────────────────────
#define TEMP_MIN_C    40     // °C
#define TEMP_MAX_C    230    // °C
#define TEMP_STEP_C   5      // °C per button press
#define TEMP_DEFAULT  100    // °C

// Hysteresis: heater turns ON below (target - HYST), OFF above (target + HYST)
#define TEMP_HYSTERESIS 2    // °C

// Sensor fault threshold. A disconnected or broken NTC lead reads as raw 0,
// which temperature.h converts to 0.0 °C — a value the thermostat would happily
// treat as "heat harder". Anything this cold is a broken sensor, not a cold
// fryer, so the machine cuts the relays and latches into the error state.
// Lower it if you run the fryer somewhere genuinely near freezing; keep it
// above 0 or an open lead stops being detectable.
#define TEMP_FAULT_MIN_C 10  // °C

// ── Time Settings ────────────────────────────────────────────────────────────
// Cook time is entered as hours first, then minutes.
#define TIME_MAX_HOURS     6     // hours
#define TIME_DEFAULT_HOURS 0
#define TIME_DEFAULT_MINS  30

// Minute step ladder. With an hour or more already on the clock the fine bands
// stop being useful, so minutes move in quarter hours (0/15/30/45). With no
// hours set the step grows with the value: 1 min below 10, 5 up to 30, 10 above.
#define TIME_STEP_QUARTER  15    // used whenever hours >= 1
#define TIME_STEP_FINE     1     // minutes < TIME_BAND_MED
#define TIME_STEP_MED      5     // TIME_BAND_MED .. TIME_BAND_COARSE
#define TIME_STEP_COARSE   10    // >= TIME_BAND_COARSE
#define TIME_BAND_MED      10    // minutes
#define TIME_BAND_COARSE   30    // minutes

// Upper bound on the minutes field. Capped to the last quarter hour when hours
// are set so the 15-minute ladder lands cleanly instead of clamping to 59.
#define TIME_MINS_MAX      50
#define TIME_MINS_MAX_HRS  45

// ── NTC Thermistor (100kΩ NTC, 10kΩ series resistor) ─────────────────────────
#define NTC_BETA      3950   // Beta coefficient (from datasheet)
#define NTC_R0        100000  // Nominal resistance at T0 (Ω)
#define NTC_T0_K      298.15 // Nominal temperature in Kelvin (25°C)
#define NTC_SERIES_R  10000  // Series resistor value (Ω)
#define NTC_ADC_MAX   1023   // ESP8266 ADC resolution

// ── Timing ───────────────────────────────────────────────────────────────────
#define BTN_DEBOUNCE_MS   50
#define TEMP_SAMPLE_MS    500   // how often to read NTC
#define DISPLAY_REFRESH_MS 1000  // how often to refresh LCD during cooking
