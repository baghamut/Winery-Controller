// =============================================================================
//  ota.h  –  Pull OTA interface
//  Device polls version.json from GitHub and flashes when a newer version
//  is available and the process is stopped.
// =============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  otaInit
//  Starts the background OTA check task. Call once from setup() after WiFi
//  has connected. The task runs on Core 0 at priority 1.
// ---------------------------------------------------------------------------
void otaInit();