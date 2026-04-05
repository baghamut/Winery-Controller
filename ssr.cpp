// =============================================================================
//  ssr.cpp  –  SSR driver  (software time-proportional, replaces LEDC)
//
//  The ESP32-S3 LEDC peripheral cannot generate 10 Hz: the required divider
//  value (~8 000 000) exceeds the 18-bit hardware maximum (262 143).
//  Instead, an esp_timer fires every SSR_TICK_MS (100 ms).  Each SSR_PERIOD_MS
//  (2 s) cycle is divided into SSR_TICKS_PER_PERIOD (20) ticks.
//  Duty 0–100 % → 0–20 ON ticks per period (5 % resolution, adequate for
//  proportional heater control).
//
//  Public API is unchanged:
//    ssrInit()           – configure GPIO + start timer
//    ssrSetDuty(n, pct)  – set SSR n (1-based) to pct % duty
//    ssrAllOff()         – immediately de-energise all SSRs
//
//  This file is a pure hardware abstraction layer.
//  All policy (which SSRs run, at what duty) is in control.cpp.
// =============================================================================
#include "ssr.h"
#include "config.h"
#include "esp_timer.h"

// GPIO pin map: index 0 = SSR1 … index 4 = SSR5
static const uint8_t SSR_PINS[SSR_COUNT] = {
    PIN_SSR1,   // SSR1 – Distillation heater A
    PIN_SSR2,   // SSR2 – Distillation heater B
    PIN_SSR3,   // SSR3 – Distillation heater C
    PIN_SSR4,   // SSR4 – Rectification heater A
    PIN_SSR5    // SSR5 – Rectification heater B
};

// Desired duty for each SSR: integer 0–100 (percent).
// Written by ssrSetDuty() / ssrAllOff() from controlTask (Core 0).
// Read inside ssrTimerCb() from the esp_timer service task.
// uint8_t stores are atomic on 32-bit Xtensa; volatile prevents caching.
static volatile uint8_t s_duty[SSR_COUNT] = {0, 0, 0, 0, 0};

// Current position within the period: 0 … SSR_TICKS_PER_PERIOD-1.
// Only touched inside ssrTimerCb() – no concurrent access.
static uint8_t s_tick = 0;

static esp_timer_handle_t s_ssrTimer = nullptr;

// ---------------------------------------------------------------------------
// ssrTimerCb
//   Called every SSR_TICK_MS from the esp_timer service task.
//   Drives each SSR pin HIGH during the first (duty × period / 100) ticks.
// ---------------------------------------------------------------------------
static void ssrTimerCb(void* /*arg*/) {
    if (++s_tick >= SSR_TICKS_PER_PERIOD) s_tick = 0;

    for (int i = 0; i < SSR_COUNT; i++) {
        uint8_t d = s_duty[i];                               // atomic read

        bool on;
        if      (d == 0)   on = false;                       // fast path: fully off
        else if (d >= 100) on = true;                        // fast path: fully on
        else               on = (s_tick < (d * SSR_TICKS_PER_PERIOD / 100u));

        digitalWrite(SSR_PINS[i], on ? HIGH : LOW);
    }
}

// ---------------------------------------------------------------------------
// ssrInit
//   Configure all SSR pins as GPIO outputs and start the period timer.
//   Call once from setup() before starting controlTask().
// ---------------------------------------------------------------------------
void ssrInit() {
    for (int i = 0; i < SSR_COUNT; i++) {
        pinMode(SSR_PINS[i], OUTPUT);
        digitalWrite(SSR_PINS[i], LOW);    // never energise heaters at boot
    }

    const esp_timer_create_args_t args = {
        .callback             = ssrTimerCb,
        .arg                  = nullptr,
        .dispatch_method      = ESP_TIMER_TASK,  // safe for digitalWrite
        .name                 = "ssrTimer",
        .skip_unhandled_events = true,           // drop missed ticks on overload
    };
    esp_timer_create(&args, &s_ssrTimer);
    esp_timer_start_periodic(s_ssrTimer, SSR_TICK_US);
}

// ---------------------------------------------------------------------------
// ssrSetDuty
//   Sets the time-proportional duty cycle for one SSR.
//   ssr:     1–5  (maps to SSR1–SSR5 / PIN_SSR1–PIN_SSR5)
//   percent: 0.0–100.0 (clamped; rounded to nearest integer %)
// ---------------------------------------------------------------------------
void ssrSetDuty(int ssr, float percent) {
    if (ssr < 1 || ssr > SSR_COUNT) return;
    float clamped = constrain(percent, 0.0f, 100.0f);
    s_duty[ssr - 1] = (uint8_t)(clamped + 0.5f);    // round to nearest %
}

// ---------------------------------------------------------------------------
// ssrAllOff
//   Forces all 5 SSR outputs LOW immediately (does not wait for next tick).
//   Clears s_duty[] so the timer keeps them off.
//   Used for emergency stops, STOP command, and mode transitions.
// ---------------------------------------------------------------------------
void ssrAllOff() {
    if (s_ssrTimer) esp_timer_stop(s_ssrTimer);   // wait for any in-flight callback

    for (int i = 0; i < SSR_COUNT; i++) {
        s_duty[i] = 0;
        digitalWrite(SSR_PINS[i], LOW);
    }

    if (s_ssrTimer) esp_timer_start_periodic(s_ssrTimer, SSR_TICK_US);
}