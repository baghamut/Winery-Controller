// =============================================================================
//  web_ui.h  –  Embedded Web UI interface
// =============================================================================
#pragma once
#include <Arduino.h>
#include <esp_https_server.h>

// ---------------------------------------------------------------------------
// webUiRegisterHandlers
//   Register the GET / route on the provided WebServer instance.
//   Must be called from httpServerInit() before server.begin().
// ---------------------------------------------------------------------------
#include <esp_https_server.h>
void webUiRegisterHandlers(httpd_handle_t server);

// ---------------------------------------------------------------------------
// webUiStartFetchTask
//   Legacy stub – kept for link compatibility.
//   The web UI is fully embedded and requires no remote fetch.
// ---------------------------------------------------------------------------
void webUiStartFetchTask();
