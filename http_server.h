// =============================================================================
//  http_server.h  –  HTTP server interface
// =============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// httpServerInit
//   Register all URL routes and start the HTTPS server on port 443.
//   Loads TLS cert from NVS ("certs" namespace); falls back to compiled-in
//   certs.h if NVS is empty (first boot or after NVS erase).
//   Call once from setup() after WiFi has connected.
// ---------------------------------------------------------------------------
void httpServerInit();

// ---------------------------------------------------------------------------
// httpServerHandle
//   No-op stub — esp_https_server runs on its own FreeRTOS task.
//   Retained so loop() callers compile without change.
// ---------------------------------------------------------------------------
void httpServerHandle();
