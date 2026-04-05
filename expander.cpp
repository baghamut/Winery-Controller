// =============================================================================
//  expander.cpp  –  PCF8574 (x2) implementation
//
//  Wire mutex:
//    All Wire transactions go through wireLock() / wireUnlock() to prevent
//    concurrent bus access from sensorsTask (level read), controlTask (valve
//    writes), and flow2PollTask (expander 2 poll).
//
//  Bug fixed (v3.5.2):
//    wireLock() now returns bool.  All callers gate the Wire operation AND the
//    wireUnlock() call on the acquired flag.  Previously, wireLock() ignored
//    the xSemaphoreTake() return value; if a caller timed out it still called
//    wireUnlock(), giving a mutex it never owned → FreeRTOS assert crash.
//
//    flow2PollTask now checks s_exp2Ok before polling.  When the PCF8574 @
//    0x21 is absent, Wire.requestFrom() triggers ~100 ms NACK recovery and
//    held the mutex long enough to cause the timeout-then-give sequence above.
// =============================================================================
#include "expander.h"
#include <Wire.h>

// ---------------------------------------------------------------------------
// Wire mutex – created in expanderInit(), used by all Wire callers.
// portMAX_DELAY is safe: Wire ops on a present PCF8574 complete in < 1 ms,
// there are no recursive lock paths, and the task watchdog handles true
// deadlocks.
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_wireMutex = nullptr;

// Returns true if the mutex was acquired.
// IMPORTANT: wireUnlock() must only be called when wireLock() returned true.
static bool wireLock() {
    if (!s_wireMutex) return false;
    return xSemaphoreTake(s_wireMutex, portMAX_DELAY) == pdTRUE;
}
static void wireUnlock() {
    if (s_wireMutex) xSemaphoreGive(s_wireMutex);
}

// ---------------------------------------------------------------------------
// Expander 1 state  (PCF8574 at EXT_I2C_ADDR)
// PCF8574 quasi-bidirectional: write HIGH → weak pull-up (input mode)
//                              write LOW  → driven LOW (output)
// ---------------------------------------------------------------------------
static uint8_t s_pcfState = 0xFF;   // all HIGH on boot → all inputs / valves open

// ---------------------------------------------------------------------------
// Expander 2 presence flag.
// Set to true in expander2Init() if the device ACKs.
// Checked in flow2PollTask() to avoid hammering a missing device.
// Default false: safe before expander2Init() runs, even if flow2PollTask
// starts first.
// ---------------------------------------------------------------------------
static bool s_exp2Ok = false;

// ---------------------------------------------------------------------------
// expanderInit
// ---------------------------------------------------------------------------
void expanderInit() {
    s_wireMutex = xSemaphoreCreateMutex();
    Wire.begin(EXT_I2C_SDA, EXT_I2C_SCL);

    if (wireLock()) {
        Wire.beginTransmission(EXT_I2C_ADDR);
        Wire.write(s_pcfState);
        Wire.endTransmission();
        wireUnlock();
    }
}

// ---------------------------------------------------------------------------
// expanderReadBit / expanderWriteBit
// ---------------------------------------------------------------------------
bool expanderReadBit(uint8_t bit) {
    if (bit > 7) return true;
    if (!wireLock()) return true;    // safe default on lock failure: level OK
    Wire.requestFrom((uint8_t)EXT_I2C_ADDR, (uint8_t)1);
    uint8_t v = Wire.available() ? Wire.read() : 0xFF;
    wireUnlock();
    return ((v >> bit) & 0x01) != 0;
}

void expanderWriteBit(uint8_t bit, bool value) {
    if (bit > 7) return;
    if (value) s_pcfState |=  (1u << bit);
    else       s_pcfState &= ~(1u << bit);

    if (!wireLock()) return;         // skip write if lock unavailable
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
    if (!wireLock()) return;
    // Write 0xFF to set all bits as inputs (PCF8574 quasi-bidirectional)
    Wire.beginTransmission(EXT2_I2C_ADDR);
    Wire.write(0xFF);
    uint8_t err = Wire.endTransmission();
    wireUnlock();

    if (err != 0) {
        Serial.printf("[EXP2] PCF8574 @ 0x%02X not found (err %d) – water flow disabled\n",
                      EXT2_I2C_ADDR, err);
        s_exp2Ok = false;
    } else {
        Serial.printf("[EXP2] PCF8574 @ 0x%02X OK\n", EXT2_I2C_ADDR);
        s_exp2Ok = true;
    }
}

void flow2PollTask(void* pvParams) {
    (void)pvParams;
    uint8_t prev = 0xFF;    // assume all HIGH (no pulse) initially

    for (;;) {
        if (!s_exp2Ok) {
            // Device absent: yield for 1 s then recheck.
            // s_exp2Ok can only go true → false (never toggled), so once absent
            // this branch will be taken every iteration with negligible CPU use.
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint8_t curr = prev;
        bool    avail = false;

        if (wireLock()) {
            Wire.requestFrom((uint8_t)EXT2_I2C_ADDR, (uint8_t)1);
            avail = Wire.available();
            curr  = avail ? Wire.read() : prev;
            wireUnlock();
        }

        if (avail) {
            // Count falling edges (HIGH→LOW = sensor pulse active-low)
            uint8_t fell = prev & (~curr);
            if (fell & (1u << EXPANDER2_FLOW_DEPHL)) g_waterDephlPulses++;
            if (fell & (1u << EXPANDER2_FLOW_COND))  g_waterCondPulses++;
            prev = curr;
        }

        vTaskDelay(pdMS_TO_TICKS(FLOW2_POLL_MS));
    }
}