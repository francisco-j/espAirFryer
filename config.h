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

// Hysteresis band. With the predictive controller in control.h this is no longer
// the main switching rule — it is the hard overtemp backstop (heat is cut the
// moment the *measured* temperature exceeds target + HYST, minimum on-time
// ignored) and the threshold PREHEAT uses to declare the setpoint reached.
#define TEMP_HYSTERESIS 2    // °C

// ── Heater modulation (thermal-inertia compensation) ─────────────────────────
// See the header comment in control.h for what these do. Tuning order: get
// PREDICT_LEAD_S right first, then trim APPROACH_BAND_C and DUTY_HOLD.

// How far ahead the controller extrapolates the measured rate of rise when
// deciding to cut power. A good first guess is the overshoot you get with plain
// hysteresis divided by the rate of rise near the setpoint — 20 °C at
// 0.4 °C/s ≈ 50 s. Raise it if the fryer still overshoots; lower it if preheat
// crawls or stalls short of the setpoint.
#define PREDICT_LEAD_S      45.0f   // seconds

// Rate of rise is measured across this window rather than between adjacent
// samples — one 500 ms interval is all ADC noise. Should be a whole multiple of
// TEMP_SAMPLE_MS. DERIV_SMOOTH_ALPHA then low-passes the result: lower is
// smoother but adds lag, which works against the prediction.
#define DERIV_WINDOW_MS     5000
#define DERIV_SMOOTH_ALPHA  0.3f    // 0..1

// Proportional band: full power below (target - band), tapering linearly to
// DUTY_HOLD as the projected temperature reaches the setpoint. DUTY_HOLD is the
// duty demanded right at the setpoint — the standing loss the element has to
// cover. Too high and the temperature creeps up, too low and it droops.
#define APPROACH_BAND_C     15.0f   // °C
#define DUTY_HOLD           0.20f   // 0..1

// Mechanical relay protection: once switched, the relay is held at least this
// long. The modulator stretches its period to honour these rather than dropping
// short pulses, so low duties stay accurate. Cost: a cutoff can lag by up to
// RELAY_MIN_ON_MS, which the predictive lead above already covers.
#define RELAY_MIN_ON_MS     2000
#define RELAY_MIN_OFF_MS    2000

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

// ── NTC Thermistor (120kΩ NTC, 10kΩ series resistor) ─────────────────────────
// Measured, not from a datasheet — the part is not the 100kΩ/3950 it was sold
// as. Two-point measurement with a meter: 120 kΩ at 24 °C, 7.8 kΩ in boiling
// water taken as 95 °C. Beta follows from those two points:
//   B = ln(R24/R95) / (1/297.15 - 1/368.15) = 4212
// The anchor is 24 °C, not the conventional 25 — T0 and R0 are one measured
// pair and must be changed together. Likewise beta is only meaningful paired
// with the R0/T0 it was fitted against; if you re-measure, redo all three.
//
// The curve passes exactly through both measured points, so it is accurate
// across 24-95 °C and extrapolates to the 120 °C top of the working range.
// The 95 °C boiling assumption implies roughly 1500 m elevation — it sets the
// hot end of the fit, so it is the number to re-check first if readings drift
// at temperature.
#define NTC_BETA      4212   // Beta coefficient (fitted, 24/95 °C)
#define NTC_R0        120000 // Measured resistance at T0 (Ω)
#define NTC_T0_K      297.15 // Anchor temperature in Kelvin (24°C)
// 10kΩ series is kept deliberately. The theoretical optimum for a 40-120 °C
// span is ~15kΩ, but that buys 608 ADC counts instead of 593 — a 2.5% gain.
// At 10kΩ the quantisation is 0.18 °C/count at both 40 °C and 120 °C, far
// below the sensor noise floor. Not worth rewiring.
#define NTC_SERIES_R  10000  // Series resistor value (Ω)
#define NTC_ADC_MAX   1023   // ESP8266 ADC resolution

// ── Timing ───────────────────────────────────────────────────────────────────
#define BTN_DEBOUNCE_MS   50
#define TEMP_SAMPLE_MS    500   // how often to read NTC
#define DISPLAY_REFRESH_MS 900  // how often to refresh LCD during cooking

// Fan-only run-on after the cook finishes. The element holds far more heat than
// the air around it — the same inertia the predictive controller exists to fight
// — so cutting both relays together leaves that heat to soak into the chamber
// and the basket instead of being carried out. SEL ends it early.
#define COOLDOWN_MS       90000UL   // 1.5 minute
