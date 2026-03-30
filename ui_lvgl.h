// =============================================================================
//  ui_lvgl.h  –  LVGL UI interface (simplified 3-screen UI)
// =============================================================================
#pragma once
#include <Arduino.h>

// Initialise LVGL widgets and create all screens.
// Call once from setup() after display/touch init.
void uiInit();

// Request a refresh of LVGL widgets from g_state.
// Safe to call from any task; just sets a flag consumed by an LVGL timer.
void uiRequestRefresh();

// Actually update widgets from g_state.
// Called only from the LVGL task/timer.
void uiRefreshFromState();

// Optional helpers to switch screens explicitly (used by LVGL code only).
void uiShowModeScreen();
void uiShowControlScreen();
void uiShowMonitorScreen();