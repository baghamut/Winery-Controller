// =============================================================================
//  state.h  –  Shared application state + NVS helpers
//
//  Single global AppState (g_state) is the only source of truth.
//  All tasks read/write it under the mutex (stateLock / stateUnlock).
//
//  MASTER POWER UNIFICATION:
//    ssrPower[] and ssrOn[] arrays are REMOVED.
//    Replaced by a single float  masterPower  (0–100 %).
//    When masterPower > 0 and the process is running, ALL SSRs for the
//    active mode receive exactly that duty cycle.
//    masterPower == 0  ⟹  all SSRs forced OFF immediately.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"   // FW_VERSION, safety defaults, MASTER_POWER_DEFAULT, VALVE_COUNT, etc.

// ---------------------------------------------------------------------------
// NVS helpers – implemented in state.cpp
// ---------------------------------------------------------------------------
void stateInit();           // Load NVS → g_state; call once from setup()
void stateSaveToNVS();      // Persist current g_state → NVS
void stateSaveSensorMapToNVS(); // Legacy stub – kept for link compatibility

// Safety defaults (fallbacks if not provided by config.h)
#ifndef SAFETY_TEMP_MAX_C
#define SAFETY_TEMP_MAX_C  95.0f
#endif
#ifndef SAFETY_PRESS_MAX_BAR
#define SAFETY_PRESS_MAX_BAR 3.0f
#endif

// Generic offline sentinel for non-temperature sensors
// (Temperature sensors use TEMP_OFFLINE_THRESH from config.h)
static constexpr float SENSOR_OFFLINE = -999.0f;

// ---------------------------------------------------------------------------
// Per-process sensor colour thresholds
//   Stored inside AppState and persisted in NVS.
//   Separate structs for Distillation and Rectification so both can be
//   configured independently without mode-switching.
// ---------------------------------------------------------------------------
struct SensorThresholds {
    float tempWarn[3];    // warn colour for sensors 1–3 (°C)
    float tempDanger[3];  // danger/flash colour for sensors 1–3 (°C)
    float pressWarn;      // pressure warn (bar)
    float pressDanger;    // pressure danger (bar)
};

// Factory helpers – initialise from compile-time config.h defaults
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
// Valve rule types  – defined BEFORE AppState so AppState can embed them
// ---------------------------------------------------------------------------

// Comparison operator for a valve condition.
// VALVE_OP_NONE means the condition is unset and will never trigger.
enum ValveOp : uint8_t {
    VALVE_OP_NONE = 0,   // unset – condition disabled
    VALVE_OP_GT   = 1,   // sensor >  value
    VALVE_OP_LT   = 2,   // sensor <  value
    VALVE_OP_GTE  = 3,   // sensor >= value
    VALVE_OP_LTE  = 4,   // sensor <= value
    VALVE_OP_EQ   = 5    // sensor == value  (within 0.001 float tolerance)
};

// One half of a valve rule (either the open or the close trigger).
// sensorId maps to RuleSensorId; 0 means unset.
struct ValveCondition {
    uint8_t sensorId = 0;             // RuleSensorId value; 0 = unset
    uint8_t op       = VALVE_OP_NONE; // ValveOp
    float   value    = 0.0f;          // threshold to compare against
};

// A full rule for one valve: separate conditions for opening and closing.
// Using distinct open/close thresholds gives a natural deadband that prevents
// chattering when a sensor reading oscillates near a single trip point.
struct ValveRule {
    ValveCondition openWhen;
    ValveCondition closeWhen;
};

// ---------------------------------------------------------------------------
// Global application state
// ---------------------------------------------------------------------------
struct AppState {
    // -----------------------------------------------------------------------
    // Core process state  (persisted in NVS)
    // -----------------------------------------------------------------------
    bool  isRunning   = false;  // true while a distillation/rectification run is active
    int   processMode = 0;      // 0 = Off, 1 = Distillation (SSR1–3), 2 = Rectification (SSR4–5)

    // -----------------------------------------------------------------------
    // MASTER POWER  (persisted in NVS)
    //   Single unified duty cycle for ALL active SSRs.
    //   Range: 0.0 – 100.0 %.
    //   Set via MASTER:NN.N command or the UI slider.
    //   When 0 %: all SSRs are OFF even if isRunning is true.
    //   SSRs turn ON automatically when masterPower > 0 and process is running.
    // -----------------------------------------------------------------------
    float masterPower = MASTER_POWER_DEFAULT;

    // -----------------------------------------------------------------------
    // Power-glitch auto-restore  (persisted in NVS)
    //   wasRunning:    was the process running at the last stateSaveToNVS()?
    //   lastTankTempC: Tank (t2) temperature saved during the last run tick.
    //   On boot, if wasRunning && tankDrop ≤ AUTO_RESTORE_MAX_TEMP_DROP_C,
    //   the process resumes automatically.
    // -----------------------------------------------------------------------
    bool   wasRunning    = false;
    float  lastTankTempC = 0.0f;

    // -----------------------------------------------------------------------
    // Live sensor readings  (NOT persisted – updated by sensorsTask)
    // -----------------------------------------------------------------------
    // Core temperature sensors (mapped by ROM address, slots 0–2)
    float roomTemp    = TEMP_OFFLINE_THRESH;   // Room / ambient
    float kettleTemp  = TEMP_OFFLINE_THRESH;   // Kettle / base of boiler
    float pillar1Temp = TEMP_OFFLINE_THRESH;   // Pillar lower section

    // Extended temperature sensors (slots 3–7, online when ROM assigned + present)
    float pillar2Temp  = TEMP_OFFLINE_THRESH;  // Pillar middle
    float pillar3Temp  = TEMP_OFFLINE_THRESH;  // Pillar upper
    float dephlegmTemp = TEMP_OFFLINE_THRESH;  // Dephlegmator head
    float refluxTemp   = TEMP_OFFLINE_THRESH;  // Reflux condenser exit
    float productTemp  = TEMP_OFFLINE_THRESH;  // Product cooler / condensate

    float pressureBar       = SENSOR_OFFLINE;  // Pillar pressure (bar)
    bool  levelHigh         = false;           // Kettle level OK (float switch)
    float flowRateLPM       = SENSOR_OFFLINE;  // Product flow (L/min)
    float totalVolumeLiters = 0.0f;            // Accumulated distillate volume

    // Extended sensors – online when hardware present and responding
    float pillarBaseBar  = SENSOR_OFFLINE;     // Pillar base pressure (ADS1115, future)
    bool  refluxLevel    = false;              // Reflux drum level (expander, future)
    float waterDephlLpm  = SENSOR_OFFLINE;     // Water flow – dephlegmator loop
    float waterCondLpm   = SENSOR_OFFLINE;     // Water flow – condenser loop
    float waterCoolerLpm = SENSOR_OFFLINE;     // Water flow – product cooler (future)

    // DS18B20 ROM address map (MAX_SENSORS slots, 8 bytes each).
    // All-zero bytes = unassigned slot.  Persisted in NVS as "rom0".."rom7".
    uint8_t tempSensorRom[MAX_SENSORS][8] = {};

    // -----------------------------------------------------------------------
    // Network info  (NOT persisted – set by wifiConnect() in .ino)
    // -----------------------------------------------------------------------
    String ip   = "0.0.0.0";
    String ssid = "";

    // -----------------------------------------------------------------------
    // Firmware / safety config  (partially persisted)
    // -----------------------------------------------------------------------
    String fw               = FW_VERSION;
    float  safetyTempMaxC   = SAFETY_TEMP_MAX_C;     // User-adjustable temperature ceiling
    float  safetyPresMaxBar = SAFETY_PRESS_MAX_BAR;  // User-adjustable pressure ceiling

    // -----------------------------------------------------------------------
    // Run timer  (informational only – NOT controlling the process)
    // -----------------------------------------------------------------------
    uint32_t timerSetSeconds = 0;    // Reserved; not currently used for process control
    uint32_t timerElapsedMs  = 0;    // Accumulated ms since START (reset on STOP)

    // -----------------------------------------------------------------------
    // Safety status  (NOT persisted – cleared on STOP or MODE:0)
    // -----------------------------------------------------------------------
    bool   safetyTripped = false;  // Latched true after automatic safety shutdown
    String safetyMessage = "";     // Human-readable reason (e.g. "Over temp")

    // -----------------------------------------------------------------------
    // Sensor colour-coding thresholds  (persisted in NVS, per process mode)
    // -----------------------------------------------------------------------
    SensorThresholds threshDist;   // Active when processMode == 1
    SensorThresholds threshRect;   // Active when processMode == 2

    // -----------------------------------------------------------------------
    // Valve subsystem  (VALVE_COUNT from config.h)
    //   valveRules: open/close conditions per valve – persisted in NVS
    //   valveOpen:  live hardware state – always false on cold boot,
    //               driven exclusively by valveEvaluateAll() in controlTask
    // -----------------------------------------------------------------------
    ValveRule valveRules[VALVE_COUNT];        // persisted; default = all VALVE_OP_NONE
    bool      valveOpen[VALVE_COUNT] = {};    // live; NOT persisted
};

// ---------------------------------------------------------------------------
// Global state instance (defined in state.cpp)
// ---------------------------------------------------------------------------
extern AppState g_state;

// ---------------------------------------------------------------------------
// Mutex helpers – always pair lock/unlock
//   Use stateLock() before touching g_state from any task or ISR.
//   Keep the locked section as short as possible (copy-snapshot pattern).
// ---------------------------------------------------------------------------
void stateLock();
void stateUnlock();

// ---------------------------------------------------------------------------
// Sensor catalog  (metadata only – no NVS persistence, no AppState changes)
//   Used by http_server.cpp to export ruleSensors[] in /state JSON, and by
//   future valve-rule UI to populate condition dropdowns.
// ---------------------------------------------------------------------------

// Logical sensor kinds – stored as uint8_t in RuleSensorDef.kind
enum SensorKind : uint8_t {
    SENSOR_KIND_UNKNOWN  = 0,
    SENSOR_KIND_TEMP     = 1,
    SENSOR_KIND_PRESSURE = 2,
    SENSOR_KIND_FLOW     = 3,
    SENSOR_KIND_LEVEL    = 4
};

// Stable IDs for each rule-eligible sensor input.
// Numeric values are stored in NVS (valve rules) – never change an existing value.
enum RuleSensorId : uint8_t {
    // Core sensors (IDs 1-6, NVS-stable)
    RULE_SENSOR_ROOM          = 1,
    RULE_SENSOR_KETTLE        = 2,   // was RULE_SENSOR_TANK
    RULE_SENSOR_PILLAR1       = 3,   // was RULE_SENSOR_PILLAR
    RULE_SENSOR_PRESSURE      = 4,
    RULE_SENSOR_FLOW          = 5,   // was RULE_SENSOR_FLOW_1 (Product Flow)
    RULE_SENSOR_LEVEL         = 6,
    // Extended temperature (IDs 7-11)
    RULE_SENSOR_PILLAR2       = 7,   // was PLACEHOLDER1
    RULE_SENSOR_PILLAR3       = 8,   // was PLACEHOLDER2
    RULE_SENSOR_DEPHLEGM      = 9,
    RULE_SENSOR_REFLUX        = 10,
    RULE_SENSOR_PRODUCT       = 11,
    // Extended pressure (ID 12, disabled – ADS1115 future)
    RULE_SENSOR_PILLAR_BASE   = 12,
    // Extended level (ID 13, disabled – future)
    RULE_SENSOR_REFLUX_LEVEL  = 13,
    // Extended flow (IDs 14-16)
    RULE_SENSOR_WATER_DEPHL   = 14,
    RULE_SENSOR_WATER_COND    = 15,
    RULE_SENSOR_WATER_COOLER  = 16,  // disabled – future
    // Optional temperature (ID 17, disabled – future)
    RULE_SENSOR_WATER_IN_TEMP = 17
};

// Descriptor for one rule-eligible sensor entry
struct RuleSensorDef {
    uint8_t     id;       // RuleSensorId value
    uint8_t     kind;     // SensorKind value
    const char* label;    // Display name (from ui_strings.h)
    const char* unit;     // Unit string (from ui_strings.h), "" if none
    bool        enabled;  // false = placeholder, not shown in dropdowns
};

constexpr size_t RULE_SENSOR_COUNT = 17;

// Defined in state.cpp; labels come from ui_strings.h
extern const RuleSensorDef g_ruleSensors[RULE_SENSOR_COUNT];
