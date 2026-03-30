// =============================================================================
//  http_server.cpp  –  Arduino WebServer handlers (simplified 3-screen UI)
//  Endpoints:
//    GET  /               → web UI HTML
//    GET  /state          → JSON state
//    POST /               → plain-text command
//    POST /api/flow_reset → reset totalVolumeLiters
//    POST /ota            → trigger HTTPS OTA
// =============================================================================
#include "http_server.h"
#include "config.h"
#include "state.h"
#include "control.h"
#include "sensors.h"
#include "web_ui.h"

#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer s_server(80);

// ---------------------------------------------------------------------------
// extern from DistillController.ino
// ---------------------------------------------------------------------------
extern bool handleOtaFromHttp();

// ---------------------------------------------------------------------------
// GET /state  →  JSON
// ---------------------------------------------------------------------------
static void handleGetState() {
    stateLock();
    AppState snap = g_state;
    stateUnlock();

    DynamicJsonDocument doc(2048);

    doc["fw"]          = snap.fw;
    doc["isRunning"]   = snap.isRunning;
    doc["processMode"] = snap.processMode;

    JsonArray ssrOn  = doc.createNestedArray("ssrOn");
    JsonArray ssrPwr = doc.createNestedArray("ssrPower");
    for (int i = 0; i < 5; ++i) {
        ssrOn.add(snap.ssrOn[i]);
        ssrPwr.add(snap.ssrPower[i]);
    }

    doc["t1"] = snap.t1;
    doc["t2"] = snap.t2;
    doc["t3"] = snap.t3;

    doc["pressureBar"]       = snap.pressureBar;
    doc["levelHigh"]         = snap.levelHigh;
    doc["flowRateLPM"]       = snap.flowRateLPM;
    doc["totalVolumeLiters"] = snap.totalVolumeLiters;

    doc["ip"]   = snap.ip;
    doc["ssid"] = snap.ssid;

    // Optional: run time info
    doc["timerSetSeconds"] = snap.timerSetSeconds;
    doc["timerElapsedMs"]  = snap.timerElapsedMs;

    // Safety-related fields
    doc["safetyTempMaxC"] = snap.safetyTempMaxC;
    doc["safetyTripped"]  = snap.safetyTripped;
    doc["safetyMessage"]  = snap.safetyMessage;

    String output;
    serializeJson(doc, output);

    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", output);
}

// ---------------------------------------------------------------------------
// POST /  →  plain-text command
// ---------------------------------------------------------------------------
static void handlePostCommand() {
    String cmd = s_server.arg("plain");
    if (cmd.length() == 0) {
        s_server.send(400, "text/plain", "Empty command");
        return;
    }
    handleCommand(cmd);
    s_server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------------
// POST /api/flow_reset
// ---------------------------------------------------------------------------
static void handleFlowReset() {
    flowResetTotal();
    s_server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------------
// POST /ota  →  trigger HTTPS OTA
// ---------------------------------------------------------------------------
static void handleOta() {
    if (s_server.method() != HTTP_POST) {
        s_server.send(405, "text/plain", "Method Not Allowed\n");
        return;
    }

    s_server.send(200, "text/plain",
                  "OTA started. Device will reboot if update succeeds.\n");

    handleOtaFromHttp();
}

// ---------------------------------------------------------------------------
// Init / loop
// ---------------------------------------------------------------------------
void httpServerInit() {
    // GET / -> HTML (templated)
    webUiRegisterHandlers(s_server);

    // POST / -> commands
    s_server.on("/", HTTP_POST, handlePostCommand);

    // Other endpoints
    s_server.on("/state",          HTTP_GET,  handleGetState);
    s_server.on("/api/flow_reset", HTTP_POST, handleFlowReset);
    s_server.on("/ota",            HTTP_POST, handleOta);

    s_server.begin();
    Serial.println("[HTTP] Server started on port 80");
}

void httpServerHandle() {
    s_server.handleClient();
}