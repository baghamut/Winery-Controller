// =============================================================================
//  sensors.h  –  Sensor driver interface
// =============================================================================
#pragma once
#include <Arduino.h>

void sensorsInit();
void sensorsUpdate();
void flowResetTotal();
void sensorsTask(void* pvParams);

// DS18B20 bus scan
int  sensorsScanBus();
int  sensorsGetScannedCount();
void sensorsGetScannedRom(int idx, uint8_t romOut[8]);

// ROM address utilities (used by http_server.cpp and control.cpp)
void romToHex(const uint8_t* rom, char* buf);   // buf >= 17 bytes
bool hexToRom(const char* hex, uint8_t* romOut); // hex must be exactly 16 chars

extern portMUX_TYPE g_waterFlowMux;   // used by flow2PollTask in expander.cpp