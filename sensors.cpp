// =============================================================================
//  sensors.cpp – DS18B20 (ROM-addressed), pressure, level, flow → g_state
// =============================================================================
#include "sensors.h"
#include "config.h"
#include "state.h"
#include "ui_lvgl.h"
#include "expander.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include <string.h>

// ---------------------------------------------------------------------------
// DS18B20
// ---------------------------------------------------------------------------
static OneWire          oneWire(PIN_ONEWIRE);
static DallasTemperature dallas(&oneWire);

// ---------------------------------------------------------------------------
// 1-Wire bus mutex
//   Protects the shared OneWire/DallasTemperature bus against concurrent
//   access between sensorsTask (temperature reads) and the HTTP handler
//   (sensorsScanBus).  Created in sensorsInit() before any task is started.
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_onewireMutex = nullptr;

// ---------------------------------------------------------------------------
// Bus scan cache  (written by sensorsScanBus, read by http_server.cpp)
// ---------------------------------------------------------------------------
static uint8_t s_scannedRoms[MAX_SENSORS][8];
static int     s_scannedCount = 0;

// ---------------------------------------------------------------------------
// Product flow sensor (native GPIO interrupt)
// ---------------------------------------------------------------------------
volatile uint32_t g_flowPulses = 0;
static uint32_t   lastFlowMillis = 0;

// Spinlock for g_flowPulses.
// flowISR fires on Core 1 (attachInterrupt called from setup() on Core 1).
// sensorsUpdate reads+resets on Core 0.  noInterrupts() only masks the
// local core — use a portMUX spinlock so both cores are covered.
static portMUX_TYPE s_flowMux = portMUX_INITIALIZER_UNLOCKED;

// Spinlock protecting g_waterDephlPulses / g_waterCondPulses.
// These are incremented by flow2PollTask (a FreeRTOS task, not a hardware ISR),
// so the ESP32 portMUX-based critical section is required — not noInterrupts().
portMUX_TYPE g_waterFlowMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR flowISR() {
    portENTER_CRITICAL_ISR(&s_flowMux);
    g_flowPulses = g_flowPulses + 1;
    portEXIT_CRITICAL_ISR(&s_flowMux);
}

// ---------------------------------------------------------------------------
// Pressure filtering (unchanged from original)
// ---------------------------------------------------------------------------
static float    pressureFilteredBar = 0.0f;
static bool     pressureOnline      = false;
static uint8_t  pressureGoodCount   = 0;
static uint8_t  pressureBadCount    = 0;

static const float   PRESSURE_MIN_BAR_ONLINE = 0.002f;
static const float   PRESSURE_MAX_BAR_ONLINE = PRESSURE_MAX_BAR * 1.1f;
static const float   PRESSURE_ALPHA          = 0.2f;
static const uint8_t PRESSURE_GOOD_THRESH    = 12;
static const uint8_t PRESSURE_BAD_THRESH     = 8;

static const uint8_t PRESSURE_JITTER_WINDOW  = 8;
static const int     PRESSURE_JITTER_THRESH  = 40;
static int      pressureRawWindow[PRESSURE_JITTER_WINDOW] = {};
static uint8_t  pressureWinIdx   = 0;
static uint8_t  pressureWinFill  = 0;

// ---------------------------------------------------------------------------
// ROM utility functions
// ---------------------------------------------------------------------------
void romToHex(const uint8_t* rom, char* buf) {
    for (int i = 0; i < 8; i++) sprintf(buf + i * 2, "%02X", rom[i]);
    buf[16] = '\0';
}

bool hexToRom(const char* hex, uint8_t* romOut) {
    if (!hex || strlen(hex) != 16) return false;
    for (int i = 0; i < 8; i++) {
        char byte[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        char* end;
        romOut[i] = (uint8_t)strtol(byte, &end, 16);
        if (end != byte + 2) return false;   // invalid hex char
    }
    return true;
}

static bool romIsAssigned(const uint8_t* rom) {
    for (int i = 0; i < 8; i++) if (rom[i]) return true;
    return false;
}

// ---------------------------------------------------------------------------
// sensorsInit
// ---------------------------------------------------------------------------
void sensorsInit() {
    s_onewireMutex = xSemaphoreCreateMutex();   // must be first; guards all 1-Wire access

    dallas.begin();
    dallas.setResolution(DS18B20_RESOLUTION);
    // setWaitForConversion left at default (true) so requestTemperatures()
    // blocks ~750 ms and getTempC() always returns valid data on the same tick.

    analogReadResolution(12);

    expanderInit();   // also creates the Wire mutex

    pinMode(PIN_FLOW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW), flowISR, RISING);
    lastFlowMillis = millis();
}

// ---------------------------------------------------------------------------
// Bus scan  (call from HTTP handler; re-enumerates the 1-Wire bus)
// ---------------------------------------------------------------------------
int sensorsScanBus() {
    if (!s_onewireMutex) return 0;   // called before sensorsInit() – should never happen
    s_scannedCount = 0;
    uint8_t addr[8];
    // Acquire the bus mutex.  sensorsTask may be mid-read (requestTemperatures
    // blocks ~750 ms), so we wait.  The scan is a rare manual operation and a
    // brief HTTP-handler stall is acceptable.
    xSemaphoreTake(s_onewireMutex, portMAX_DELAY);
    oneWire.reset_search();
    while (s_scannedCount < MAX_SENSORS && oneWire.search(addr)) {
        memcpy(s_scannedRoms[s_scannedCount], addr, 8);
        s_scannedCount++;
    }
    xSemaphoreGive(s_onewireMutex);
    Serial.printf("[SCAN] 1-Wire: %d device(s) found\n", s_scannedCount);
    return s_scannedCount;
}

int sensorsGetScannedCount() { return s_scannedCount; }

void sensorsGetScannedRom(int idx, uint8_t romOut[8]) {
    if (idx < 0 || idx >= s_scannedCount) { memset(romOut, 0, 8); return; }
    memcpy(romOut, s_scannedRoms[idx], 8);
}

// ---------------------------------------------------------------------------
// sensorsUpdate – called every ~1000 ms from sensorsTask
// ---------------------------------------------------------------------------
void sensorsUpdate() {
    // -----------------------------------------------------------------------
    // Temperatures – request all simultaneously, then read by ROM address.
    //
    // Pattern:
    //   1. Short stateLock to snapshot ROM addresses into a local array.
    //      This keeps stateLock duration to a memcpy, well under 1 ms.
    //   2. All Dallas/OneWire I/O (requestTemperatures ~750 ms + scratchpad
    //      reads) runs outside stateLock but inside s_onewireMutex.
    //      This prevents concurrent access from sensorsScanBus().
    //   3. Short stateLock to write the 8 temperature results.
    // -----------------------------------------------------------------------

    // Step 1: snapshot ROM map
    uint8_t roms[MAX_SENSORS][8];
    stateLock();
    memcpy(roms, g_state.tempSensorRom, sizeof(roms));
    stateUnlock();

    // Step 2: bus I/O — guarded by the 1-Wire mutex only, stateLock NOT held
    float temps[MAX_SENSORS];
    xSemaphoreTake(s_onewireMutex, portMAX_DELAY);
    dallas.requestTemperatures();
    // DS18B20 at 12-bit needs ~750 ms; sensorsTask sleeps 1000 ms so we're fine.
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!romIsAssigned(roms[i])) {
            temps[i] = TEMP_OFFLINE_THRESH;
        } else {
            float v = dallas.getTempC(roms[i]);
            temps[i] = (v == DEVICE_DISCONNECTED_C) ? TEMP_OFFLINE_THRESH : v;
        }
    }
    xSemaphoreGive(s_onewireMutex);

    // Step 3: write results
    stateLock();
    g_state.roomTemp     = temps[0];
    g_state.kettleTemp   = temps[1];
    g_state.pillar1Temp  = temps[2];
    g_state.pillar2Temp  = temps[3];
    g_state.pillar3Temp  = temps[4];
    g_state.dephlegmTemp = temps[5];
    g_state.refluxTemp   = temps[6];
    g_state.productTemp  = temps[7];
    stateUnlock();

    // -----------------------------------------------------------------------
    // Pressure  (analog, GPIO9 – unchanged filtering logic)
    // -----------------------------------------------------------------------
    int   raw = analogRead(PIN_PRESSURE);
    float bar = (float)raw / ADC_MAX * PRESSURE_MAX_BAR;

    pressureRawWindow[pressureWinIdx] = raw;
    pressureWinIdx = (pressureWinIdx + 1) % PRESSURE_JITTER_WINDOW;
    if (pressureWinFill < PRESSURE_JITTER_WINDOW) pressureWinFill++;

    bool isStable = false;
    if (pressureWinFill >= PRESSURE_JITTER_WINDOW) {
        int mn = pressureRawWindow[0], mx = pressureRawWindow[0];
        for (uint8_t j = 1; j < PRESSURE_JITTER_WINDOW; ++j) {
            if (pressureRawWindow[j] < mn) mn = pressureRawWindow[j];
            if (pressureRawWindow[j] > mx) mx = pressureRawWindow[j];
        }
        isStable = (mx - mn) < PRESSURE_JITTER_THRESH;
    }

    bool inRange = (bar >= PRESSURE_MIN_BAR_ONLINE && bar <= PRESSURE_MAX_BAR_ONLINE);
    bool isGood  = inRange && isStable;

    if (!pressureOnline && isGood)       pressureFilteredBar = bar;
    else if (pressureOnline)             pressureFilteredBar = PRESSURE_ALPHA * bar +
                                         (1.0f - PRESSURE_ALPHA) * pressureFilteredBar;

    if (isGood) {
        if (pressureGoodCount < 255) pressureGoodCount++;
        if (pressureBadCount  > 0)   pressureBadCount--;
    } else {
        if (pressureBadCount  < 255) pressureBadCount++;
        if (pressureGoodCount > 0)   pressureGoodCount--;
    }
    if (!pressureOnline && pressureGoodCount >= PRESSURE_GOOD_THRESH) {
        pressureOnline = true; pressureBadCount = 0;
    } else if (pressureOnline && pressureBadCount >= PRESSURE_BAD_THRESH) {
        pressureOnline = false; pressureGoodCount = 0; pressureWinFill = 0;
    }

    stateLock();
    if (pressureOnline) {
        float p = pressureFilteredBar;
        if (p < 0.0f) p = 0.0f;
        if (p > PRESSURE_MAX_BAR_ONLINE) p = PRESSURE_MAX_BAR_ONLINE;
        g_state.pressureBar = p;
    } else {
        g_state.pressureBar = SENSOR_OFFLINE;
    }
    stateUnlock();

    // -----------------------------------------------------------------------
    // Level (expander 1, bit EXPANDER_LEVEL)
    // -----------------------------------------------------------------------
    bool lvlOk = levelIsOk();
    stateLock();
    g_state.levelHigh = lvlOk;
    stateUnlock();

    // -----------------------------------------------------------------------
    // Product flow  (native GPIO interrupt counter)
    // -----------------------------------------------------------------------
    uint32_t now = millis();
    uint32_t dt  = now - lastFlowMillis;
    if (dt >= FLOW_COMPUTE_INTERVAL_MS) {
    uint32_t pulses;
    taskENTER_CRITICAL(&s_flowMux);
    pulses = g_flowPulses;
    g_flowPulses = 0;
    taskEXIT_CRITICAL(&s_flowMux);

        float litres = pulses / FLOW_PULSES_PER_LITRE;
        float lpm    = (dt > 0) ? litres * 60000.0f / dt : 0.0f;

        stateLock();
        g_state.flowRateLPM = (lpm <= 0.0f && g_state.totalVolumeLiters <= 0.0f)
                              ? SENSOR_OFFLINE : lpm;
        g_state.totalVolumeLiters += litres;
        stateUnlock();

        // Water flow sensors  (expander 2, polled by flow2PollTask)
        // -----------------------------------------------------------------------
        // g_waterDephlPulses / g_waterCondPulses are incremented by flow2PollTask,
        // a FreeRTOS software task — NOT a hardware ISR.  noInterrupts() only
        // masks hardware interrupts; it does not prevent the FreeRTOS scheduler
        // from switching to flow2PollTask between the two reads.
        // taskENTER_CRITICAL() also suspends the scheduler tick on this core,
        // giving a safe atomic read-and-reset for both counters.
        uint32_t dephlPulses, condPulses;
        taskENTER_CRITICAL(&g_waterFlowMux);
        dephlPulses = g_waterDephlPulses; g_waterDephlPulses = 0;
        condPulses  = g_waterCondPulses;  g_waterCondPulses  = 0;
        taskEXIT_CRITICAL(&g_waterFlowMux);

        float dephlLpm = (dt > 0) ? (dephlPulses / FLOW_PULSES_PER_LITRE) * 60000.0f / dt : 0.0f;
        float condLpm  = (dt > 0) ? (condPulses  / FLOW_PULSES_PER_LITRE) * 60000.0f / dt : 0.0f;

        stateLock();
        // Stay SENSOR_OFFLINE until first non-zero reading so the Web UI
        // shows "Offline" rather than "0.00" for unconnected sensors.
        if (dephlPulses > 0 || g_state.waterDephlLpm > SENSOR_OFFLINE + 1.0f)
            g_state.waterDephlLpm = dephlLpm;
        if (condPulses > 0 || g_state.waterCondLpm > SENSOR_OFFLINE + 1.0f)
            g_state.waterCondLpm  = condLpm;
        stateUnlock();

        lastFlowMillis = now;
    }
}

// ---------------------------------------------------------------------------
// sensorsTask
// ---------------------------------------------------------------------------
void sensorsTask(void* pvParams) {
    (void)pvParams;
    // sensorsInit() is called once from setup() before this task is created.
    // Do NOT call it here — expanderInit() would recreate the Wire mutex and
    // corrupt flow2PollTask which is already using it.
    for (;;) {
        sensorsUpdate();
        uiRequestRefresh();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---------------------------------------------------------------------------
// flowResetTotal
// ---------------------------------------------------------------------------
void flowResetTotal() {
    taskENTER_CRITICAL(&s_flowMux);
    g_flowPulses = 0;
    taskEXIT_CRITICAL(&s_flowMux);
    stateLock();
    g_state.totalVolumeLiters = 0.0f;
    stateUnlock();
}
