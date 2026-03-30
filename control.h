// =============================================================================
//  control.h  –  Process control
// =============================================================================
#pragma once
#include <Arduino.h>

void controlInit();
void controlTask(void* pvParams);

// Handle plain-text commands from HTTP/UI
void handleCommand(const String& cmd);