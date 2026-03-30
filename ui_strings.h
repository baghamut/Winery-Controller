// =============================================================================
//  ui_strings.h  –  Centralised string constants for LVGL and Web UI
//  Change once here → reflected in both interfaces.
//  Source: https://raw.githubusercontent.com/baghamut/Winery-Controller/main/ui_strings.h
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// App identity
// ---------------------------------------------------------------------------
constexpr const char* STR_APP_TITLE        = "Cask & Crown";
constexpr const char* STR_APP_SUBTITLE     = "Winery Controller";

// ---------------------------------------------------------------------------
// Process names
// ---------------------------------------------------------------------------
constexpr const char* STR_PROC_DIST        = "DISTILLATION";
constexpr const char* STR_PROC_RECT        = "RECTIFICATION";

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
constexpr const char* STR_STATUS_RUNNING   = "RUNNING";
constexpr const char* STR_STATUS_STOPPED   = "STOPPED";
constexpr const char* STR_STATUS_SAFETY    = "SAFETY TRIP";

// ---------------------------------------------------------------------------
// Sensor names  (used in header, monitor rows, and sensor limit buttons)
// ---------------------------------------------------------------------------
constexpr const char* STR_SENSOR_NAME1     = "Room";
constexpr const char* STR_SENSOR_NAME2     = "Tank";
constexpr const char* STR_SENSOR_NAME3     = "Pillar";

// Header T1 format  ("%s: %.1f" → "Room: 24.5")
constexpr const char* STR_HDR_T1_FMT      = "%s: %.1f";

// ---------------------------------------------------------------------------
// Units / generic values
// ---------------------------------------------------------------------------
constexpr const char* STR_UNIT_DEGC        = "\xC2\xB0""C";   // UTF-8 °C
constexpr const char* STR_OFFLINE          = "Offline";

// ---------------------------------------------------------------------------
// Monitor labels (left column)
// ---------------------------------------------------------------------------
constexpr const char* STR_MON_PRESSURE     = "Pressure";
constexpr const char* STR_MON_LEVEL        = "Level";
constexpr const char* STR_MON_FLOW         = "Flow";
constexpr const char* STR_MON_TOTAL        = "Total";
constexpr const char* STR_MON_SSRS         = "SSRs";

// ---------------------------------------------------------------------------
// Level sensor values
// ---------------------------------------------------------------------------
constexpr const char* STR_LEVEL_OK         = "Level OK";
constexpr const char* STR_LEVEL_LOW        = "Level LOW";

// ---------------------------------------------------------------------------
// Screen / card titles
// ---------------------------------------------------------------------------
constexpr const char* STR_TITLE_MODE       = "Mode";            // web only
constexpr const char* STR_TITLE_MODE_SEL   = "Select Mode";
constexpr const char* STR_TITLE_CTRL       = "Control";
constexpr const char* STR_TITLE_CTRL_DIST  = "Distillation Control";
constexpr const char* STR_TITLE_CTRL_RECT  = "Rectification Control";
constexpr const char* STR_TITLE_MONITOR    = "Monitor";
constexpr const char* STR_TITLE_WIFI_SETUP = "WiFi Setup";

// ---------------------------------------------------------------------------
// Tmax panel
// ---------------------------------------------------------------------------
constexpr const char* STR_TMAX_PANEL_TITLE = "Set Sensor Max Temp";
constexpr const char* STR_LIMITS_LABEL     = "Limits:";        // LVGL row prefix

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
constexpr const char* STR_BTN_START        = "START";
constexpr const char* STR_BTN_STOP         = "STOP";
constexpr const char* STR_BTN_BACK         = "BACK";
constexpr const char* STR_BTN_SAVE         = "SAVE";

// ---------------------------------------------------------------------------
// Wi-Fi setup
// ---------------------------------------------------------------------------
constexpr const char* STR_WIFI_SSID_LABEL  = "SSID";
constexpr const char* STR_WIFI_PASS_LABEL  = "PASS";

// ---------------------------------------------------------------------------
// Safety config  (legacy / http usage)
// ---------------------------------------------------------------------------
constexpr const char* STR_MAX_TEMP         = "Max Temp (C)";
constexpr const char* STR_MAX_TEMP_ROW     = "MAX TEMP";

// ---------------------------------------------------------------------------
// Maintenance / diagnostic
// ---------------------------------------------------------------------------
constexpr const char* STR_OTA              = "Remote OTA Update";
constexpr const char* STR_RESTART          = "Restart Device";
constexpr const char* STR_SAVING           = "Saving...";
constexpr const char* STR_SAVED            = "Saved!";
constexpr const char* STR_ERROR            = "Error!";
constexpr const char* STR_YES              = "Yes";
constexpr const char* STR_NO               = "No";