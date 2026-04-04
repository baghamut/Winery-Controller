// =============================================================================
//  state.cpp  –  AppState implementation + NVS persistence
//
//  MASTER POWER UNIFICATION CHANGES:
//    • ssrOn[]/ssrPower[] arrays REMOVED from NVS load/save.
//    • masterPower (single float) persisted under NVS_KEY_MASTER_POWER.
//    • Auto-restore logic unchanged: wasRunning + lastTankTempC are kept.
// =============================================================================
#include "state.h"
#include "ui_strings.h"
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Global state instance – accessed by all tasks via stateLock/stateUnlock
// ---------------------------------------------------------------------------
AppState g_state;

// ---------------------------------------------------------------------------
// Rule sensor catalog  (read-only metadata, no NVS involvement)
//   Labels and units reference string constants from ui_strings.h.
//   IDs and kinds come from the enums declared in state.h.
// ---------------------------------------------------------------------------
const RuleSensorDef g_ruleSensors[RULE_SENSOR_COUNT] = {
    // Core temperature sensors
    { RULE_SENSOR_ROOM,         SENSOR_KIND_TEMP,     STR_RULE_SENSOR_ROOM,         STR_UNIT_DEGC, true  },
    { RULE_SENSOR_KETTLE,       SENSOR_KIND_TEMP,     STR_RULE_SENSOR_KETTLE,        STR_UNIT_DEGC, true  },
    { RULE_SENSOR_PILLAR1,      SENSOR_KIND_TEMP,     STR_RULE_SENSOR_PILLAR1,       STR_UNIT_DEGC, true  },
    // Extended temperature
    { RULE_SENSOR_PILLAR2,      SENSOR_KIND_TEMP,     STR_RULE_SENSOR_PILLAR2,       STR_UNIT_DEGC, true  },
    { RULE_SENSOR_PILLAR3,      SENSOR_KIND_TEMP,     STR_RULE_SENSOR_PILLAR3,       STR_UNIT_DEGC, true  },
    { RULE_SENSOR_DEPHLEGM,     SENSOR_KIND_TEMP,     STR_RULE_SENSOR_DEPHLEGM,      STR_UNIT_DEGC, true  },
    { RULE_SENSOR_REFLUX,       SENSOR_KIND_TEMP,     STR_RULE_SENSOR_REFLUX,        STR_UNIT_DEGC, true  },
    { RULE_SENSOR_PRODUCT,      SENSOR_KIND_TEMP,     STR_RULE_SENSOR_PRODUCT,       STR_UNIT_DEGC, true  },
    // Core pressure
    { RULE_SENSOR_PRESSURE,     SENSOR_KIND_PRESSURE, STR_RULE_SENSOR_PRESSURE,      STR_UNIT_BAR,  true  },
    // Extended pressure (disabled – ADS1115 not yet fitted)
    { RULE_SENSOR_PILLAR_BASE,  SENSOR_KIND_PRESSURE, STR_RULE_SENSOR_PILLAR_BASE,   STR_UNIT_BAR,  false },
    // Level sensors
    { RULE_SENSOR_LEVEL,        SENSOR_KIND_LEVEL,    STR_RULE_SENSOR_LEVEL,         "",            true  },
    { RULE_SENSOR_REFLUX_LEVEL, SENSOR_KIND_LEVEL,    STR_RULE_SENSOR_REFLUX_LEVEL,  "",            false },
    // Flow sensors
    { RULE_SENSOR_FLOW,         SENSOR_KIND_FLOW,     STR_RULE_SENSOR_FLOW,          STR_UNIT_LPM,  true  },
    { RULE_SENSOR_WATER_DEPHL,  SENSOR_KIND_FLOW,     STR_RULE_SENSOR_WATER_DEPHL,   STR_UNIT_LPM,  true  },
    { RULE_SENSOR_WATER_COND,   SENSOR_KIND_FLOW,     STR_RULE_SENSOR_WATER_COND,    STR_UNIT_LPM,  true  },
    { RULE_SENSOR_WATER_COOLER, SENSOR_KIND_FLOW,     STR_RULE_SENSOR_WATER_COOLER,  STR_UNIT_LPM,  false },
    // Optional temperature placeholder
    { RULE_SENSOR_WATER_IN_TEMP,SENSOR_KIND_TEMP,     STR_RULE_SENSOR_WATER_IN_TEMP, STR_UNIT_DEGC, false },
};

// ---------------------------------------------------------------------------
// FreeRTOS mutex
//   Created once in stateInit() and used for all g_state access.
//   Never block with portMAX_DELAY inside an ISR.
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_mutex = nullptr;

// Internal forward declaration
static void stateLoadFromNVS();

// ---------------------------------------------------------------------------
// stateInit
//   Must be called once from setup() before any task is started.
//   Initialises the mutex, applies compile-time threshold defaults,
//   then loads whatever was previously saved in NVS.
// ---------------------------------------------------------------------------
void stateInit() {
    s_mutex = xSemaphoreCreateMutex();

    // Set firmware version from config.h
    g_state.fw = FW_VERSION;

    // Populate sensor thresholds from compile-time config.h defaults
    // (may be overridden by NVS load below)
    g_state.threshDist = makeDistThresholds();
    g_state.threshRect = makeRectThresholds();

    stateLoadFromNVS();
}

// ---------------------------------------------------------------------------
// stateLock / stateUnlock – mutex wrappers
// ---------------------------------------------------------------------------
void stateLock() {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void stateUnlock() {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

// ---------------------------------------------------------------------------
// stateLoadFromNVS (internal)
//   Reads persisted values into g_state.
//   If the VALID flag is missing (first boot, or NVS erased) → keep defaults.
// ---------------------------------------------------------------------------
static void stateLoadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;   // open read-only

    // VALID flag: if not set, this is the first boot → keep compile defaults
    uint8_t valid = prefs.getUChar(NVS_KEY_VALID, 0);
    if (valid != 1) {
        prefs.end();
        return;
    }

    // --- Process mode ---
    g_state.processMode = (int)prefs.getInt(NVS_KEY_PMODE, 0);

    // Safety: never auto-start on boot.
    // isRunning is always false after a cold boot; auto-restore logic in
    // setup() (DistillController.ino) decides whether to set it to true.
    g_state.isRunning = false;

    // --- Master Power ---
    // Restore the last-used power level so the operator sees the same
    // value they left before power loss. The process itself does NOT
    // restart until the user presses START (or auto-restore triggers).
    g_state.masterPower = prefs.getFloat(NVS_KEY_MASTER_POWER, MASTER_POWER_DEFAULT);
    // Clamp to valid range in case of NVS corruption
    if (g_state.masterPower < 0.0f)   g_state.masterPower = 0.0f;
    if (g_state.masterPower > 100.0f) g_state.masterPower = 100.0f;

    // --- Power-glitch auto-restore data ---
    // wasRunning: was the process running when power was lost?
    // lastTankTempC: tank temperature at that point.
    // These two fields together allow the auto-restore check in setup().
    g_state.wasRunning    = prefs.getBool("wasRunning", false);
    g_state.lastTankTempC = prefs.getFloat("lastTankC",  0.0f);

    // --- Safety limits (user-configurable, persist across reboots) ---
    g_state.safetyTempMaxC   = prefs.getFloat(NVS_KEY_TEMP_MAX, SAFETY_TEMP_MAX_C);
    g_state.safetyPresMaxBar = prefs.getFloat(NVS_KEY_PRES_MAX, SAFETY_PRESS_MAX_BAR);

    // --- Sensor colour thresholds ---
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

    // --- Valve rules ---
    // valveOpen[] is intentionally NOT restored – valves always start closed
    // on boot for safety; the evaluator loop will open them if conditions are met.
    for (int i = 0; i < VALVE_COUNT; i++) {
        char k[6];
        snprintf(k, sizeof(k), "v%dos", i);
        g_state.valveRules[i].openWhen.sensorId = prefs.getUChar(k, 0);
        snprintf(k, sizeof(k), "v%doo", i);
        g_state.valveRules[i].openWhen.op       = prefs.getUChar(k, VALVE_OP_NONE);
        snprintf(k, sizeof(k), "v%dov", i);
        g_state.valveRules[i].openWhen.value    = prefs.getFloat(k, 0.0f);

        snprintf(k, sizeof(k), "v%dcs", i);
        g_state.valveRules[i].closeWhen.sensorId = prefs.getUChar(k, 0);
        snprintf(k, sizeof(k), "v%dco", i);
        g_state.valveRules[i].closeWhen.op       = prefs.getUChar(k, VALVE_OP_NONE);
        snprintf(k, sizeof(k), "v%dcv", i);
        g_state.valveRules[i].closeWhen.value    = prefs.getFloat(k, 0.0f);

        g_state.valveOpen[i] = false;   // always start closed
    }

    // --- DS18B20 ROM address map ---
    // Each slot is 8 bytes; all-zero = unassigned.  Loaded on every boot so
    // sensorsTask can immediately address sensors by ROM without a scan.
    for (int i = 0; i < MAX_SENSORS; i++) {
        char k[6];
        snprintf(k, sizeof(k), "rom%d", i);
        prefs.getBytes(k, g_state.tempSensorRom[i], 8);
    }

    prefs.end();
}
//   Persists current g_state to NVS.
//   Called whenever significant state changes (mode, masterPower, START, STOP,
//   threshold edits, safety config changes).
// ---------------------------------------------------------------------------
void stateSaveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;   // open read-write

    // Mark NVS as containing valid data
    prefs.putUChar(NVS_KEY_VALID, 1);

    // Process mode
    prefs.putInt(NVS_KEY_PMODE, g_state.processMode);

    // Master power – single float replaces the old ssrPower[] / ssrOn[] arrays
    prefs.putFloat(NVS_KEY_MASTER_POWER, g_state.masterPower);

    // Safety limits
    prefs.putFloat(NVS_KEY_TEMP_MAX, g_state.safetyTempMaxC);
    prefs.putFloat(NVS_KEY_PRES_MAX, g_state.safetyPresMaxBar);

    // Auto-restore fields:
    //   wasRunning → true while running so a short power blip triggers restore
    //   lastTankC  → continuously updated during a run (see control.cpp)
    prefs.putBool("wasRunning", g_state.isRunning);
    // Only overwrite lastTankC with a valid temperature so a sensor-offline
    // state never corrupts the auto-restore reference temperature.
    if (g_state.kettleTemp > TEMP_OFFLINE_THRESH) {
        prefs.putFloat("lastTankC", g_state.kettleTemp);
    } else if (g_state.lastTankTempC > TEMP_OFFLINE_THRESH) {
        prefs.putFloat("lastTankC", g_state.lastTankTempC);  // keep last known good
    }

    // Sensor colour thresholds
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

    // --- DS18B20 ROM address map ---
    for (int i = 0; i < MAX_SENSORS; i++) {
        char k[6];
        snprintf(k, sizeof(k), "rom%d", i);
        prefs.putBytes(k, g_state.tempSensorRom[i], 8);
    }

    // --- Valve rules ---
    // valveOpen[] is NOT persisted – valves always start closed on next boot.
    for (int i = 0; i < VALVE_COUNT; i++) {
        char k[6];
        snprintf(k, sizeof(k), "v%dos", i);
        prefs.putUChar(k, g_state.valveRules[i].openWhen.sensorId);
        snprintf(k, sizeof(k), "v%doo", i);
        prefs.putUChar(k, g_state.valveRules[i].openWhen.op);
        snprintf(k, sizeof(k), "v%dov", i);
        prefs.putFloat(k, g_state.valveRules[i].openWhen.value);

        snprintf(k, sizeof(k), "v%dcs", i);
        prefs.putUChar(k, g_state.valveRules[i].closeWhen.sensorId);
        snprintf(k, sizeof(k), "v%dco", i);
        prefs.putUChar(k, g_state.valveRules[i].closeWhen.op);
        snprintf(k, sizeof(k), "v%dcv", i);
        prefs.putFloat(k, g_state.valveRules[i].closeWhen.value);
    }

    prefs.end();
}

// ---------------------------------------------------------------------------
// stateSaveSensorMapToNVS – legacy stub
//   Kept so any compiled-in code that calls it doesn't generate a linker error.
//   The sensor mapping concept was removed; this is intentionally a no-op.
// ---------------------------------------------------------------------------
void stateSaveSensorMapToNVS() {
    // No-op – sensor mapping removed in current architecture
}
