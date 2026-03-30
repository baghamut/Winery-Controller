// =============================================================================
//  sensors.cpp – DS18B20, pressure, level, flow → g_state
// =============================================================================
#include "sensors.h"
#include "config.h"
#include "state.h"
#include "ui_lvgl.h"

#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------------------------------------------------------------------
// DS18B20
// ---------------------------------------------------------------------------
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature dallas(&oneWire);
static int dsCount = 0;

// ---------------------------------------------------------------------------
// Flow sensor
// ---------------------------------------------------------------------------
volatile uint32_t g_flowPulses = 0;
static uint32_t   lastFlowMillis = 0;

// ISR for flow pulses
void IRAM_ATTR flowISR() {
    g_flowPulses++;
}

// ---------------------------------------------------------------------------
// Pressure filtering / online detection
// ---------------------------------------------------------------------------
static float    pressureFilteredBar = 0.0f;
static bool     pressureOnline      = false;
static uint8_t  pressureGoodCount   = 0;
static uint8_t  pressureBadCount    = 0;

// Tunables
static const float   PRESSURE_MIN_BAR_ONLINE = 0.002f;             // ~82 ADC counts; below → treat as disconnected/zero
static const float   PRESSURE_MAX_BAR_ONLINE = PRESSURE_MAX_BAR * 1.1f; // sanity ceiling (0.11 bar)
static const float   PRESSURE_ALPHA          = 0.2f;               // low-pass filter
static const uint8_t PRESSURE_GOOD_THRESH    = 12;                 // consecutive stable+in-range samples
static const uint8_t PRESSURE_BAD_THRESH     = 8;                  // consecutive bad samples to go offline

// Jitter / stability detection
// A connected sensor is a low-impedance source → tight ADC readings.
// A floating pin picks up EMI → wide, jumping readings.
// We keep a rolling window of raw ADC counts and require the peak-to-peak
// spread to be below PRESSURE_JITTER_THRESH before treating a reading as "good".
static const uint8_t PRESSURE_JITTER_WINDOW  = 8;                  // rolling window size (samples)
static const int     PRESSURE_JITTER_THRESH  = 40;                 // ADC counts; ≈0.001 bar @ 0.1 bar FS (~1% FS)
static int      pressureRawWindow[PRESSURE_JITTER_WINDOW] = {};
static uint8_t  pressureWinIdx   = 0;
static uint8_t  pressureWinFill  = 0;   // how many slots are valid (0..PRESSURE_JITTER_WINDOW)

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void sensorsInit() {
    // DS18B20
    dallas.begin();
    dsCount = dallas.getDeviceCount();

    // Pressure ADC
    analogReadResolution(12); // 0-4095

    // Level
    pinMode(PIN_LEVEL, INPUT_PULLUP);

    // Flow
    pinMode(PIN_FLOW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW), flowISR, RISING);

    lastFlowMillis = millis();
}

// ---------------------------------------------------------------------------
// Periodic update – read all sensors and update g_state
// ---------------------------------------------------------------------------
void sensorsUpdate() {
    // --- Temperatures ---
    dallas.requestTemperatures();

    float t[3] = {
        TEMP_OFFLINE_THRESH,
        TEMP_OFFLINE_THRESH,
        TEMP_OFFLINE_THRESH
    };

    for (int i = 0; i < dsCount && i < 3; ++i) {
        float v = dallas.getTempCByIndex(i);
        if (v == DEVICE_DISCONNECTED_C) v = TEMP_OFFLINE_THRESH;
        t[i] = v;
    }

    stateLock();
    g_state.t1 = t[0];
    g_state.t2 = t[1];
    g_state.t3 = t[2];
    stateUnlock();

    // --- Pressure ---
    int   raw = analogRead(PIN_PRESSURE);
    float bar = (float)raw / ADC_MAX * PRESSURE_MAX_BAR;

    // --- Jitter / stability check ---
    // Push raw count into rolling window.
    pressureRawWindow[pressureWinIdx] = raw;
    pressureWinIdx = (pressureWinIdx + 1) % PRESSURE_JITTER_WINDOW;
    if (pressureWinFill < PRESSURE_JITTER_WINDOW) pressureWinFill++;

    bool isStable = false;
    if (pressureWinFill >= PRESSURE_JITTER_WINDOW) {
        int minRaw = pressureRawWindow[0], maxRaw = pressureRawWindow[0];
        for (uint8_t j = 1; j < PRESSURE_JITTER_WINDOW; ++j) {
            if (pressureRawWindow[j] < minRaw) minRaw = pressureRawWindow[j];
            if (pressureRawWindow[j] > maxRaw) maxRaw = pressureRawWindow[j];
        }
        isStable = (maxRaw - minRaw) < PRESSURE_JITTER_THRESH;
    }
    // Window not full yet → treat as unstable (not enough history)

    // Basic sanity window + stability: both must pass to count as "good"
    bool inRange = (bar >= PRESSURE_MIN_BAR_ONLINE &&
                    bar <= PRESSURE_MAX_BAR_ONLINE);
    bool isGood  = inRange && isStable;

    // Exponential low-pass filter (only when we have some prior value)
    if (!pressureOnline && isGood) {
        // First time we see something stable and sane: initialise filter
        pressureFilteredBar = bar;
    } else if (pressureOnline) {
        pressureFilteredBar = PRESSURE_ALPHA * bar +
                              (1.0f - PRESSURE_ALPHA) * pressureFilteredBar;
    }

    // Debounce online/offline state
    if (isGood) {
        if (pressureGoodCount < 255) pressureGoodCount++;
        if (pressureBadCount  > 0)   pressureBadCount--;
    } else {
        if (pressureBadCount  < 255) pressureBadCount++;
        if (pressureGoodCount > 0)   pressureGoodCount--;
    }

    if (!pressureOnline && pressureGoodCount >= PRESSURE_GOOD_THRESH) {
        pressureOnline   = true;
        pressureBadCount = 0;
    } else if (pressureOnline && pressureBadCount >= PRESSURE_BAD_THRESH) {
        pressureOnline    = false;
        pressureGoodCount = 0;
        pressureWinFill   = 0;   // flush window so next connect re-qualifies cleanly
    }

    // Publish into state:
    // - when online: filtered, clamped value
    // - when offline: SENSOR_OFFLINE
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

    // --- Level ---
    bool levelOk = (digitalRead(PIN_LEVEL) == LOW); // LOW = level OK
    stateLock();
    g_state.levelHigh = levelOk;
    stateUnlock();

    // --- Flow & total volume ---
    uint32_t now = millis();
    uint32_t dt  = now - lastFlowMillis;
    if (dt >= FLOW_COMPUTE_INTERVAL_MS) {
        uint32_t pulses;
        noInterrupts();
        pulses = g_flowPulses;
        g_flowPulses = 0;
        interrupts();

        float litres = pulses / FLOW_PULSES_PER_LITRE;
        float lpm    = (dt > 0) ? (litres * 60000.0f / dt) : 0.0f;

        stateLock();
        // If we never saw pulses since boot, treat as offline.
        if (lpm <= 0.0f && g_state.totalVolumeLiters <= 0.0f) {
            g_state.flowRateLPM = SENSOR_OFFLINE;
        } else {
            g_state.flowRateLPM = lpm;
        }
        g_state.totalVolumeLiters += litres;
        stateUnlock();

        lastFlowMillis = now;
    }
}

// ---------------------------------------------------------------------------
// FreeRTOS task: call sensorsUpdate() and refresh UI
// ---------------------------------------------------------------------------
void sensorsTask(void* pvParams) {
    sensorsInit();
    (void)pvParams;

    for (;;) {
        sensorsUpdate();
        uiRequestRefresh();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---------------------------------------------------------------------------
// Reset accumulated flow total to zero
// ---------------------------------------------------------------------------
void flowResetTotal() {
    noInterrupts();
    g_flowPulses = 0;
    interrupts();

    stateLock();
    g_state.totalVolumeLiters = 0.0f;
    stateUnlock();
}