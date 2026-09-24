#pragma once
#include <Arduino.h>
#include "config.h"

// ── Heater control ────────────────────────────────────────────────────────────
// Replaces plain hysteresis, which cannot work on a fryer: the element holds far
// more heat than the chamber air, so cutting power the instant the setpoint is
// reached still dumps ~20 °C of stored energy into the chamber afterwards.
//
// Two mechanisms, both tuneable in config.h:
//
//   1. The cutoff decision runs on a *projected* temperature — the current
//      reading extrapolated PREDICT_LEAD_S seconds along the measured rate of
//      rise — so the element goes off while still climbing and coasts in.
//   2. Power is duty-cycled down across APPROACH_BAND_C rather than switching
//      hard from 100% to 0%, so there is less stored energy left to coast on.
//
// Together these are a PD controller with the derivative gain expressed as a
// lead time. The duty demand is then modulated onto a relay that can only
// switch every few seconds (see ctlCredit below).
//
// Nothing here touches hardware: controlUpdate() returns the state it wants and
// the caller drives the relay. So an abort or a sensor fault can still cut the
// relays directly — call controlReset() afterwards to resynchronise.

// Rate of rise is measured across the whole window, not between adjacent
// samples: the ESP8266 ADC is far too noisy for a 500 ms baseline.
#define DERIV_SAMPLES ((DERIV_WINDOW_MS / TEMP_SAMPLE_MS) + 1)

static float         ctlTemp[DERIV_SAMPLES];   // ring of recent readings
static unsigned long ctlTime[DERIV_SAMPLES];   // and their timestamps
static uint8_t       ctlHead  = 0;
static uint8_t       ctlCount = 0;

static float ctlRate   = 0.0f;   // °C/s, smoothed
static float ctlDuty   = 0.0f;   // 0..1 demanded
static float ctlCredit = 0.0f;   // ms of element on-time owed, see below
static bool  ctlHeat   = false;  // relay state the controller wants

static unsigned long ctlSwitchMs = 0;   // when the relay last changed
static unsigned long ctlLastMs   = 0;   // last controlUpdate()

// Clears the derivative history and the duty accumulator. Backdates the switch
// timestamp so the minimum off-time does not delay the first burst of a preheat.
void controlReset() {
    ctlHead = ctlCount = 0;
    ctlRate = ctlDuty = ctlCredit = 0.0f;
    ctlHeat = false;
    ctlLastMs   = millis();
    ctlSwitchMs = millis() - RELAY_MIN_OFF_MS;
}

static void ctlPush(float t, unsigned long now) {
    ctlTemp[ctlHead] = t;
    ctlTime[ctlHead] = now;
    ctlHead = (ctlHead + 1) % DERIV_SAMPLES;
    if (ctlCount < DERIV_SAMPLES) ctlCount++;
}

// Index of the oldest sample still in the ring.
static uint8_t ctlOldest() {
    return (ctlHead + DERIV_SAMPLES - ctlCount) % DERIV_SAMPLES;
}

// Call once per temperature sample. Returns nothing; read the result with
// controlHeatDemand().
void controlUpdate(float temp, float target) {
    unsigned long now = millis();
    unsigned long dt  = now - ctlLastMs;
    ctlLastMs = now;

    // ── Rate of rise, over the full window and then low-pass filtered ────────
    ctlPush(temp, now);
    if (ctlCount >= 2) {
        uint8_t       o    = ctlOldest();
        unsigned long span = now - ctlTime[o];
        if (span > 0) {
            float raw = (temp - ctlTemp[o]) * 1000.0f / (float)span;   // °C/s
            ctlRate += DERIV_SMOOTH_ALPHA * (raw - ctlRate);
        }
    }

    // ── Duty demand: proportional band on the projected temperature ──────────
    // Full power until the projection is within APPROACH_BAND_C of the setpoint,
    // then a linear taper down to DUTY_HOLD, then nothing once the projection
    // reaches the setpoint. Throttling lowers the rate of rise, which shortens
    // the projection and lets the duty back up — the loop settles itself.
    float error = target - (temp + ctlRate * PREDICT_LEAD_S);

    if      (error <= 0.0f)             ctlDuty = 0.0f;
    else if (error >= APPROACH_BAND_C)  ctlDuty = 1.0f;
    else ctlDuty = DUTY_HOLD + (1.0f - DUTY_HOLD) * (error / APPROACH_BAND_C);

    // ── Time-proportional modulator with minimum on/off times ────────────────
    // ctlCredit is on-time owed, in ms: it accrues at the demanded duty and
    // drains in real time while the element is on. Switching only when a full
    // RELAY_MIN_ON_MS has been banked stretches the period instead of
    // truncating the pulse, so even a 5% demand comes out as ~3 s on / ~60 s off
    // rather than a burst too short for the relay to bother with.
    //
    // Capped at RELAY_MIN_ON_MS so a long full-power ramp cannot bank credit
    // that would hold the element on past a cutoff.
    ctlCredit += ctlDuty * (float)dt;
    if (ctlHeat) ctlCredit -= (float)dt;
    ctlCredit = constrain(ctlCredit, 0.0f, (float)RELAY_MIN_ON_MS);

    unsigned long held = now - ctlSwitchMs;

    if (temp > target + TEMP_HYSTERESIS) {
        // Measured (not projected) overtemp cuts power immediately, ignoring the
        // minimum on-time — safety outranks contact wear.
        ctlCredit = 0.0f;
        if (ctlHeat) { ctlHeat = false; ctlSwitchMs = now; }
    } else if (ctlHeat) {
        if (held >= RELAY_MIN_ON_MS && ctlCredit <= 0.0f) {
            ctlHeat = false;
            ctlSwitchMs = now;
        }
    } else {
        // At full demand there is nothing to modulate, so skip the accumulator
        // and fire as soon as the minimum off-time allows.
        if (held >= RELAY_MIN_OFF_MS &&
            (ctlDuty >= 1.0f || ctlCredit >= (float)RELAY_MIN_ON_MS)) {
            ctlHeat = true;
            ctlSwitchMs = now;
        }
    }
}

bool  controlHeatDemand() { return ctlHeat; }
float controlDuty()       { return ctlDuty; }   // 0..1, for logging
float controlRate()       { return ctlRate; }   // °C/s, for logging
