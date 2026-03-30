// =============================================================================
//  state.h  –  Shared application state + NVS helpers
// =============================================================================
#pragma once
#include <Arduino.h>
#include <vector>
#include "config.h"   // for FW_VERSION, safety defaults if defined

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
void stateInit();
void stateSaveToNVS();
void stateSaveSensorMapToNVS(); // keep for compatibility (can be empty)

// Safety defaults (fallbacks if not provided by config.h)
#ifndef SAFETY_TEMP_MAX_C
#define SAFETY_TEMP_MAX_C  95.0f
#endif

#ifndef SAFETY_PRESS_MAX_BAR
#define SAFETY_PRESS_MAX_BAR 3.0f
#endif

// Generic offline sentinel for non-temperature sensors
// (Temps already use TEMP_OFFLINE_THRESH.)
static constexpr float SENSOR_OFFLINE = -999.0f;

// ---------------------------------------------------------------------------
// Per-process sensor color thresholds
// ---------------------------------------------------------------------------
struct SensorThresholds {
    float tempWarn[3];
    float tempDanger[3];
    float pressWarn;
    float pressDanger;
};

// Default-initialised instances (used to populate AppState on first boot)
inline SensorThresholds makeDistThresholds() {
    SensorThresholds t;
    t.tempWarn[0]   = THRESH_D_TW0; t.tempDanger[0] = THRESH_D_TD0;
    t.tempWarn[1]   = THRESH_D_TW1; t.tempDanger[1] = THRESH_D_TD1;
    t.tempWarn[2]   = THRESH_D_TW2; t.tempDanger[2] = THRESH_D_TD2;
    t.pressWarn     = THRESH_D_PW;
    t.pressDanger   = THRESH_D_PD;
    return t;
}
inline SensorThresholds makeRectThresholds() {
    SensorThresholds t;
    t.tempWarn[0]   = THRESH_R_TW0; t.tempDanger[0] = THRESH_R_TD0;
    t.tempWarn[1]   = THRESH_R_TW1; t.tempDanger[1] = THRESH_R_TD1;
    t.tempWarn[2]   = THRESH_R_TW2; t.tempDanger[2] = THRESH_R_TD2;
    t.pressWarn     = THRESH_R_PW;
    t.pressDanger   = THRESH_R_PD;
    return t;
}

// ---------------------------------------------------------------------------
// Global application state
// ---------------------------------------------------------------------------
struct AppState {
    // Core process state (persisted)
    bool  isRunning    = false;  // true while process is actively running
    int   processMode  = 0;      // 0 = Off, 1 = Distillation, 2 = Rectification

    // Per-SSR configuration (persisted)
    // Indexes: 0..4 → SSR1..SSR5
    float ssrPower[5]  = { 0, 0, 0, 0, 0 };   // power in percent 0–100
    bool  ssrOn[5]     = { false, false, false, false, false };

    // Measurements (NOT persisted; updated by sensors task)
    float t1 = -200.0f;
    float t2 = -200.0f;
    float t3 = -200.0f;

    float pressureBar        = SENSOR_OFFLINE; // offline by default
    bool  levelHigh          = false;
    float flowRateLPM        = SENSOR_OFFLINE; // offline by default
    float totalVolumeLiters  = 0.0f;

    // Network info (NOT persisted; updated from Wi‑Fi helper)
    String ip   = "0.0.0.0";
    String ssid = "";

    // Firmware / safety (persisted if you wish)
    String fw               = FW_VERSION;
    float  safetyTempMaxC   = SAFETY_TEMP_MAX_C;     // user‑adjustable max temp
    float  safetyPresMaxBar = SAFETY_PRESS_MAX_BAR;

    // Timer (run time info only; not controlling process anymore)
    uint32_t timerSetSeconds = 0;
    uint32_t timerElapsedMs  = 0;

    // Safety status
    bool   safetyTripped     = false;     // true after any safety shutoff
    String safetyMessage     = "";        // short human-readable reason

    // Sensor color-coding thresholds (persisted, per process)
    SensorThresholds threshDist;   // used when processMode == 1
    SensorThresholds threshRect;   // used when processMode == 2
};

// Global state + mutex
extern AppState g_state;

void stateLock();
void stateUnlock();