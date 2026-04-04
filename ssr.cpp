// =============================================================================
//  ssr.cpp  –  SSR driver
//
//  Uses the ESP32 Arduino core v3 pin-based LEDC API:
//    ledcAttach(pin, freq, resolution) – replaces old ledcSetup + ledcAttachPin
//    ledcWrite(pin, duty)              – replaces old ledcWrite(channel, duty)
//
//  This file is a pure hardware abstraction layer.
//  It knows NOTHING about processMode or masterPower.
//  All policy (which SSRs run, at what duty) is in control.cpp.
// =============================================================================
#include "ssr.h"
#include "config.h"

// Map SSR number (1-based index) to GPIO pin.
// Order MUST match PIN_SSR1…PIN_SSR5 in config.h.
static const uint8_t SSR_PINS[SSR_COUNT] = {
    PIN_SSR1,   // SSR1 – Distillation heater A
    PIN_SSR2,   // SSR2 – Distillation heater B
    PIN_SSR3,   // SSR3 – Distillation heater C
    PIN_SSR4,   // SSR4 – Rectification heater A
    PIN_SSR5    // SSR5 – Rectification heater B
};

// ---------------------------------------------------------------------------
// ssrInit
//   Attach all SSR pins to the LEDC peripheral and set duty = 0.
//   ledcAttach(pin, freq, resolution):
//     • freq = SSR_LEDC_FREQ_HZ  (10 Hz – appropriate for resistive heaters)
//     • resolution = SSR_LEDC_RESOLUTION  (8-bit → duty range 0–255)
// ---------------------------------------------------------------------------
void ssrInit() {
    for (int i = 0; i < SSR_COUNT; i++) {
        // Configure pin as PWM output on the LEDC peripheral
        ledcAttach(SSR_PINS[i], SSR_LEDC_FREQ_HZ, SSR_LEDC_RESOLUTION);
        // Start fully off – never energise a heater at boot without explicit
        // user action (or a valid auto-restore check)
        ledcWrite(SSR_PINS[i], 0);
    }
}

// ---------------------------------------------------------------------------
// ssrSetDuty
//   Converts a percentage (0–100) to a LEDC duty count and writes it.
//   The clamping ensures hardware safety even if the caller passes bad values.
// ---------------------------------------------------------------------------
void ssrSetDuty(int ssr, float percent) {
    if (ssr < 1 || ssr > SSR_COUNT) return;    // ignore out-of-range SSR number

    // Clamp to valid range before any arithmetic
    float clamped = constrain(percent, 0.0f, 100.0f);

    // Convert percentage to duty count.
    // maxDuty = (2^resolution - 1) = 255 for 8-bit.
    uint32_t maxDuty = (1u << SSR_LEDC_RESOLUTION) - 1u;
    uint32_t duty    = (uint32_t)((clamped / 100.0f) * (float)maxDuty);

    ledcWrite(SSR_PINS[ssr - 1], duty);
}

// ---------------------------------------------------------------------------
// ssrAllOff
//   Writes duty = 0 to every SSR pin directly.
//   Used for emergency stops, STOP command, and safe shutdown.
//   Bypasses percentage conversion for speed.
// ---------------------------------------------------------------------------
void ssrAllOff() {
    for (int i = 0; i < SSR_COUNT; i++) {
        ledcWrite(SSR_PINS[i], 0);
    }
}
