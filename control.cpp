// =============================================================================
//  control.cpp  –  Process control: state machine, safety, SSR output
//
//  MASTER POWER UNIFICATION:
//    All active SSRs for the selected process mode receive the SAME duty
//    cycle: g_state.masterPower (0–100 %).
//
//    Distillation (mode 1)  → SSR1, SSR2, SSR3  all get masterPower %
//    Rectification (mode 2) → SSR4, SSR5         all get masterPower %
//    Off (mode 0)           → all SSRs at 0 %
//
//    SSRs turn ON automatically when masterPower > 0 and the process is
//    running. No separate per-SSR on/off toggle is needed.
//
//    REMOVED COMMANDS: SSR:N:ON/OFF, SSR:N:PWR:NN
//    NEW COMMAND:       MASTER:NN.N
// =============================================================================
#include "control.h"
#include "config.h"
#include "state.h"
#include "ssr.h"
#include "expander.h"
#include "sensors.h"   // romToHex, hexToRom
#include <math.h>

// wifiApplyConfig is implemented in DistillController.ino with C linkage
// so it can be called from this C++ translation unit.
extern "C" void wifiApplyConfig(const char* ssid, const char* pass);

// Kept for future PI control. Not used in power-driven mode.
static float s_piIntegral = 0.0f;

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

static String urlDecodeString(const String& in) {
    String out;
    out.reserve(in.length());

    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];

        if (c == '%' && i + 2 < in.length()) {
            int hi = hexNibble(in[i + 1]);
            int lo = hexNibble(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += char((hi << 4) | lo);
                i += 2;
                continue;
            }
        }

        if (c == '+') {
            out += ' ';
        } else {
            out += c;
        }
    }

    return out;
}

// Small hysteresis band around safetyTempMaxC to avoid rapid on/off cycling
// near the threshold (prevents relay chatter and log spam).
#ifndef SAFETY_TEMP_HYST_C
#define SAFETY_TEMP_HYST_C 1.0f
#endif


// ---------------------------------------------------------------------------
// controlInit
//   Reset internal state. Called once from setup().
// ---------------------------------------------------------------------------
void controlInit() {
    s_piIntegral = 0.0f;
}


// ---------------------------------------------------------------------------
// controlSafetyCheck
//   Returns true if it is safe to continue operating.
//   Returns false and latches g_state.safetyTripped if any sensor exceeds
//   its per-sensor danger threshold, pressure exceeds pressDanger, or any
//   temperature exceeds the global safetyTempMaxC ceiling.
//
//   Only checks when the process is actually running – avoids false trips
//   during sensor warm-up or while the device is idle.
// ---------------------------------------------------------------------------
bool controlSafetyCheck() {
    stateLock();
    AppState snap = g_state;   // snapshot for lock-free comparison below
    stateUnlock();

    // Nothing to check when the process is stopped
    if (!snap.isRunning) return true;

    // Select thresholds for the active mode
    const SensorThresholds& thr = (snap.processMode == 2)
        ? snap.threshRect : snap.threshDist;

    // --- Per-sensor danger check ---
    // Each sensor is checked against its own configured limit.
    // Offline sensors (≤ TEMP_OFFLINE_THRESH) are skipped individually.
    float temps[3] = { snap.roomTemp, snap.kettleTemp, snap.pillar1Temp };
    for (int i = 0; i < 3; i++) {
        if (temps[i] <= TEMP_OFFLINE_THRESH) continue;
        if (temps[i] >= thr.tempDanger[i] + SAFETY_TEMP_HYST_C) {
            char msg[32];
            snprintf(msg, sizeof(msg), "T%d over limit %.1fC", i + 1, temps[i]);
            stateLock();
            g_state.safetyTripped = true;
            g_state.safetyMessage = msg;
            stateUnlock();
            return false;
        }
    }

    // Extended temperature sensors – checked against global safetyTempMaxC ceiling only
    float extTemps[] = { snap.pillar2Temp, snap.pillar3Temp,
                         snap.dephlegmTemp, snap.refluxTemp, snap.productTemp };
    for (float t : extTemps) {
        if (t <= TEMP_OFFLINE_THRESH) continue;
        if (t >= snap.safetyTempMaxC + SAFETY_TEMP_HYST_C) {
            stateLock();
            g_state.safetyTripped = true;
            g_state.safetyMessage = "Ext sensor over temp";
            stateUnlock();
            return false;
        }
    }

    // --- Pressure danger check ---
    // Offline sentinel is SENSOR_OFFLINE (-999). Use +1 margin to avoid
    // float equality issues while still catching any real reading.
    if (snap.pressureBar > SENSOR_OFFLINE + 1.0f &&
        snap.pressureBar >= thr.pressDanger) {
        stateLock();
        g_state.safetyTripped = true;
        g_state.safetyMessage = "Over pressure";
        stateUnlock();
        return false;
    }

    // --- Global temperature backstop ---
    // safetyTempMaxC is a hard ceiling regardless of per-sensor settings.
    // Only evaluated when at least one sensor is online.
    float tMax = snap.roomTemp;
    const float allT[] = { snap.kettleTemp, snap.pillar1Temp,
                           snap.pillar2Temp, snap.pillar3Temp,
                           snap.dephlegmTemp, snap.refluxTemp, snap.productTemp };
    for (float t : allT) if (t > TEMP_OFFLINE_THRESH && t > tMax) tMax = t;
    if (tMax > TEMP_OFFLINE_THRESH &&
        tMax >= snap.safetyTempMaxC + SAFETY_TEMP_HYST_C) {
        stateLock();
        g_state.safetyTripped = true;
        g_state.safetyMessage = "Over temp (global)";
        stateUnlock();
        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------
// applySsrFromState  (internal)
//   Reads g_state and writes the correct PWM duty to every SSR.
//
//   Rules:
//     1. If not running OR processMode == 0 → all SSRs at 0 %.
//     2. If running in mode 1 (Distillation): SSR1–3 get masterPower %.
//        SSR4–5 stay at 0 %.
//     3. If running in mode 2 (Rectification): SSR4–5 get masterPower %.
//        SSR1–3 stay at 0 %.
//     4. masterPower == 0 → all SSRs at 0 % (treated as "heaters off").
//
//   Also keeps lastTankTempC continuously updated while running so that a
//   sudden power loss records the most recent known tank temperature, giving
//   the auto-restore check the best possible data.
// ---------------------------------------------------------------------------
 void applySsrFromState() {
    stateLock();
    AppState snap = g_state;
    stateUnlock();

    // Continuously update lastTankTempC during a run so power-glitch recovery
    // always has the freshest tank temperature in NVS after stateSaveToNVS().
    if (snap.isRunning && snap.kettleTemp > TEMP_OFFLINE_THRESH) {
        stateLock();
        g_state.lastTankTempC = snap.kettleTemp;
        stateUnlock();
    }

    // If not running or no mode selected → safe-off all SSRs
    if (!snap.isRunning || snap.processMode == 0) {
        ssrAllOff();
        return;
    }

    int   pm    = snap.processMode;
    float power = snap.masterPower;  // 0–100 % (same duty for all active SSRs)

    for (int i = 0; i < SSR_COUNT; ++i) {
        // Determine whether this SSR is in the active group for the current mode
        bool allowed = false;
        if (pm == 1 && i <= 2)            allowed = true;  // Distillation: SSR1–3
        if (pm == 2 && (i == 3 || i == 4)) allowed = true; // Rectification: SSR4–5

        // All allowed SSRs receive the same masterPower duty cycle.
        // SSRs outside the active group are always 0 % for safety.
        float duty = (allowed && power > 0.0f) ? power : 0.0f;
        ssrSetDuty(i + 1, duty);
    }
}


// ---------------------------------------------------------------------------
// Valve subsystem helpers
// ---------------------------------------------------------------------------

// Read the live sensor value for a given RuleSensorId from a state snapshot.
// Level sensor is mapped to 1.0 (OK) / 0.0 (LOW) so numeric operators work.
static float valveSensorValue(const AppState& snap, uint8_t sensorId) {
    switch (sensorId) {
        case RULE_SENSOR_ROOM:          return snap.roomTemp;
        case RULE_SENSOR_KETTLE:        return snap.kettleTemp;
        case RULE_SENSOR_PILLAR1:       return snap.pillar1Temp;
        case RULE_SENSOR_PILLAR2:       return snap.pillar2Temp;
        case RULE_SENSOR_PILLAR3:       return snap.pillar3Temp;
        case RULE_SENSOR_DEPHLEGM:      return snap.dephlegmTemp;
        case RULE_SENSOR_REFLUX:        return snap.refluxTemp;
        case RULE_SENSOR_PRODUCT:       return snap.productTemp;
        case RULE_SENSOR_PRESSURE:      return snap.pressureBar;
        case RULE_SENSOR_PILLAR_BASE:   return snap.pillarBaseBar;
        case RULE_SENSOR_LEVEL:         return snap.levelHigh  ? 1.0f : 0.0f;
        case RULE_SENSOR_REFLUX_LEVEL:  return snap.refluxLevel ? 1.0f : 0.0f;
        case RULE_SENSOR_FLOW:          return snap.flowRateLPM;
        case RULE_SENSOR_WATER_DEPHL:   return snap.waterDephlLpm;
        case RULE_SENSOR_WATER_COND:    return snap.waterCondLpm;
        case RULE_SENSOR_WATER_COOLER:  return snap.waterCoolerLpm;
        default:                         return SENSOR_OFFLINE;
    }
}

// Return true if the sensor value should be treated as "no reading" and the
// condition skipped.  Uses the same sentinels that sensors.cpp writes.
static bool valveSensorOffline(uint8_t sensorId, float val) {
    for (size_t i = 0; i < RULE_SENSOR_COUNT; i++) {
        if (g_ruleSensors[i].id != sensorId) continue;
        switch (g_ruleSensors[i].kind) {
            case SENSOR_KIND_TEMP:  return val <= TEMP_OFFLINE_THRESH;
            case SENSOR_KIND_LEVEL: return false;   // bool – always valid
            default:                return val <= (SENSOR_OFFLINE + 1.0f);  // catches -999
        }
    }
    return true;   // unknown id → treat as offline
}

// Evaluate one ValveCondition against the state snapshot.
// Returns false if condition is unset (op == NONE or sensorId == 0),
// or if the sensor is offline.
static bool evalValveCondition(const ValveCondition& cond, const AppState& snap) {
    if (cond.op == VALVE_OP_NONE || cond.sensorId == 0) return false;
    float val = valveSensorValue(snap, cond.sensorId);
    if (valveSensorOffline(cond.sensorId, val)) return false;
    switch (cond.op) {
        case VALVE_OP_GT:  return val >  cond.value;
        case VALVE_OP_LT:  return val <  cond.value;
        case VALVE_OP_GTE: return val >= cond.value;
        case VALVE_OP_LTE: return val <= cond.value;
        case VALVE_OP_EQ:  return fabsf(val - cond.value) < 0.001f;
        default:           return false;
    }
}

// Evaluate all valve rules and drive hardware outputs.
// Called every controlTask tick regardless of isRunning or processMode.
// Close takes priority over open when both conditions fire simultaneously.
static void valveEvaluateAll() {
    // One state snapshot for the whole evaluation pass – cheap, consistent.
    stateLock();
    AppState snap = g_state;
    stateUnlock();

    for (int i = 0; i < VALVE_COUNT; i++) {
        bool openTrig  = evalValveCondition(snap.valveRules[i].openWhen,  snap);
        bool closeTrig = evalValveCondition(snap.valveRules[i].closeWhen, snap);

        bool newOpen = snap.valveOpen[i];   // latch: no change if neither fires
        if (closeTrig)     newOpen = false; // close wins if both fire
        else if (openTrig) newOpen = true;

        if (newOpen != snap.valveOpen[i]) {
            stateLock();
            g_state.valveOpen[i] = newOpen;
            stateUnlock();
            valveSet(i, newOpen);   // expander.h – silently ignores index > 2
            Serial.printf("[VALVE] %d (%s) → %s\n", i,
                          i == 0 ? "Dephlegmator" :
                          i == 1 ? "Dripper" :
                          i == 2 ? "Water" : "Placeholder",
                          newOpen ? "OPEN" : "CLOSED");
        }
    }
}

// Parse an operator string from a VALVE command into a ValveOp value.
static uint8_t valveOpFromString(const String& s) {
    if (s == "GT")  return VALVE_OP_GT;
    if (s == "LT")  return VALVE_OP_LT;
    if (s == "GTE") return VALVE_OP_GTE;
    if (s == "LTE") return VALVE_OP_LTE;
    if (s == "EQ")  return VALVE_OP_EQ;
    return VALVE_OP_NONE;
}


// ---------------------------------------------------------------------------
// controlTask  (FreeRTOS task – Core 0, priority 3)
//   The main control loop. Runs every CONTROL_LOOP_MS milliseconds.
//
//   Each iteration:
//     1. Safety check – trip and shut down if over-temperature.
//     2. Apply SSR outputs from current g_state.
//     3. Increment elapsed timer if the process is running.
// ---------------------------------------------------------------------------
void controlTask(void* pvParams) {
    const uint32_t loopMs = CONTROL_LOOP_MS;

    for (;;) {
        if (!controlSafetyCheck()) {
            // =================================================================
            // SAFETY TRIP – shut everything down immediately
            // =================================================================
            stateLock();
            g_state.isRunning      = false;
            g_state.timerElapsedMs = 0;
            // Zero out masterPower so the UI correctly shows 0 % after a trip.
            // The user must set a new power level and press START to resume.
            g_state.masterPower    = 0.0f;
            stateUnlock();

            ssrAllOff();               // cut all outputs at hardware level
            s_piIntegral = 0.0f;       // reset integral for future PI use
            stateSaveToNVS();          // persist masterPower=0 and safetyTripped

        } else {
            // =================================================================
            // Normal operation – apply outputs and update timer
            // =================================================================
            applySsrFromState();

            stateLock();
            if (g_state.isRunning) {
                g_state.timerElapsedMs += loopMs;
            }
            bool runningNow = g_state.isRunning;
            stateUnlock();

            // Extra safety: if something else stopped the process between
            // the safety check and here, make sure SSRs are off.
            if (!runningNow) {
                ssrAllOff();
            }
        }

        // Valve evaluation runs every tick regardless of isRunning or safety state.
        // This allows valves to respond to sensor conditions even when the
        // distillation/rectification process is stopped or tripped.
        valveEvaluateAll();

        vTaskDelay(pdMS_TO_TICKS(loopMs));
    }
}

// ---------------------------------------------------------------------------
// handleCommand
//   Single entry point for all control commands from LVGL and Web UI.
//   Both UIs send the same strings – this guarantees identical behaviour.
// ---------------------------------------------------------------------------
void handleCommand(const String& cmd) {
    String c = cmd;
    c.trim();
    Serial.print("[CMD] ");
    Serial.println(c);

    // =========================================================================
    // MODE:X – Switch process mode (0=Off, 1=Distillation, 2=Rectification)
    //   Stops any active run, resets timer, resets masterPower to 0,
    //   and turns all SSRs off.
    //   Switching to mode 0 also clears the safety latch.
    // =========================================================================
    if (c.startsWith("MODE:")) {
        int pm = c.substring(5).toInt();
        if (pm < 0 || pm > 2) pm = 0;

        stateLock();
        g_state.processMode    = pm;
        g_state.isRunning      = false;
        g_state.timerElapsedMs = 0;
        g_state.masterPower    = 0.0f;
        if (pm == 0) {
            // Clear safety latch when returning to idle
            g_state.safetyTripped = false;
            g_state.safetyMessage = "";
        }
        stateUnlock();

        ssrAllOff();
        applySsrFromState();
        stateSaveToNVS();
        return;
    }

    // =========================================================================
    // MASTER:NN.N  –  Set master power level (0–100 %)
    //   This is the ONLY power control command in the unified model.
    //   All active SSRs will run at exactly this duty cycle.
    //   Setting masterPower to 0 while running is allowed – SSRs go OFF
    //   but isRunning stays true (user can ramp back up without STOP/START).
    // =========================================================================
    if (c.startsWith("MASTER:")) {
        float p = c.substring(7).toFloat();
        if (p < 0.0f)   p = 0.0f;
        if (p > 100.0f) p = 100.0f;

        stateLock();
        g_state.masterPower = p;
        stateUnlock();

        applySsrFromState();    // ← ADD: immediate hardware update

        stateSaveToNVS();
        Serial.printf("[CMD] masterPower → %.1f %%\n", p);
        return;
    }

    // =========================================================================
    // TMAX:NN.N        – Set safetyTempMaxC absolutely or ±relatively
    // TMAX:N:SET:val   – Set per-sensor danger threshold for active process mode
    // =========================================================================
    if (c.startsWith("TMAX:")) {
        String arg = c.substring(5);
        arg.trim();

        // New format: "N:SET:val" – per-sensor danger threshold
        int setPos = arg.indexOf(":SET:");
        if (setPos > 0) {
            int   sensor = arg.substring(0, setPos).toInt();   // 1-based
            float val    = arg.substring(setPos + 5).toFloat();
            int   idx    = sensor - 1;

            if (idx < 0 || idx > 2) {
                Serial.println("[CMD] TMAX:SET – bad sensor index (must be 1–3)");
                return;
            }
            val = constrain(val, 0.0f, 200.0f);

            stateLock();
            // Write to the threshold set that matches the current mode.
            // If mode is Off (0), write to both so the value isn't silently lost.
            if (g_state.processMode == 2) {
                g_state.threshRect.tempDanger[idx] = val;
            } else if (g_state.processMode == 1) {
                g_state.threshDist.tempDanger[idx] = val;
            } else {
                g_state.threshDist.tempDanger[idx] = val;
                g_state.threshRect.tempDanger[idx] = val;
            }
            stateUnlock();

            stateSaveToNVS();
            Serial.printf("[CMD] TMAX sensor %d danger → %.1f C\n", sensor, val);
            return;
        }

        // Legacy format: absolute or ±relative safetyTempMaxC
        stateLock();
        float current = g_state.safetyTempMaxC;
        if (arg.startsWith("+") || arg.startsWith("-")) {
            current += arg.toFloat();
        } else {
            current = arg.toFloat();
        }
        current = constrain(current, 0.0f, 200.0f);
        g_state.safetyTempMaxC = current;
        stateUnlock();

        stateSaveToNVS();
        Serial.printf("[CMD] safetyTempMaxC → %.1f C\n", current);
        return;
    }

    // =========================================================================
    // START  –  Begin the process
    //   Requirements for START to be accepted:
    //     1. processMode is 1 or 2 (not Off)
    //     2. masterPower > 0 (no point starting with heaters at 0 %)
    //     3. Safety is not currently tripped
    // =========================================================================
    if (c == "START") {
        stateLock();
        bool canStart = (g_state.processMode == 1 || g_state.processMode == 2)
                        && !g_state.safetyTripped
                        && g_state.masterPower > 0.0f;

        if (canStart) {
            g_state.isRunning      = true;
            g_state.timerElapsedMs = 0;   // reset run timer on each fresh start
        }
        stateUnlock();

        if (canStart) {
            stateSaveToNVS();
        } else {
            Serial.println("[CMD] START refused: no mode, masterPower=0, or safety trip");
        }
        return;
    }

    // =========================================================================
    // STOP  –  End the process
    //   Resets mode to Off, clears safety latch, zeros masterPower.
    //   Sets lastTankTempC to current reading for auto-restore data quality.
    // =========================================================================
    if (c == "STOP") {
        stateLock();
        g_state.isRunning      = false;
        g_state.processMode    = 0;
        g_state.timerElapsedMs = 0;
        g_state.masterPower    = 0.0f;   // reset power on stop so next run starts clean
        g_state.safetyTripped  = false;
        g_state.safetyMessage  = "";
        g_state.lastTankTempC  = g_state.kettleTemp;   // snapshot for auto-restore
        stateUnlock();

        ssrAllOff();
        stateSaveToNVS();
        return;
    }

    // =========================================================================
    // THRESH:D/R:TW/TD:sensor:value  –  Fine-grained threshold editing
    // THRESH:D/R:PW/PD:value
    //   D = Distillation, R = Rectification
    //   TW = temperature warn, TD = temperature danger
    //   PW = pressure warn,    PD = pressure danger
    // =========================================================================
    if (c.startsWith("THRESH:")) {
        int p1 = 7;
        int p2 = c.indexOf(':', p1);
        if (p2 < 0) { Serial.println("[CMD] THRESH: bad format"); return; }
        String proc  = c.substring(p1, p2);
        bool isRect  = (proc == "R" || proc == "r");

        int p3 = c.indexOf(':', p2 + 1);
        String type, rest;
        if (p3 < 0) {
            type = c.substring(p2 + 1);
            rest = "";
        } else {
            type = c.substring(p2 + 1, p3);
            rest = c.substring(p3 + 1);
        }

        stateLock();
        SensorThresholds& thr = isRect ? g_state.threshRect : g_state.threshDist;

        if (type == "TW" || type == "TD") {
            int p4 = rest.indexOf(':');
            if (p4 < 0) { stateUnlock(); Serial.println("[CMD] THRESH: missing sensor index"); return; }
            int   idx = rest.substring(0, p4).toInt();
            float val = rest.substring(p4 + 1).toFloat();
            if (idx < 0 || idx > 2) { stateUnlock(); Serial.println("[CMD] THRESH: bad sensor index"); return; }

            if (type == "TW") thr.tempWarn[idx]   = val;
            else               thr.tempDanger[idx] = val;

        } else if (type == "PW" || type == "PD") {
            float val = rest.toFloat();
            if (type == "PW") thr.pressWarn   = val;
            else               thr.pressDanger = val;

        } else {
            stateUnlock();
            Serial.println("[CMD] THRESH: unknown type");
            return;
        }
        stateUnlock();

        stateSaveToNVS();
        Serial.print("[CMD] THRESH saved: "); Serial.println(c);
        return;
    }

    // =========================================================================
    // WIFI:SET:ssid:password  –  Update WiFi credentials and reconnect
    // =========================================================================
    if (c.startsWith("WIFI:SET:")) {
        String arg = c.substring(9);
        int colonPos = arg.indexOf(':');
        if (colonPos > 0) {
            String ssidEnc = arg.substring(0, colonPos);
            String passEnc = arg.substring(colonPos + 1);

            String ssid = urlDecodeString(ssidEnc);
            String pass = urlDecodeString(passEnc);

            ssid.trim();
            pass.trim();

            wifiApplyConfig(ssid.c_str(), pass.c_str());
            Serial.printf("[CMD] WiFi updated → SSID: '%s'\n", ssid.c_str());
            return;
        }
        Serial.println("[CMD] WIFI:SET: bad format (expected WIFI:SET:ssid:pass)");
        return;
    }

    // =========================================================================
    // VALVE:N:OPENCFG:<sensorId>:<op>:<value>
    // VALVE:N:CLOSECFG:<sensorId>:<op>:<value>
    //   Configure the open or close condition for valve N (0-based index).
    //   sensorId : RuleSensorId numeric value (from /state.ruleSensors[].id)
    //   op       : "GT" "LT" "GTE" "LTE" "EQ" "NONE"
    //   value    : float threshold
    //   Setting op to "NONE" (or sensorId 0) disables the condition.
    //   Rules are persisted immediately to NVS.
    // =========================================================================
    if (c.startsWith("VALVE:")) {
        // Parse valve index
        int p1 = 6;
        int p2 = c.indexOf(':', p1);
        if (p2 < 0) { Serial.println("[CMD] VALVE: missing index"); return; }
        int valveIdx = c.substring(p1, p2).toInt();
        if (valveIdx < 0 || valveIdx >= VALVE_COUNT) {
            Serial.printf("[CMD] VALVE: index %d out of range (0–%d)\n", valveIdx, VALVE_COUNT - 1);
            return;
        }

        // Parse condition type (OPENCFG or CLOSECFG)
        int p3 = c.indexOf(':', p2 + 1);
        if (p3 < 0) { Serial.println("[CMD] VALVE: missing condition type"); return; }
        String type = c.substring(p2 + 1, p3);
        bool isOpenCfg = (type == "OPENCFG");
        if (!isOpenCfg && type != "CLOSECFG") {
            Serial.println("[CMD] VALVE: type must be OPENCFG or CLOSECFG");
            return;
        }

        // Parse sensorId
        int p4 = c.indexOf(':', p3 + 1);
        if (p4 < 0) { Serial.println("[CMD] VALVE: missing sensorId"); return; }
        uint8_t sensorId = (uint8_t)c.substring(p3 + 1, p4).toInt();

        // Parse op string
        int p5 = c.indexOf(':', p4 + 1);
        if (p5 < 0) { Serial.println("[CMD] VALVE: missing op"); return; }
        String opStr = c.substring(p4 + 1, p5);
        uint8_t op   = valveOpFromString(opStr);

        // Parse threshold value
        float val = c.substring(p5 + 1).toFloat();

        ValveCondition cond;
        cond.sensorId = sensorId;
        cond.op       = op;
        cond.value    = val;

        stateLock();
        if (isOpenCfg) g_state.valveRules[valveIdx].openWhen  = cond;
        else           g_state.valveRules[valveIdx].closeWhen = cond;
        stateUnlock();

        stateSaveToNVS();
        Serial.printf("[CMD] VALVE %d %s → sensor=%d op=%s val=%.3f\n",
                      valveIdx, type.c_str(), sensorId, opStr.c_str(), val);
        return;
    }

    // =========================================================================
    // SENSOR:MAP:N:ROMHEX  –  Assign a DS18B20 ROM address to a sensor slot
    //   N:      0-based slot index (0 = Room, 1 = Kettle, … 7 = Product Cooler)
    //   ROMHEX: 16 uppercase hex chars (8 bytes), e.g. "28AABBCCDDEEFF01"
    //           "0000000000000000" clears the slot (marks it unassigned).
    //   Rule persisted to NVS immediately.
    // =========================================================================
    if (c.startsWith("SENSOR:MAP:")) {
        int p1 = 11;
        int p2 = c.indexOf(':', p1);
        if (p2 < 0) { Serial.println("[CMD] SENSOR:MAP: missing slot"); return; }
        int slot = c.substring(p1, p2).toInt();
        if (slot < 0 || slot >= MAX_SENSORS) {
            Serial.printf("[CMD] SENSOR:MAP: slot %d out of range (0–%d)\n", slot, MAX_SENSORS - 1);
            return;
        }
        String romHex = c.substring(p2 + 1);
        romHex.trim();
        romHex.toUpperCase();

        uint8_t rom[8] = {};
        if (!hexToRom(romHex.c_str(), rom)) {
            Serial.println("[CMD] SENSOR:MAP: invalid ROM hex (need 16 hex chars)");
            return;
        }
        stateLock();
        memcpy(g_state.tempSensorRom[slot], rom, 8);
        stateUnlock();

        stateSaveToNVS();
        Serial.printf("[CMD] Sensor slot %d → %s\n", slot, romHex.c_str());
        return;
    }

    // Unknown command
    Serial.print("[CMD] Unknown command: ");
    Serial.println(c);
}
