// =============================================================================
//  control.cpp  –  Process control: state machine, safety, SSR output
// =============================================================================
#include "control.h"
#include "config.h"
#include "state.h"
#include "ssr.h"

static float s_piIntegral = 0.0f;   // kept in case you want PI later, not used now

// Small hysteresis so we don't chatter around the setpoint
#ifndef SAFETY_TEMP_HYST_C
#define SAFETY_TEMP_HYST_C 1.0f
#endif


// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void controlInit() {
    s_piIntegral = 0.0f;
}


// ---------------------------------------------------------------------------
// Safety check – returns true if safe to operate
// Uses g_state.safetyTempMaxC as the max allowed temperature
// ---------------------------------------------------------------------------
bool controlSafetyCheck() {
    stateLock();
    AppState snap = g_state;
    stateUnlock();

    // If not running, nothing to check.
    if (!snap.isRunning) return true;

    // Use the hottest available temperature as the deciding value
    float tMax = snap.t1;
    if (snap.t2 > tMax) tMax = snap.t2;
    if (snap.t3 > tMax) tMax = snap.t3;

    // If reading is obviously offline, do nothing special here
    if (tMax <= -150.0f) {
        return true;
    }

    // Over absolute configured max?
    if (tMax >= snap.safetyTempMaxC + SAFETY_TEMP_HYST_C) {
        // Latch safety trip flag and message
        stateLock();
        g_state.safetyTripped = true;
        g_state.safetyMessage = "Over temp";
        stateUnlock();
        // Trip: let controlTask stop process and SSRs
        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------
// Apply SSR outputs from state (called from control task)
// ---------------------------------------------------------------------------
static void applySsrFromState() {
    stateLock();
    AppState snap = g_state;
    stateUnlock();

    // If not running or processMode == 0 → everything off
    if (!snap.isRunning || snap.processMode == 0) {
        ssrAllOff();
        return;
    }

    // For Distillation → SSR1–3, Rectification → SSR4–5
    int pm = snap.processMode;

    for (int i = 0; i < 5; ++i) {
        bool allowed = false;
        if (pm == 1 && i <= 2) allowed = true;             // SSR1–3
        if (pm == 2 && (i == 3 || i == 4)) allowed = true; // SSR4–5

        float duty = 0.0f;
        if (allowed && snap.ssrOn[i] && snap.ssrPower[i] > 0.0f) {
            duty = snap.ssrPower[i];   // already 0–100 %
        }

        ssrSetDuty(i + 1, duty);
    }
}


// ---------------------------------------------------------------------------
// Control task (FreeRTOS)
//   - Periodically applies SSR outputs.
//   - Tracks elapsed run time (timerElapsedMs) for info only.
//   - Stops when safetyTempMaxC is exceeded.
// ---------------------------------------------------------------------------
void controlTask(void* pvParams) {
    const uint32_t loopMs = CONTROL_LOOP_MS;

    for (;;) {
        if (!controlSafetyCheck()) {
            // Safety trip: stop everything
            stateLock();
            g_state.isRunning      = false;
            g_state.timerElapsedMs = 0;
            // Clear SSR state in memory
            for (int i = 0; i < 5; ++i) {
                g_state.ssrOn[i]    = false;
                g_state.ssrPower[i] = 0.0f;
            }
            stateUnlock();

            ssrAllOff();
            s_piIntegral = 0.0f;
        } else {
            applySsrFromState();

            // Track elapsed time only for informational purposes
            stateLock();
            if (g_state.isRunning) {
                g_state.timerElapsedMs += loopMs;
            }
            bool runningNow = g_state.isRunning;
            stateUnlock();

            if (!runningNow) {
                ssrAllOff();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(loopMs));
    }
}


// ---------------------------------------------------------------------------
// handleCommand – simplified command dispatcher
//   MODE:X          (0=Off, 1=Distillation, 2=Rectification)
//   SSR:N:ON/OFF
//   SSR:N:PWR:NN.N  (0–100 %)
//   TMAX:NN.N       (set safetyTempMaxC in °C; NN.N >= 0, +/- relative)
//   START / STOP
// ---------------------------------------------------------------------------
void handleCommand(const String& cmd) {
    String c = cmd;
    c.trim();
    Serial.print("[CMD] ");
    Serial.println(c);

    // MODE
    if (c.startsWith("MODE:")) {
        int pm = c.substring(5).toInt();  // 0,1,2
        if (pm < 0 || pm > 2) pm = 0;

        stateLock();
        g_state.processMode    = pm;
        g_state.isRunning      = false;
        g_state.timerElapsedMs = 0;       // reset timer when mode changes
        if (pm == 0) {
            // Clear safety latch when going to idle
            g_state.safetyTripped = false;
            g_state.safetyMessage = "";
        }
        stateUnlock();

        ssrAllOff();
        stateSaveToNVS();
        return;
    }

    // SSR commands
    if (c.startsWith("SSR:")) {
        // Format: SSR:N:...
        int colon2 = c.indexOf(':', 4);
        String ssrStr = (colon2 > 0) ? c.substring(4, colon2) : c.substring(4);
        int ssr = ssrStr.toInt();  // 1..5
        int idx = ssr - 1;
        if (idx < 0 || idx > 4) {
            Serial.println("[CMD] Invalid SSR index");
            return;
        }

        if (c.indexOf(":ON", colon2) > 0) {
            stateLock();
            g_state.ssrOn[idx] = true;
            stateUnlock();
            stateSaveToNVS();
            return;
        }

        if (c.indexOf(":OFF", colon2) > 0) {
            stateLock();
            g_state.ssrOn[idx] = false;
            stateUnlock();
            stateSaveToNVS();
            return;
        }

        int pPos = c.indexOf(":PWR:", colon2);
        if (pPos > 0) {
            float p = c.substring(pPos + 5).toFloat();
            if (p < 0)   p = 0;
            if (p > 100) p = 100;
            stateLock();
            g_state.ssrPower[idx] = p;
            stateUnlock();
            stateSaveToNVS();
            return;
        }

        Serial.println("[CMD] Unknown SSR subcommand");
        return;
    }

    // TMAX:N:SET:val   → per-sensor danger threshold for the active process mode
    // TMAX:NN.N        → absolute set of safetyTempMaxC   (legacy)
    // TMAX:+NN / TMAX:-NN → relative adjust of safetyTempMaxC (legacy)
    if (c.startsWith("TMAX:")) {
        String arg = c.substring(5);   // everything after "TMAX:"
        arg.trim();

        // --- New format: "N:SET:val" ---
        int setPos = arg.indexOf(":SET:");
        if (setPos > 0) {
            int sensor  = arg.substring(0, setPos).toInt();   // 1-based
            float val   = arg.substring(setPos + 5).toFloat();
            int   idx   = sensor - 1;

            if (idx < 0 || idx > 2) {
                Serial.println("[CMD] TMAX:SET – bad sensor index");
                return;
            }
            if (val <   0.0f) val =   0.0f;
            if (val > 200.0f) val = 200.0f;

            stateLock();
            // Write to whichever threshold set matches the current mode;
            // if mode is unknown/idle write both so the value isn't lost.
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

        // --- Legacy format: absolute or relative safetyTempMaxC ---
        stateLock();
        float current = g_state.safetyTempMaxC;

        if (arg.startsWith("+") || arg.startsWith("-")) {
            current += arg.toFloat();
        } else {
            current = arg.toFloat();
        }

        if (current <   0.0f) current =   0.0f;
        if (current > 200.0f) current = 200.0f;

        g_state.safetyTempMaxC = current;
        stateUnlock();

        stateSaveToNVS();
        Serial.printf("[CMD] safetyTempMaxC → %.1f C\n", current);
        return;
    }

    // START
    if (c == "START") {
        stateLock();
        bool canStart = (g_state.processMode == 1 || g_state.processMode == 2);

        // Do not allow start if safety is still tripped
        if (g_state.safetyTripped) {
            canStart = false;
        }

        // Require at least one SSR enabled with >0 power
        bool anySSR = false;
        if (canStart) {
            for (int i = 0; i < 5; ++i) {
                if (g_state.ssrOn[i] && g_state.ssrPower[i] > 0.0f) {
                    anySSR = true;
                    break;
                }
            }
            if (!anySSR) canStart = false;
        }
        if (canStart) {
            g_state.isRunning      = true;
            g_state.timerElapsedMs = 0;   // start fresh
        }
        stateUnlock();

        if (canStart) {
            stateSaveToNVS();
        } else {
            Serial.println("[CMD] START refused: no mode, no active SSRs, or safety trip");
        }
        return;
    }

    // STOP
    if (c == "STOP") {
        stateLock();
        g_state.isRunning      = false;
        g_state.timerElapsedMs = 0;
        // Clear safety latch on explicit STOP
        g_state.safetyTripped  = false;
        g_state.safetyMessage  = "";
        // Reset all SSRs: OFF + 0% power
        for (int i = 0; i < 5; ++i) {
            g_state.ssrOn[i]    = false;
            g_state.ssrPower[i] = 0.0f;
        }
        stateUnlock();

        ssrAllOff();
        stateSaveToNVS();
        return;
    }

    // THRESH:D/R:TW/TD:sensor:value  or  THRESH:D/R:PW/PD:value
    if (c.startsWith("THRESH:")) {
        // Field 1: process (D or R)
        int p1 = 7;
        int p2 = c.indexOf(':', p1);
        if (p2 < 0) { Serial.println("[CMD] THRESH: bad format"); return; }
        String proc = c.substring(p1, p2);  // "D" or "R"
        bool isRect = (proc == "R" || proc == "r");

        // Field 2: type (TW, TD, PW, PD)
        int p3 = c.indexOf(':', p2 + 1);
        String type;
        String rest;
        if (p3 < 0) {
            type = c.substring(p2 + 1);
            rest = "";
        } else {
            type = c.substring(p2 + 1, p3);
            rest = c.substring(p3 + 1);
        }

        SensorThresholds& thr = isRect ? g_state.threshRect : g_state.threshDist;

        if (type == "TW" || type == "TD") {
            // Field 3: sensor index, Field 4: value
            int p4 = rest.indexOf(':');
            if (p4 < 0) { Serial.println("[CMD] THRESH: missing sensor index"); return; }
            int idx = rest.substring(0, p4).toInt();
            float val = rest.substring(p4 + 1).toFloat();
            if (idx < 0 || idx > 2) { Serial.println("[CMD] THRESH: bad sensor index"); return; }

            stateLock();
            if (type == "TW") thr.tempWarn[idx]   = val;
            else               thr.tempDanger[idx] = val;
            stateUnlock();

        } else if (type == "PW" || type == "PD") {
            float val = rest.toFloat();
            stateLock();
            if (type == "PW") thr.pressWarn   = val;
            else               thr.pressDanger = val;
            stateUnlock();

        } else {
            Serial.println("[CMD] THRESH: unknown type");
            return;
        }

        stateSaveToNVS();
        Serial.print("[CMD] THRESH saved: "); Serial.println(c);
        return;
    }

    Serial.print("[CMD] Unknown command: ");
    Serial.println(c);
}

// ---------------------------------------------------------------------------
// handleThreshCommand – called by handleCommand for THRESH:... strings
// Formats:
//   THRESH:D:TW:0:80.0   (distillation, temp warn, sensor 0)
//   THRESH:D:TD:1:92.0   (distillation, temp danger, sensor 1)
//   THRESH:R:TW:2:78.0   (rectification, temp warn, sensor 2)
//   THRESH:D:PW:0.06     (distillation, pressure warn)
//   THRESH:R:PD:0.08     (rectification, pressure danger)
// ---------------------------------------------------------------------------