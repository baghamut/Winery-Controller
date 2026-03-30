// =============================================================================
//  sensors.h  –  Sensor driver interface
// =============================================================================
#pragma once
#include <Arduino.h>

// Initialise all sensor hardware. Call once from setup().
void sensorsInit();

// Periodic sensor update – read temps, pressure, level, flow
// and write results into g_state.
// Call from a FreeRTOS task or from loop() every ~1 second.
void sensorsUpdate();

// Reset accumulated flow total to zero (thread-safe).
void flowResetTotal();

// FreeRTOS task function – run on Core 0.
void sensorsTask(void* pvParams);