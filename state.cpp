// =============================================================================
//  state.cpp  –  AppState implementation + NVS persistence
// =============================================================================
#include "state.h"
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Global state instance
// ---------------------------------------------------------------------------
AppState g_state;

// ---------------------------------------------------------------------------
// Mutex
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_mutex = nullptr;

// Internal forward
static void stateLoadFromNVS();

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void stateInit() {
    s_mutex = xSemaphoreCreateMutex();
    g_state.fw = FW_VERSION;

    // Apply compile-time defaults for thresholds before loading NVS
    g_state.threshDist = makeDistThresholds();
    g_state.threshRect = makeRectThresholds();

    stateLoadFromNVS();
}

void stateLock() {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void stateUnlock() {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------

// Expected keys (define in config.h or here)
#ifndef NVS_NAMESPACE
#define NVS_NAMESPACE     "distill"
#endif
#ifndef NVS_KEY_VALID
#define NVS_KEY_VALID     "valid"
#endif
#ifndef NVS_KEY_PMODE
#define NVS_KEY_PMODE     "pmode"
#endif
#ifndef NVS_KEY_TEMP_MAX
#define NVS_KEY_TEMP_MAX  "tmax"
#endif
#ifndef NVS_KEY_PRES_MAX
#define NVS_KEY_PRES_MAX  "pmax"
#endif
// ssrOn[i]  → "on0".."on4"
// ssrPower[i] → "pwr0".."pwr4"

static void stateLoadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;   // read-only

    // If VALID flag is not set, keep defaults
    uint8_t valid = prefs.getUChar(NVS_KEY_VALID, 0);
    if (valid != 1) {
        prefs.end();
        return;
    }

    g_state.processMode = (int)prefs.getInt(NVS_KEY_PMODE, 0);
    // Never auto-run on boot – force stopped, user must press START
    g_state.isRunning = false;

    g_state.safetyTempMaxC   = prefs.getFloat(NVS_KEY_TEMP_MAX, SAFETY_TEMP_MAX_C);
    g_state.safetyPresMaxBar = prefs.getFloat(NVS_KEY_PRES_MAX, SAFETY_PRESS_MAX_BAR);

    for (int i = 0; i < 5; ++i) {
        char keyOn[8], keyP[8];
        snprintf(keyOn, sizeof(keyOn), "on%d",  i);
        snprintf(keyP,  sizeof(keyP),  "pwr%d", i);
        g_state.ssrOn[i]    = prefs.getBool(keyOn, false);
        g_state.ssrPower[i] = prefs.getFloat(keyP,  0.0f);
    }

    // Load sensor color thresholds  (keys: tw_d0..2, td_d0..2, pw_d, pd_d, etc.)
    for (int i = 0; i < 3; ++i) {
        char kw[8], kd[8];
        snprintf(kw, sizeof(kw), "tw_d%d", i);
        snprintf(kd, sizeof(kd), "td_d%d", i);
        g_state.threshDist.tempWarn[i]   = prefs.getFloat(kw, g_state.threshDist.tempWarn[i]);
        g_state.threshDist.tempDanger[i] = prefs.getFloat(kd, g_state.threshDist.tempDanger[i]);
        snprintf(kw, sizeof(kw), "tw_r%d", i);
        snprintf(kd, sizeof(kd), "td_r%d", i);
        g_state.threshRect.tempWarn[i]   = prefs.getFloat(kw, g_state.threshRect.tempWarn[i]);
        g_state.threshRect.tempDanger[i] = prefs.getFloat(kd, g_state.threshRect.tempDanger[i]);
    }
    g_state.threshDist.pressWarn    = prefs.getFloat("pw_d", g_state.threshDist.pressWarn);
    g_state.threshDist.pressDanger  = prefs.getFloat("pd_d", g_state.threshDist.pressDanger);
    g_state.threshRect.pressWarn    = prefs.getFloat("pw_r", g_state.threshRect.pressWarn);
    g_state.threshRect.pressDanger  = prefs.getFloat("pd_r", g_state.threshRect.pressDanger);

    prefs.end();
}

void stateSaveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;  // read-write

    prefs.putUChar(NVS_KEY_VALID, 1);
    prefs.putInt(NVS_KEY_PMODE,   g_state.processMode);
    prefs.putFloat(NVS_KEY_TEMP_MAX, g_state.safetyTempMaxC);
    prefs.putFloat(NVS_KEY_PRES_MAX, g_state.safetyPresMaxBar);

    for (int i = 0; i < 5; ++i) {
        char keyOn[8], keyP[8];
        snprintf(keyOn, sizeof(keyOn), "on%d",  i);
        snprintf(keyP,  sizeof(keyP),  "pwr%d", i);
        prefs.putBool(keyOn,  g_state.ssrOn[i]);
        prefs.putFloat(keyP,  g_state.ssrPower[i]);
    }

    // Save sensor color thresholds
    for (int i = 0; i < 3; ++i) {
        char kw[8], kd[8];
        snprintf(kw, sizeof(kw), "tw_d%d", i);
        snprintf(kd, sizeof(kd), "td_d%d", i);
        prefs.putFloat(kw, g_state.threshDist.tempWarn[i]);
        prefs.putFloat(kd, g_state.threshDist.tempDanger[i]);
        snprintf(kw, sizeof(kw), "tw_r%d", i);
        snprintf(kd, sizeof(kd), "td_r%d", i);
        prefs.putFloat(kw, g_state.threshRect.tempWarn[i]);
        prefs.putFloat(kd, g_state.threshRect.tempDanger[i]);
    }
    prefs.putFloat("pw_d", g_state.threshDist.pressWarn);
    prefs.putFloat("pd_d", g_state.threshDist.pressDanger);
    prefs.putFloat("pw_r", g_state.threshRect.pressWarn);
    prefs.putFloat("pd_r", g_state.threshRect.pressDanger);

    prefs.end();
}

// Kept for compatibility; does nothing in simplified model
void stateSaveSensorMapToNVS() {
    // No-op
}