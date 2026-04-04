// =============================================================================
//  expander.cpp  –  PCF8574 (x2) implementation
//
//  Wire mutex:
//    All Wire transactions go through wireLock() / wireUnlock() to prevent
//    concurrent bus access from sensorsTask (level read), controlTask (valve
//    writes), and flow2PollTask (expander 2 poll).
// =============================================================================
#include "expander.h"
#include <Wire.h>

// ---------------------------------------------------------------------------
// Wire mutex – created in expanderInit(), used by all Wire calls
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_wireMutex = nullptr;

static inline void wireLock() {
    if (s_wireMutex) xSemaphoreTake(s_wireMutex, pdMS_TO_TICKS(20));
}
static inline void wireUnlock() {
    if (s_wireMutex) xSemaphoreGive(s_wireMutex);
}

// ---------------------------------------------------------------------------
// Expander 1 state  (PCF8574 at EXT_I2C_ADDR)
// PCF8574 quasi-bidirectional: write HIGH → weak pull-up (input mode)
//                              write LOW  → driven LOW (output)
// ---------------------------------------------------------------------------
static uint8_t s_pcfState = 0xFF;   // all HIGH on boot → all inputs / valves open

// ---------------------------------------------------------------------------
// expanderInit
// ---------------------------------------------------------------------------
void expanderInit() {
    s_wireMutex = xSemaphoreCreateMutex();
    Wire.begin(EXT_I2C_SDA, EXT_I2C_SCL);

    wireLock();
    Wire.beginTransmission(EXT_I2C_ADDR);
    Wire.write(s_pcfState);
    Wire.endTransmission();
    wireUnlock();
}

// ---------------------------------------------------------------------------
// expanderReadBit / expanderWriteBit
// ---------------------------------------------------------------------------
bool expanderReadBit(uint8_t bit) {
    if (bit > 7) return true;
    wireLock();
    Wire.requestFrom((uint8_t)EXT_I2C_ADDR, (uint8_t)1);
    uint8_t v = Wire.available() ? Wire.read() : 0xFF;
    wireUnlock();
    return ((v >> bit) & 0x01) != 0;
}

void expanderWriteBit(uint8_t bit, bool value) {
    if (bit > 7) return;
    if (value) s_pcfState |=  (1u << bit);
    else       s_pcfState &= ~(1u << bit);

    wireLock();
    Wire.beginTransmission(EXT_I2C_ADDR);
    Wire.write(s_pcfState);
    Wire.endTransmission();
    wireUnlock();
}

// ---------------------------------------------------------------------------
// Expander 2  (PCF8574 at EXT2_I2C_ADDR)
// Flow sensor inputs: EXPANDER2_FLOW_DEPHL (bit 0), EXPANDER2_FLOW_COND (bit 1)
// Sensors pull pin LOW on each pulse (active-low / falling-edge counted).
// ---------------------------------------------------------------------------
volatile uint32_t g_waterDephlPulses = 0;
volatile uint32_t g_waterCondPulses  = 0;

void expander2Init() {
    // Set all bits HIGH = input mode; no output needed on this expander
    wireLock();
    Wire.beginTransmission(EXT2_I2C_ADDR);
    Wire.write(0xFF);
    uint8_t err = Wire.endTransmission();
    wireUnlock();

    if (err != 0) {
        Serial.printf("[EXP2] PCF8574 @ 0x%02X not found (err %d) – water flow disabled\n",
                      EXT2_I2C_ADDR, err);
    } else {
        Serial.printf("[EXP2] PCF8574 @ 0x%02X OK\n", EXT2_I2C_ADDR);
    }
}

void flow2PollTask(void* pvParams) {
    (void)pvParams;
    uint8_t prev = 0xFF;   // assume all HIGH initially

    for (;;) {
        wireLock();
        Wire.requestFrom((uint8_t)EXT2_I2C_ADDR, (uint8_t)1);
        bool avail = Wire.available();
        uint8_t curr = avail ? Wire.read() : prev;
        wireUnlock();

        if (avail) {
            // Count falling edges (HIGH→LOW = sensor pulse active)
            uint8_t fell = prev & (~curr);
            if (fell & (1u << EXPANDER2_FLOW_DEPHL)) g_waterDephlPulses++;
            if (fell & (1u << EXPANDER2_FLOW_COND))  g_waterCondPulses++;
            prev = curr;
        }

        vTaskDelay(pdMS_TO_TICKS(FLOW2_POLL_MS));
    }
}
