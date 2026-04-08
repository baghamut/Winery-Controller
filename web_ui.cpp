// =============================================================================
//  web_ui.cpp  –  Web UI served from LittleFS
//
//  Routes:
//   GET /              → web_ui.html (interactive UI)
//   GET /monitor       → monitor.html (always-on monitoring page)
//   GET /Barrel_Big.png → static asset, 24h cache
//   GET /favicon.ico   → static asset, 24h cache
// =============================================================================
#include "web_ui.h"
#include "config.h"
#include "state.h"
#include "control.h"
#include <LittleFS.h>

static const char* UI_PATH      = "/web_ui.html";
static const char* MONITOR_PATH = "/monitor.html";

// Minimal fallback shown when /web_ui.html is absent from LittleFS.
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

// ---------------------------------------------------------------------------
//  streamLittleFSFile
//  Streams an open LittleFS File to the client in 512-byte chunks.
//  Caller must have already called httpd_resp_set_type() and any headers.
//  File is NOT closed here — caller owns the File object.
//  Returns ESP_OK on success, ESP_FAIL if any chunk send fails.
// ---------------------------------------------------------------------------
static esp_err_t streamLittleFSFile(httpd_req_t *req, File &f)
{
    uint8_t buf[512];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        if (httpd_resp_send_chunk(req, (const char*)buf, n) != ESP_OK) {
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
//  GET /
// ---------------------------------------------------------------------------
static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache");

    if (LittleFS.exists(UI_PATH)) {
        File f = LittleFS.open(UI_PATH, "r");
        if (f) {
            esp_err_t ret = streamLittleFSFile(req, f);
            f.close();
            return ret;
        }
    }

    Serial.println("[WebUI] WARN: /web_ui.html missing – serving fallback");
    const char* fb = FALLBACK_HTML;
    httpd_resp_sendstr(req, fb);
    return ESP_OK;
}
static const httpd_uri_t uri_root = {
    .uri = "/", .method = HTTP_GET, .handler = handle_root, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  GET /monitor
// ---------------------------------------------------------------------------
static esp_err_t handle_monitor(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache");

    if (LittleFS.exists(MONITOR_PATH)) {
        File f = LittleFS.open(MONITOR_PATH, "r");
        if (f) {
            esp_err_t ret = streamLittleFSFile(req, f);
            f.close();
            return ret;
        }
    }

    Serial.println("[WebUI] WARN: /monitor.html missing");
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                        "monitor.html not found. Upload LittleFS image.");
    return ESP_FAIL;
}
static const httpd_uri_t uri_monitor = {
    .uri = "/monitor.html", .method = HTTP_GET, .handler = handle_monitor, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  GET /Barrel_Big.png
// ---------------------------------------------------------------------------
static esp_err_t handle_barrel_png(httpd_req_t *req)
{
    File f = LittleFS.open("/Barrel_Big.png", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    esp_err_t ret = streamLittleFSFile(req, f);
    f.close();
    return ret;
}
static const httpd_uri_t uri_barrel_png = {
    .uri = "/Barrel_Big.png", .method = HTTP_GET,
    .handler = handle_barrel_png, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  GET /Background.png
// ---------------------------------------------------------------------------
static esp_err_t handle_background_png(httpd_req_t *req)
{
    File f = LittleFS.open("/Background.png", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    esp_err_t ret = streamLittleFSFile(req, f);
    f.close();
    return ret;
}
static const httpd_uri_t uri_background_png = {
    .uri = "/Background.png", .method = HTTP_GET,
    .handler = handle_background_png, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  GET /favicon.ico
// ---------------------------------------------------------------------------
static esp_err_t handle_favicon(httpd_req_t *req)
{
    File f = LittleFS.open("/favicon.ico", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    esp_err_t ret = streamLittleFSFile(req, f);
    f.close();
    return ret;
}
static const httpd_uri_t uri_favicon = {
    .uri = "/favicon.ico", .method = HTTP_GET,
    .handler = handle_favicon, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  webUiRegisterHandlers
// ---------------------------------------------------------------------------
void webUiRegisterHandlers(httpd_handle_t server)
{
    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_monitor);
    httpd_register_uri_handler(server, &uri_barrel_png);
    httpd_register_uri_handler(server, &uri_background_png);
    httpd_register_uri_handler(server, &uri_favicon);

    Serial.printf("[WebUI] Handlers registered – UI: %s  Monitor: %s\n",
                  UI_PATH, MONITOR_PATH);
}

void webUiStartFetchTask()
{
    Serial.println("[WebUI] Embedded UI active (no remote fetch needed)");
}
