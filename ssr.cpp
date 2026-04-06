// =============================================================================
//  ssr.cpp  –  SSR driver  (software time-proportional, replaces LEDC)
// =============================================================================
#include "ssr.h"
#include "config.h"
#include "esp_timer.h"
// >>> ADD – needed for portMUX_TYPE / taskENTER_CRITICAL
#include "freertos/portmacro.h"


// GPIO pin map: index 0 = SSR1 … index 4 = SSR5
static const uint8_t SSR_PINS[SSR_COUNT] = {
    PIN_SSR1,
    PIN_SSR2,
    PIN_SSR3,
    PIN_SSR4,
    PIN_SSR5
};


// Desired duty for each SSR: integer 0–100 (percent).
// Written by ssrSetDuty() / ssrAllOff() from controlTask (Core 0).
// Read inside ssrTimerCb() from the esp_timer service task.
// uint8_t stores are atomic on 32-bit Xtensa; volatile prevents caching.
static volatile uint8_t s_duty[SSR_COUNT] = {0, 0, 0, 0, 0};

// >>> ADD – place immediately after s_duty[], before s_tick
// Spinlock protecting s_duty[] writes (ssrSetDuty/ssrAllOff, Core 0 or Core 1)
// vs ssrTimerCb reads (esp_timer service task, any core).
// portMUX covers cross-core access; use portENTER_CRITICAL_ISR in the callback.
static portMUX_TYPE s_ssrMux = portMUX_INITIALIZER_UNLOCKED;
// >>> END ADD


// Current position within the period: 0 … SSR_TICKS_PER_PERIOD-1.
// Only touched inside ssrTimerCb() – no concurrent access.
static uint8_t s_tick = 0;


static esp_timer_handle_t s_ssrTimer = nullptr;


// ---------------------------------------------------------------------------
// ssrTimerCb
// ---------------------------------------------------------------------------
// >>> REPLACE: old signature was:  static void ssrTimerCb(void* /*arg*/) {
static void IRAM_ATTR ssrTimerCb(void* /*arg*/) {
// >>> END REPLACE
    if (++s_tick >= SSR_TICKS_PER_PERIOD) s_tick = 0;

    for (int i = 0; i < SSR_COUNT; i++) {
        // >>> REPLACE: old line was:  uint8_t d = s_duty[i];
        portENTER_CRITICAL_ISR(&s_ssrMux);
        uint8_t d = s_duty[i];
        portEXIT_CRITICAL_ISR(&s_ssrMux);
        // >>> END REPLACE

        bool on;
        if      (d == 0)   on = false;
        else if (d >= 100) on = true;
        else               on = (s_tick < (d * SSR_TICKS_PER_PERIOD / 100u));

        digitalWrite(SSR_PINS[i], on ? HIGH : LOW);
    }
}


// ---------------------------------------------------------------------------
// ssrInit  – unchanged
// ---------------------------------------------------------------------------
void ssrInit() {
    for (int i = 0; i < SSR_COUNT; i++) {
        pinMode(SSR_PINS[i], OUTPUT);
        digitalWrite(SSR_PINS[i], LOW);
    }

    const esp_timer_create_args_t args = {
        .callback              = ssrTimerCb,
        .arg                   = nullptr,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "ssrTimer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &s_ssrTimer);
    esp_timer_start_periodic(s_ssrTimer, SSR_TICK_US);
}


// ---------------------------------------------------------------------------
// ssrSetDuty
// ---------------------------------------------------------------------------
void ssrSetDuty(int ssr, float percent) {
    if (ssr < 1 || ssr > SSR_COUNT) return;
    float clamped = constrain(percent, 0.0f, 100.0f);
    // >>> REPLACE: old line was:  s_duty[ssr - 1] = (uint8_t)(clamped + 0.5f);
    uint8_t d = (uint8_t)(clamped + 0.5f);
    taskENTER_CRITICAL(&s_ssrMux);
    s_duty[ssr - 1] = d;
    taskEXIT_CRITICAL(&s_ssrMux);
    // >>> END REPLACE
}


// ---------------------------------------------------------------------------
// ssrAllOff
// ---------------------------------------------------------------------------
void ssrAllOff() {
    if (s_ssrTimer) esp_timer_stop(s_ssrTimer);   // drain any in-flight callback

    // >>> REPLACE: old was bare loop without lock
    taskENTER_CRITICAL(&s_ssrMux);
    for (int i = 0; i < SSR_COUNT; i++) {
        s_duty[i] = 0;
        digitalWrite(SSR_PINS[i], LOW);
    }
    s_tick = 0;   // clean period start when timer resumes
    taskEXIT_CRITICAL(&s_ssrMux);
    // >>> END REPLACE

    if (s_ssrTimer) esp_timer_start_periodic(s_ssrTimer, SSR_TICK_US);
}