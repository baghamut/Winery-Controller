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
// 1-Wire auto-recovery
//   If every assigned sensor returns DEVICE_DISCONNECTED_C or 85.0 °C
//   (power-on default) for TEMP_REINIT_AFTER consecutive cycles, the bus is
//   considered stuck and dallas.begin() is called to reinitialise the library.
// ---------------------------------------------------------------------------
static uint8_t       s_tempFailStreak  = 0;
static const uint8_t TEMP_REINIT_AFTER = 3;   // ~3 s at 1 Hz

// ---------------------------------------------------------------------------
// Per-sensor miss debounce
//   A single bad read (noise, brief parasitic power sag) no longer immediately
//   marks a sensor OFFLINE.  The last good value is held silently until
//   SENSOR_OFFLINE_AFTER consecutive misses confirm the sensor is truly gone.
//   Recovery is immediate: one valid reading resets the counter and updates
//   the displayed value.
//
//   This eliminates the "flicker" pattern where individual sensors briefly
//   show OFFLINE for 1–2 cycles due to 1-Wire bus noise or brief CPU load
//   spikes (e.g. TLS handshakes) disturbing the conversion timing.
// ---------------------------------------------------------------------------
static float         s_lastGoodTemp[MAX_SENSORS];          // initialised in sensorsInit()
static uint8_t       s_sensorMissCount[MAX_SENSORS] = {0};
static const uint8_t SENSOR_OFFLINE_AFTER = 3;             // ~3 s at 1 Hz

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

    // Initialise per-sensor debounce state.
    // s_sensorMissCount is zero-initialised by the static declaration above.
    for (int i = 0; i < MAX_SENSORS; i++) s_lastGoodTemp[i] = TEMP_OFFLINE_THRESH;

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
    static uint32_t s_callCount = 0;
    Serial.printf("[TEMP] sensorsUpdate enter #%lu\n", ++s_callCount);
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

    // DEBUG: measure how long we wait for the mutex (long wait = sensorsScanBus
    // or another caller is holding the bus — points to contention as root cause).
    uint32_t t_mutexWait = millis();
    xSemaphoreTake(s_onewireMutex, portMAX_DELAY);
    uint32_t mutexWaitMs = millis() - t_mutexWait;
    if (mutexWaitMs > 50) {
        Serial.printf("[TEMP] waited %u ms for 1-Wire mutex\n", mutexWaitMs);
    }

    // DEBUG: measure total bus read time (>1100 ms = bus stuck or conversion
    // timeout — points to parasitic power issue or bus line problem).
    uint32_t t_busStart = millis();
    dallas.requestTemperatures();

    uint8_t assigned = 0, failed = 0;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!romIsAssigned(roms[i])) {
            temps[i] = TEMP_OFFLINE_THRESH;
            s_sensorMissCount[i] = 0;   // unassigned slot — reset counter
        } else {
            assigned++;
            float v = dallas.getTempC(roms[i]);
            // DEVICE_DISCONNECTED_C  = bus error / sensor not responding
            // 85.0 °C                = DS18B20 power-on default returned when
            //                          conversion did not complete (bus glitch)
            if (v == DEVICE_DISCONNECTED_C || v == 85.0f) {
                failed++;
                if (++s_sensorMissCount[i] >= SENSOR_OFFLINE_AFTER) {
                    // Confirmed offline after SENSOR_OFFLINE_AFTER consecutive misses.
                    Serial.printf("[TEMP] slot %d OFFLINE after %u misses (last good %.2f C)\n",
                                  i, s_sensorMissCount[i], s_lastGoodTemp[i]);
                    temps[i] = TEMP_OFFLINE_THRESH;
                } else {
                    // Brief glitch — hold last good value to avoid UI flicker.
                    // The miss counter keeps incrementing; if the sensor stays
                    // gone it will reach the threshold and go OFFLINE cleanly.
                    Serial.printf("[TEMP] slot %d miss %u/%u val=%.2f holding %.2f C\n",
                                  i, s_sensorMissCount[i], SENSOR_OFFLINE_AFTER,
                                  v, s_lastGoodTemp[i]);
                    temps[i] = s_lastGoodTemp[i];
                }
            } else {
                if (s_sensorMissCount[i] > 0) {
                    Serial.printf("[TEMP] slot %d RECOVERED after %u miss(es), val=%.2f C\n",
                                  i, s_sensorMissCount[i], v);
                }
                s_sensorMissCount[i] = 0;
                s_lastGoodTemp[i]    = v;
                temps[i]             = v;
            }
        }
    }

    uint32_t busMs = millis() - t_busStart;
    if (busMs > 1100) {
        Serial.printf("[TEMP] bus read took %u ms (expected ~750) assigned=%u failed=%u\n",
                      busMs, assigned, failed);
    }

    // Auto-recovery: if every assigned sensor missed this cycle the bus is
    // stuck.  After TEMP_REINIT_AFTER consecutive full-miss cycles call
    // dallas.begin() to reinitialise the library and unstick the bus.
    // Note: failed == assigned can be true even when individual sensors are
    // in their debounce window (holding last-good values), because 'failed'
    // counts raw bad reads, not how many are displaying OFFLINE.  This is
    // intentional — if all sensors are returning bad data the bus needs reinit
    // regardless of what the UI is showing.
    // DEBUG: periodic heartbeat — log all assigned sensor values every 10 cycles
    // even when healthy, so you can see the last known-good state before dropout.
    static uint8_t s_heartbeatCount = 0;
    if (++s_heartbeatCount >= 10) {
        s_heartbeatCount = 0;
        Serial.printf("[TEMP] heartbeat: assigned=%u failed=%u", assigned, failed);
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (romIsAssigned(roms[i])) {
                Serial.printf(" s%d=%.2f", i, temps[i]);
            }
        }
        Serial.println();
    }

    if (assigned > 0 && failed == assigned) {
        if (++s_tempFailStreak >= TEMP_REINIT_AFTER) {
            Serial.printf("[TEMP] *** ALL %u sensor(s) offline x%u — calling dallas.begin() ***\n",
                          assigned, s_tempFailStreak);
            dallas.begin();
            s_tempFailStreak = 0;
            // Reset per-sensor debounce state after bus reinit so sensors
            // come back online cleanly on the next valid read.
            memset(s_sensorMissCount, 0, sizeof(s_sensorMissCount));
        } else {
            Serial.printf("[TEMP] all %u sensor(s) failed this cycle (streak %u/%u)\n",
                          assigned, s_tempFailStreak, TEMP_REINIT_AFTER);
        }
    } else {
        s_tempFailStreak = 0;
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