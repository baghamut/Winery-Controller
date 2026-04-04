// =============================================================================
//  http_server.h  –  HTTP server interface
// =============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// httpServerInit
//   Register all URL routes and call server.begin().
//   Call once from setup() after WiFi has connected.
// ---------------------------------------------------------------------------
void httpServerInit();

// ---------------------------------------------------------------------------
// httpServerHandle
//   Process pending HTTP requests.
//   Must be called from loop() on every iteration.
// ---------------------------------------------------------------------------
void httpServerHandle();
