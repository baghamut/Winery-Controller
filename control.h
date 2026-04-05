// =============================================================================
//  control.h  –  Process control interface
//
//  MASTER POWER UNIFICATION:
//    • SSR:N:ON/OFF and SSR:N:PWR:NN commands are REMOVED.
//    • New command: MASTER:NN.N  (set masterPower, 0–100 %)
//    • START/STOP/MODE/TMAX/THRESH/WIFI commands unchanged.
// =============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// controlInit
//   Resets any internal state (PI integral, etc.).
//   Call once from setup() after stateInit().
// ---------------------------------------------------------------------------
void controlInit();

// ---------------------------------------------------------------------------
// controlTask  (FreeRTOS task entry point)
//   Runs on Core 0 at priority 3.
//   Every CONTROL_LOOP_MS milliseconds:
//     1. Runs safety check (over-temperature).
//     2. Applies masterPower to all active SSRs via applySsrFromState().
//     3. Tracks elapsed run time in g_state.timerElapsedMs.
//   Signature matches xTaskCreatePinnedToCore() requirement.
// ---------------------------------------------------------------------------
void controlTask(void* pvParams);

// ---------------------------------------------------------------------------
// handleCommand
//   Dispatches a plain-text command string from the Web UI or LVGL UI.
//   Both interfaces must use the SAME command strings so behaviour is
//   always identical regardless of which UI the operator uses.
//
//   Supported commands:
//     MODE:X          – 0=Off, 1=Distillation, 2=Rectification
//                       Stops any running process and resets timer.
//     MASTER:NN.N     – Set masterPower to NN.N % (0–100).
//                       SSRs turn ON automatically when masterPower > 0
//                       and the process is running.
//     START           – Start the process (requires mode ≠ 0, masterPower > 0,
//                       safety not tripped).
//     STOP            – Stop process, reset mode to Off, clear safety latch.
//     TMAX:NN.N       – Set safetyTempMaxC (absolute or ±relative).
//     TMAX:N:SET:val  – Set per-sensor danger threshold for active process mode.
//     THRESH:…        – Full threshold command (see implementation for format).
//     WIFI:SET:ssid:pass – Change WiFi credentials and reconnect.
// ---------------------------------------------------------------------------
void handleCommand(const String& cmd);

// Instant SSR Response on Power Change
void applySsrFromState();

// ---------------------------------------------------------------------------
// postCommand
//   Thread-safe alternative to handleCommand() for calls originating inside
//   the LVGL task. Posts to a queue drained by loop() so NVS writes never
//   block lv_timer_handler().
//   Falls back to direct handleCommand() if called before the queue exists.
// ---------------------------------------------------------------------------
void postCommand(const char* cmd);
