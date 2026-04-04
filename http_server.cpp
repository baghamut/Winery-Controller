// =============================================================================
//  http_server.cpp  –  Arduino WebServer handlers
// =============================================================================
#include "http_server.h"
#include "config.h"
#include "state.h"
#include "control.h"
#include "sensors.h"
#include "web_ui.h"
#include "ui_strings.h"

#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer s_server(80);

extern bool handleOtaFromHttp();

static void threshToJson(JsonObject obj, const SensorThresholds& t)
{
    JsonArray tw = obj.createNestedArray("tempWarn");
    JsonArray td = obj.createNestedArray("tempDanger");
    for (int i = 0; i < 3; i++) {
        tw.add(t.tempWarn[i]);
        td.add(t.tempDanger[i]);
    }
    obj["pressWarn"]   = t.pressWarn;
    obj["pressDanger"] = t.pressDanger;
}

static void handleGetState()
{
    stateLock();
    AppState snap = g_state;
    stateUnlock();

    // StaticJsonDocument lives in BSS (static storage) — zero heap allocation.
    // DynamicJsonDocument was allocating/freeing 8 KB from the heap on every
    // /state poll (~6000×/hr), causing progressive heap fragmentation and an
    // eventual crash after ~1 hour.  doc.clear() resets it for each new request.
    // WebServer processes one request at a time so a single static instance is safe.
    static StaticJsonDocument<8192> doc;
    doc.clear();

    doc["fw"]          = snap.fw;
    doc["isRunning"]   = snap.isRunning;
    doc["processMode"] = snap.processMode;
    doc["masterPower"] = snap.masterPower;

    // Core temperatures (field names changed; keys updated to match AppState)
    doc["roomTemp"]    = snap.roomTemp;
    doc["kettleTemp"]  = snap.kettleTemp;
    doc["pillar1Temp"] = snap.pillar1Temp;

    // Extended temperatures (SENSOR_OFFLINE when ROM unassigned or sensor absent)
    doc["pillar2Temp"]  = snap.pillar2Temp;
    doc["pillar3Temp"]  = snap.pillar3Temp;
    doc["dephlegmTemp"] = snap.dephlegmTemp;
    doc["refluxTemp"]   = snap.refluxTemp;
    doc["productTemp"]  = snap.productTemp;

    doc["pressureBar"]       = snap.pressureBar;
    doc["levelHigh"]         = snap.levelHigh;
    doc["flowRateLPM"]       = snap.flowRateLPM;
    doc["totalVolumeLiters"] = snap.totalVolumeLiters;

    // Extended sensors
    doc["waterDephlLpm"]  = snap.waterDephlLpm;
    doc["waterCondLpm"]   = snap.waterCondLpm;

    // DS18B20 ROM address map – exported as hex strings; "" = unassigned slot
    JsonArray sensorRoms = doc.createNestedArray("sensorRoms");
    for (int i = 0; i < MAX_SENSORS; i++) {
        char hex[17];
        bool assigned = false;
        for (int b = 0; b < 8; b++) if (snap.tempSensorRom[i][b]) { assigned = true; break; }
        if (assigned) { romToHex(snap.tempSensorRom[i], hex); sensorRoms.add(hex); }
        else          { sensorRoms.add(""); }
    }

    // Sensor slot names for the mapper UI (in slot order, same as tempSensorRom)
    const char* slotNames[MAX_SENSORS] = {
        STR_SENSOR_NAME1, STR_SENSOR_NAME2, STR_SENSOR_NAME3, STR_SENSOR_NAME4,
        STR_SENSOR_NAME5, STR_SENSOR_NAME6, STR_SENSOR_NAME7, STR_SENSOR_NAME8
    };
    JsonArray slotNamesArr = doc.createNestedArray("tempSensorSlotNames");
    for (int i = 0; i < MAX_SENSORS; i++) slotNamesArr.add(slotNames[i]);

    doc["ip"]   = snap.ip;
    doc["ssid"] = snap.ssid;

    doc["timerSetSeconds"] = snap.timerSetSeconds;
    doc["timerElapsedMs"]  = snap.timerElapsedMs;

    doc["safetyTempMaxC"]   = snap.safetyTempMaxC;
    doc["safetyPresMaxBar"] = snap.safetyPresMaxBar;
    doc["safetyTripped"]    = snap.safetyTripped;
    doc["safetyMessage"]  = snap.safetyMessage;

    threshToJson(doc.createNestedObject("threshDist"), snap.threshDist);
    threshToJson(doc.createNestedObject("threshRect"), snap.threshRect);

    doc["smaxLabel1"] = STR_SMAX1;
    doc["smaxLabel2"] = STR_SMAX2;
    doc["smaxLabel3"] = STR_SMAX3;

    doc["appTitle"]        = STR_APP_TITLE;
    doc["appSubtitle"]     = STR_APP_SUBTITLE;
    doc["procDist"]        = STR_PROC_DIST;
    doc["procRect"]        = STR_PROC_RECT;

    doc["statusRunning"]   = STR_STATUS_RUNNING;
    doc["statusStopped"]   = STR_STATUS_STOPPED;
    doc["statusSafety"]    = STR_STATUS_SAFETY;

    doc["sensorName1"]     = STR_SENSOR_NAME1;
    doc["sensorName2"]     = STR_SENSOR_NAME2;
    doc["sensorName3"]     = STR_SENSOR_NAME3;
    // Extended DS18B20 slots (4–8 currently wired; 9 = Water Inlet, future)
    doc["sensorName4"]     = STR_SENSOR_NAME4;
    doc["sensorName5"]     = STR_SENSOR_NAME5;
    doc["sensorName6"]     = STR_SENSOR_NAME6;
    doc["sensorName7"]     = STR_SENSOR_NAME7;
    doc["sensorName8"]     = STR_SENSOR_NAME8;
    doc["sensorName9"]     = STR_SENSOR_NAME9;

    doc["hdrT1Fmt"]        = STR_HDR_T1_FMT;
    doc["unitDegC"]        = STR_UNIT_DEGC;
    doc["unitBar"]         = STR_UNIT_BAR;
    doc["unitLpm"]         = STR_UNIT_LPM;
    doc["unitLiters"]      = STR_UNIT_LITERS;
    doc["labelOffline"]    = STR_OFFLINE;
    doc["maxPrefix"]       = STR_MAX_PREFIX;

    doc["labelPressure"]   = STR_MON_PRESSURE;
    doc["labelLevel"]      = STR_MON_LEVEL;
    doc["labelFlow"]       = STR_MON_FLOW;
    doc["labelTotal"]      = STR_MON_TOTAL;
    doc["labelLoadStatus"] = STR_MON_LOAD_STATUS;
    doc["labelLevelOk"]    = STR_LEVEL_OK;
    doc["labelLevelLow"]   = STR_LEVEL_LOW;

    doc["titleCtrl"]       = STR_TITLE_CTRL;
    doc["titleCtrlDist"]   = STR_TITLE_CTRL_DIST;
    doc["titleCtrlRect"]   = STR_TITLE_CTRL_RECT;
    doc["titleMonitor"]    = STR_TITLE_MONITOR;
    doc["titleWifiSetup"]  = STR_TITLE_WIFI_SETUP;
    doc["titleValves"]     = STR_TITLE_VALVES;

    doc["btnStart"]        = STR_BTN_START;
    doc["btnStop"]         = STR_BTN_STOP;
    doc["btnBack"]         = STR_BTN_BACK;
    doc["btnSave"]         = STR_BTN_SAVE;
    doc["btnCancel"]       = STR_BTN_CANCEL;

    doc["wifiSsidLabel"]   = STR_WIFI_SSID_LABEL;
    doc["wifiPassLabel"]   = STR_WIFI_PASS_LABEL;
    doc["wifiEmptySsid"]   = STR_WIFI_EMPTY_SSID;
    doc["wifiSavedMsg"]    = STR_WIFI_SAVED_MSG;

    doc["powerLabel"]      = STR_MASTER_POWER_LABEL;
    doc["powerHelpText"]   = STR_POWER_HELP_TEXT;
    doc["limitsDangerTitle"] = STR_LIMITS_DANGER_TITLE;

    doc["valveColValve"]   = STR_VALVE_COL_VALVE;
    doc["valveColOpen"]    = STR_VALVE_COL_OPEN;
    doc["valveColClose"]   = STR_VALVE_COL_CLOSE;
    doc["valvePlaceholder"] = STR_VALVE_PLACEHOLDER;
    doc["valveProtoMsg"]   = STR_VALVE_PROTO_MSG;

    doc["promptNewDanger"] = STR_PROMPT_NEW_DANGER;
    doc["promptEnter03Bar"] = STR_PROMPT_ENTER_0_3_BAR;
    doc["promptEnter0200"] = STR_PROMPT_ENTER_0_200;

    doc["tempOfflineThresh"]     = TEMP_OFFLINE_THRESH;
    doc["pressureOfflineThresh"] = SENSOR_OFFLINE;   // use actual sentinel, not magic -900
    doc["flowOfflineThresh"]     = SENSOR_OFFLINE;

    // Rule sensor catalog – drives valve-condition dropdowns
    JsonArray ruleSensors = doc.createNestedArray("ruleSensors");
    for (size_t i = 0; i < RULE_SENSOR_COUNT; i++) {
        const RuleSensorDef& rs = g_ruleSensors[i];
        JsonObject o = ruleSensors.createNestedObject();
        o["id"]      = rs.id;
        o["kind"]    = rs.kind;
        o["label"]   = rs.label;
        o["unit"]    = rs.unit;
        o["enabled"] = rs.enabled;
    }

    // Valve names
    const char* valveNameList[VALVE_COUNT] = {
        STR_VALVE_NAME_0, STR_VALVE_NAME_1, STR_VALVE_NAME_2,
        STR_VALVE_NAME_3, STR_VALVE_NAME_4
    };
    JsonArray valveNames = doc.createNestedArray("valveNames");
    for (int i = 0; i < VALVE_COUNT; i++) valveNames.add(valveNameList[i]);

    // Valve rules (persisted configuration) + live open/closed state
    JsonArray valveRulesArr = doc.createNestedArray("valveRules");
    JsonArray valveOpenArr  = doc.createNestedArray("valveOpen");
    for (int i = 0; i < VALVE_COUNT; i++) {
        JsonObject rule = valveRulesArr.createNestedObject();
        JsonObject ow   = rule.createNestedObject("openWhen");
        ow["sensorId"]  = snap.valveRules[i].openWhen.sensorId;
        ow["op"]        = snap.valveRules[i].openWhen.op;
        ow["value"]     = snap.valveRules[i].openWhen.value;
        JsonObject cw   = rule.createNestedObject("closeWhen");
        cw["sensorId"]  = snap.valveRules[i].closeWhen.sensorId;
        cw["op"]        = snap.valveRules[i].closeWhen.op;
        cw["value"]     = snap.valveRules[i].closeWhen.value;
        valveOpenArr.add(snap.valveOpen[i]);
    }

    // Valve operator labels (for UI dropdowns; ASCII-safe)
    doc["valveOpNone"] = STR_VALVE_OP_NONE;
    doc["valveOpGt"]   = STR_VALVE_OP_GT;
    doc["valveOpLt"]   = STR_VALVE_OP_LT;
    doc["valveOpGte"]  = STR_VALVE_OP_GTE;
    doc["valveOpLte"]  = STR_VALVE_OP_LTE;
    doc["valveOpEq"]   = STR_VALVE_OP_EQ;

    // Overflow guard – ArduinoJson V6 silently truncates on overflow
    if (doc.overflowed()) {
        Serial.println("[HTTP] WARNING: /state JSON doc overflowed – increase capacity in http_server.cpp");
    }

    String output;
    serializeJson(doc, output);

    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", output);
}

static void handlePostCommand()
{
    String cmd = s_server.arg("plain");
    if (cmd.length() == 0) {
        s_server.send(400, "text/plain", "Empty command");
        return;
    }
    handleCommand(cmd);
    s_server.send(200, "text/plain", "OK");
}

static void handleSensorScan()
{
    int count = sensorsScanBus();

    static StaticJsonDocument<512> scanDoc;
    scanDoc.clear();
    scanDoc["count"] = count;
    JsonArray roms = scanDoc.createNestedArray("roms");
    for (int i = 0; i < count; i++) {
        uint8_t rom[8];
        sensorsGetScannedRom(i, rom);
        char hex[17];
        romToHex(rom, hex);
        roms.add(hex);
    }
    String out;
    serializeJson(scanDoc, out);
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", out);
}

static void handleFlowReset()
{
    flowResetTotal();
    s_server.send(200, "text/plain", "OK");
}

static void handleOta()
{
    if (s_server.method() != HTTP_POST) {
        s_server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    s_server.send(200, "text/plain",
                  "OTA started. Device will reboot if update succeeds.");
    handleOtaFromHttp();
}

void httpServerInit()
{
    webUiRegisterHandlers(s_server);
    s_server.on("/", HTTP_POST, handlePostCommand);
    s_server.on("/state", HTTP_GET, handleGetState);
    s_server.on("/api/flow_reset",   HTTP_POST, handleFlowReset);
    s_server.on("/api/sensor_scan",  HTTP_POST, handleSensorScan);
    s_server.on("/ota", HTTP_POST, handleOta);
    s_server.begin();
    Serial.println("[HTTP] Server started on port 80");
}

void httpServerHandle()
{
    s_server.handleClient();
}
