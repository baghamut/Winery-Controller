// =============================================================================
// web_ui.cpp – Web UI served from LittleFS (/web_ui.html)
//
// Falls back to a minimal inline page if the file is absent (pre-filesystem-
// flash or corrupted partition).  All behaviour and command formats unchanged.
// =============================================================================
#include "web_ui.h"
#include "config.h"
#include "state.h"
#include "control.h"
#include <LittleFS.h>

static const char* UI_PATH = "/web_ui.html";

// Minimal fallback – shown only when /web_ui.html is not on LittleFS.
static const char FALLBACK_HTML[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<title>DistillController</title></head><body style='background:#111;color:#f5f5f5;"
    "font-family:sans-serif;display:flex;flex-direction:column;align-items:center;"
    "justify-content:center;height:100vh;gap:1rem'>"
    "<h2 style='color:#f97316'>DistillController</h2>"
    "<p>Web UI file not found on filesystem.</p>"
    "<p>Please upload the LittleFS image (<code>/data/web_ui.html</code>) to the device.</p>"
    "<p><a href='/state' style='color:#f97316'>/state JSON</a></p>"
    "</body></html>";

static void handleRoot(WebServer& server)
{
    if (LittleFS.exists(UI_PATH)) {
        File f = LittleFS.open(UI_PATH, "r");
        if (f) {
            server.sendHeader("Cache-Control", "no-store, no-cache");
            server.streamFile(f, "text/html");
            f.close();
            return;
        }
    }

    // Fallback: filesystem not flashed yet
    Serial.println("[WebUI] WARN: /web_ui.html missing – serving fallback");
    server.sendHeader("Cache-Control", "no-store, no-cache");
    server.send_P(200, "text/html", FALLBACK_HTML);
}

void webUiRegisterHandlers(WebServer& server)
{
    server.on("/", HTTP_GET, [&server]() { handleRoot(server); });
    Serial.printf("[WebUI] Handler registered – UI served from %s\n", UI_PATH);
}

void webUiStartFetchTask()
{
    Serial.println("[WebUI] Embedded UI active (no remote fetch needed)");
}