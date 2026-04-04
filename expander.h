// =============================================================================
//  expander.h  –  External I2C GPIO expander helpers (PCF8574 x2)
//
//  Expander 1 (0x20): level switch input + 3 valve outputs
//  Expander 2 (0x21): 2 water flow sensor inputs (polled at FLOW2_POLL_MS)
//
//  All Wire transactions are protected by an internal mutex so that
//  sensorsTask, controlTask, and flow2PollTask can coexist safely.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Expander 1 – level + valves  (call once from sensorsInit)
// ---------------------------------------------------------------------------
void expanderInit();

bool expanderReadBit(uint8_t bit);
void expanderWriteBit(uint8_t bit, bool value);

inline bool levelIsOk() { return !expanderReadBit(EXPANDER_LEVEL); }

inline void valveSet(uint8_t index, bool open) {
    if (index > 2) return;
    expanderWriteBit(EXPANDER_VALVE1 + index, open);
}

// ---------------------------------------------------------------------------
// Expander 2 – water flow counters  (call once from setup before starting tasks)
// ---------------------------------------------------------------------------
void expander2Init();

// FreeRTOS task: polls expander 2 every FLOW2_POLL_MS and increments counters.
// Start with xTaskCreatePinnedToCore on Core 0.
void flow2PollTask(void* pvParams);

// Pulse counters incremented by flow2PollTask.
// Read and reset inside sensorsUpdate() the same way g_flowPulses is handled.
extern volatile uint32_t g_waterDephlPulses;
extern volatile uint32_t g_waterCondPulses;
