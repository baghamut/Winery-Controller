// =============================================================================
//  ssr.cpp  –  SSR driver
//  ESP32 Arduino core v3: pin-based LEDC API
//    ledcAttach(pin, freq, resolution)  – replaces ledcSetup + ledcAttachPin
//    ledcWrite(pin, duty)               – replaces ledcWrite(channel, duty)
// =============================================================================
#include "ssr.h"
#include "config.h"

static const uint8_t SSR_PINS[SSR_COUNT] = {
    PIN_SSR1, PIN_SSR2, PIN_SSR3, PIN_SSR4, PIN_SSR5
};

// ---------------------------------------------------------------------------
// Init all SSR outputs
// ---------------------------------------------------------------------------
void ssrInit() {
    for (int i = 0; i < SSR_COUNT; i++) {
        ledcAttach(SSR_PINS[i], SSR_LEDC_FREQ_HZ, SSR_LEDC_RESOLUTION);
        ledcWrite(SSR_PINS[i], 0);  // OFF at boot
    }
}

// ---------------------------------------------------------------------------
// Set duty for a given SSR (1–SSR_COUNT) in percent 0–100
// ---------------------------------------------------------------------------
void ssrSetDuty(int ssr, float percent) {
    if (ssr < 1 || ssr > SSR_COUNT) return;

    float    clamped = constrain(percent, 0.0f, 100.0f);
    uint32_t maxDuty = (1u << SSR_LEDC_RESOLUTION) - 1u;
    uint32_t duty    = (uint32_t)((clamped / 100.0f) * (float)maxDuty);

    ledcWrite(SSR_PINS[ssr - 1], duty);
}

// ---------------------------------------------------------------------------
// Turn all SSRs off
// ---------------------------------------------------------------------------
void ssrAllOff() {
    for (int i = 0; i < SSR_COUNT; i++) {
        ledcWrite(SSR_PINS[i], 0);
    }
}