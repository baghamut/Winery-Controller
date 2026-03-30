// =============================================================================
//  http_server.h  –  Arduino WebServer HTTP API
// =============================================================================
#pragma once
#include <Arduino.h>

// Initialise and start the HTTP server.  Call once from setup().
void httpServerInit();

// Call in loop() to handle incoming HTTP requests.
void httpServerHandle();

bool handleOtaFromHttp(); // OTA Handler