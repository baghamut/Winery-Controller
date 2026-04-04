// =============================================================================
//  ui_strings.h  –  Centralised string constants for LVGL and Web UI
//  Change text once here → reflected in both interfaces automatically.
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// App identity
// ---------------------------------------------------------------------------
constexpr const char* STR_APP_TITLE            = "          Cask & Crown";
constexpr const char* STR_APP_SUBTITLE         = "Winery Controller";

// ---------------------------------------------------------------------------
// Process names
// ---------------------------------------------------------------------------
constexpr const char* STR_PROC_DIST            = "DISTILLATION";
constexpr const char* STR_PROC_RECT            = "RECTIFICATION";

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
constexpr const char* STR_STATUS_RUNNING       = "RUNNING";
constexpr const char* STR_STATUS_STOPPED       = "STOPPED";
constexpr const char* STR_STATUS_SAFETY        = "SAFETY TRIP";

// ---------------------------------------------------------------------------
// Sensor names  (used in header, monitor rows, and limit buttons)
// Short forms suitable for both LVGL panel labels and web UI display.
// ---------------------------------------------------------------------------
constexpr const char* STR_SENSOR_NAME1         = "Room";
constexpr const char* STR_SENSOR_NAME2         = "Kettle";       // was "Tank"
constexpr const char* STR_SENSOR_NAME3         = "Pillar 1";     // was "Pillar"
constexpr const char* STR_SENSOR_NAME4         = "Pillar 2";
constexpr const char* STR_SENSOR_NAME5         = "Pillar 3";
constexpr const char* STR_SENSOR_NAME6         = "Dephlegmator";
constexpr const char* STR_SENSOR_NAME7         = "Reflux Cond.";
constexpr const char* STR_SENSOR_NAME8         = "Product Cooler";
constexpr const char* STR_SENSOR_NAME9         = "Water Inlet";  // optional placeholder

// Catalog-only sensor labels (pressure, level, flow entries in ruleSensors[])
constexpr const char* STR_SENSOR_PILLAR_BASE   = "Pillar Base";
constexpr const char* STR_SENSOR_LEVEL         = "Kettle Level";
constexpr const char* STR_SENSOR_REFLUX_LEVEL  = "Reflux Level";
constexpr const char* STR_SENSOR_PRODUCT_FLOW  = "Product Flow";
constexpr const char* STR_SENSOR_WATER_DEPHL   = "Water Dephl.";
constexpr const char* STR_SENSOR_WATER_COND    = "Water Cond.";
constexpr const char* STR_SENSOR_WATER_COOLER  = "Water Cooler";

// Header T1 format string: printf(STR_HDR_T1_FMT, STR_SENSOR_NAME1, t1)
constexpr const char* STR_HDR_T1_FMT           = "%s: %.1f";

// ---------------------------------------------------------------------------
// Units / generic values
// ---------------------------------------------------------------------------
constexpr const char* STR_UNIT_DEGC            = "\xC2\xB0""C";  // UTF-8 °C
constexpr const char* STR_UNIT_BAR             = "bar";
constexpr const char* STR_UNIT_LPM             = "L/min";
constexpr const char* STR_UNIT_LITERS          = "L";
constexpr const char* STR_OFFLINE              = "Offline";
constexpr const char* STR_MAX_PREFIX           = "Max";

// ---------------------------------------------------------------------------
// Monitor labels
// ---------------------------------------------------------------------------
constexpr const char* STR_MON_PRESSURE         = "Pressure";
constexpr const char* STR_MON_LEVEL            = "Level";
constexpr const char* STR_MON_FLOW             = "Flow";
constexpr const char* STR_MON_TOTAL            = "Total";
constexpr const char* STR_MON_LOAD_STATUS      = "Load Status:";
constexpr const char* STR_MON_LOAD_FMT         = "%.0f%%";

// ---------------------------------------------------------------------------
// Level sensor values
// ---------------------------------------------------------------------------
constexpr const char* STR_LEVEL_OK             = "Level OK";
constexpr const char* STR_LEVEL_LOW            = "Level LOW";

// ---------------------------------------------------------------------------
// Screen / card titles
// ---------------------------------------------------------------------------
constexpr const char* STR_TITLE_MODE           = "Mode";
constexpr const char* STR_TITLE_MODE_SEL       = "Select Mode";
constexpr const char* STR_TITLE_CTRL           = "Control";
constexpr const char* STR_TITLE_CTRL_DIST      = "Distillation Control";
constexpr const char* STR_TITLE_CTRL_RECT      = "Rectification Control";
constexpr const char* STR_TITLE_MONITOR        = "Monitor";
constexpr const char* STR_TITLE_WIFI_SETUP     = "WiFi Setup";
constexpr const char* STR_TITLE_VALVES         = "Valves Control";

// ---------------------------------------------------------------------------
// Master Power slider label
// ---------------------------------------------------------------------------
constexpr const char* STR_MASTER_POWER_LABEL   = "Power";
constexpr const char* STR_POWER_HELP_TEXT      = "All active SSRs receive this duty cycle simultaneously.";

// ---------------------------------------------------------------------------
// Tmax / limits panel
// ---------------------------------------------------------------------------
constexpr const char* STR_TMAX_PANEL_TITLE     = "Set Sensor Max Temp";
constexpr const char* STR_LIMITS_LABEL         = "Limits:";
constexpr const char* STR_LIMITS_DANGER_TITLE  = "Sensor Max Thresholds Danger";

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
constexpr const char* STR_BTN_START            = "START";
constexpr const char* STR_BTN_STOP             = "STOP";
constexpr const char* STR_BTN_BACK             = "BACK";
constexpr const char* STR_BTN_SAVE             = "SAVE";
constexpr const char* STR_BTN_CANCEL           = "Cancel";

// ---------------------------------------------------------------------------
// Wi-Fi setup
// ---------------------------------------------------------------------------
constexpr const char* STR_WIFI_SSID_LABEL      = "SSID";
constexpr const char* STR_WIFI_PASS_LABEL      = "PASS";
constexpr const char* STR_WIFI_EMPTY_SSID      = "SSID cannot be empty";
constexpr const char* STR_WIFI_SAVED_MSG       = "WiFi settings saved! Device is reconnecting...";

// ---------------------------------------------------------------------------
// Valve screen / prototype text
// ---------------------------------------------------------------------------
constexpr const char* STR_VALVE_COL_VALVE      = "Valve";
constexpr const char* STR_VALVE_COL_OPEN       = "Open when";
constexpr const char* STR_VALVE_COL_CLOSE      = "Close when";
constexpr const char* STR_VALVE_PLACEHOLDER    = "Sensor + condition";
constexpr const char* STR_VALVE_PROTO_MSG      = "Web-only prototype. Logic is not yet sent to the device; final VALVE:CFG format is TBD.";

// ---------------------------------------------------------------------------
// Prompt / validation text
// ---------------------------------------------------------------------------
constexpr const char* STR_PROMPT_NEW_DANGER    = "New danger threshold for";
constexpr const char* STR_PROMPT_ENTER_0_3_BAR = "Enter 0-3 bar";
constexpr const char* STR_PROMPT_ENTER_0_200   = "Enter 0-200";

// ---------------------------------------------------------------------------
// Safety config  (legacy / http usage)
// ---------------------------------------------------------------------------
constexpr const char* STR_MAX_TEMP             = "Max Temp (C)";
constexpr const char* STR_MAX_TEMP_ROW         = "MAX TEMP";

// ---------------------------------------------------------------------------
// Maintenance / diagnostic
// ---------------------------------------------------------------------------
constexpr const char* STR_OTA                  = "Remote OTA Update";
constexpr const char* STR_RESTART              = "Restart Device";
constexpr const char* STR_SAVING               = "Saving...";
constexpr const char* STR_SAVED                = "Saved!";
constexpr const char* STR_ERROR                = "Error!";
constexpr const char* STR_YES                  = "Yes";
constexpr const char* STR_NO                   = "No";

// ---------------------------------------------------------------------------
// Sensor Max Threshold row labels for Control screen limit buttons
// ---------------------------------------------------------------------------
constexpr const char* STR_SMAX1                = STR_MON_PRESSURE;
constexpr const char* STR_SMAX2                = STR_SENSOR_NAME2;   // "Kettle"
constexpr const char* STR_SMAX3                = STR_SENSOR_NAME3;   // "Pillar 1"

// ---------------------------------------------------------------------------
// Sensor mapper UI strings
// ---------------------------------------------------------------------------
constexpr const char* STR_MAPPER_TITLE         = "Sensor Mapping";
constexpr const char* STR_MAPPER_SCAN          = "SCAN BUS";
constexpr const char* STR_MAPPER_SCANNING      = "Scanning...";
constexpr const char* STR_MAPPER_FOUND         = "device(s) found";
constexpr const char* STR_MAPPER_SCAN_FAIL     = "Scan failed";
constexpr const char* STR_MAPPER_UNASSIGNED    = "-- unassigned --";
constexpr const char* STR_MAPPER_NOT_FOUND     = "(not found)";
constexpr const char* STR_MAPPER_BTN_SENSORS   = "Sensors";

// ---------------------------------------------------------------------------
// Rule sensor catalog labels — aliases to existing sensor name constants
//   where a counterpart exists, literals for catalog-only entries.
//   The aliases guarantee that display labels stay in sync automatically.
// ---------------------------------------------------------------------------
constexpr const char* STR_RULE_SENSOR_ROOM          = STR_SENSOR_NAME1;
constexpr const char* STR_RULE_SENSOR_KETTLE         = STR_SENSOR_NAME2;   // was TANK
constexpr const char* STR_RULE_SENSOR_PILLAR1        = STR_SENSOR_NAME3;   // was PILLAR
constexpr const char* STR_RULE_SENSOR_PILLAR2        = STR_SENSOR_NAME4;
constexpr const char* STR_RULE_SENSOR_PILLAR3        = STR_SENSOR_NAME5;
constexpr const char* STR_RULE_SENSOR_DEPHLEGM       = STR_SENSOR_NAME6;
constexpr const char* STR_RULE_SENSOR_REFLUX         = STR_SENSOR_NAME7;
constexpr const char* STR_RULE_SENSOR_PRODUCT        = STR_SENSOR_NAME8;
constexpr const char* STR_RULE_SENSOR_WATER_IN_TEMP  = STR_SENSOR_NAME9;
constexpr const char* STR_RULE_SENSOR_PRESSURE       = STR_MON_PRESSURE;
constexpr const char* STR_RULE_SENSOR_PILLAR_BASE    = STR_SENSOR_PILLAR_BASE;
constexpr const char* STR_RULE_SENSOR_LEVEL          = STR_SENSOR_LEVEL;
constexpr const char* STR_RULE_SENSOR_REFLUX_LEVEL   = STR_SENSOR_REFLUX_LEVEL;
constexpr const char* STR_RULE_SENSOR_FLOW           = STR_SENSOR_PRODUCT_FLOW;
constexpr const char* STR_RULE_SENSOR_WATER_DEPHL    = STR_SENSOR_WATER_DEPHL;
constexpr const char* STR_RULE_SENSOR_WATER_COND     = STR_SENSOR_WATER_COND;
constexpr const char* STR_RULE_SENSOR_WATER_COOLER   = STR_SENSOR_WATER_COOLER;

// ---------------------------------------------------------------------------
// Sensor kind labels  (for debug output / future UI dropdowns)
// ---------------------------------------------------------------------------
constexpr const char* STR_SENSOR_KIND_TEMP         = "temp";
constexpr const char* STR_SENSOR_KIND_PRESSURE     = "pressure";
constexpr const char* STR_SENSOR_KIND_FLOW         = "flow";
constexpr const char* STR_SENSOR_KIND_LEVEL        = "level";
constexpr const char* STR_SENSOR_KIND_UNKNOWN      = "unknown";

// ---------------------------------------------------------------------------
// Valve names  (indices match VALVE_COUNT order in config.h)
// ---------------------------------------------------------------------------
constexpr const char* STR_VALVE_NAME_0             = "Dephlegmator";
constexpr const char* STR_VALVE_NAME_1             = "Dripper";
constexpr const char* STR_VALVE_NAME_2             = "Water";
constexpr const char* STR_VALVE_NAME_3             = "Placeholder 1";
constexpr const char* STR_VALVE_NAME_4             = "Placeholder 2";

// ---------------------------------------------------------------------------
// Valve rule operator labels
//   ASCII-safe so they render on the LVGL font without special glyph support.
//   Web UI JS may remap ">=" → "≥" etc. for display.
// ---------------------------------------------------------------------------
constexpr const char* STR_VALVE_OP_NONE            = "--";    // unset / disabled
constexpr const char* STR_VALVE_OP_GT              = ">";
constexpr const char* STR_VALVE_OP_LT              = "<";
constexpr const char* STR_VALVE_OP_GTE             = ">=";
constexpr const char* STR_VALVE_OP_LTE             = "<=";
constexpr const char* STR_VALVE_OP_EQ              = "==";
