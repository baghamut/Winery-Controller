// =============================================================================
//  ui_lvgl.h  –  LVGL UI interface (3-panel UI: Mode / Control / Monitor)
// =============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// uiInit
//   Build all LVGL screens and start the periodic refresh timer.
//   Call once from setup() after lv_init() and touch init are complete.
// ---------------------------------------------------------------------------
void uiInit();

// ---------------------------------------------------------------------------
// uiRequestRefresh
//   Thread-safe: sets a flag that the LVGL timer will consume on its next tick.
//   Safe to call from any FreeRTOS task (sensorsTask, controlTask, loop()).
// ---------------------------------------------------------------------------
void uiRequestRefresh();

// ---------------------------------------------------------------------------
// uiRefreshFromState
//   Reads g_state and updates all visible LVGL widgets accordingly.
//   Must only be called from the LVGL task / timer callback (Core 1).
//   Do not call directly from other tasks – use uiRequestRefresh() instead.
// ---------------------------------------------------------------------------
void uiRefreshFromState();

// ---------------------------------------------------------------------------
// Explicit panel-switch helpers
//   Used when external logic needs to force a specific screen.
//   Normally the LVGL refresh timer handles screen transitions automatically.
// ---------------------------------------------------------------------------
void uiShowModeScreen();
void uiShowControlScreen();
void uiShowMonitorScreen();
