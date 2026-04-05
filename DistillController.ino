// =============================================================================
//  DistillController.ino  –  JC3248W535 / ESP32-S3, LVGL v9, landscape UI
//
//  MASTER POWER UNIFICATION:
//    All 5 SSRs are now controlled by a single masterPower value (0–100 %).
//    Active SSRs for the selected mode all receive the same duty cycle.
//    No per-SSR on/off or per-SSR power level exists in AppState.
//    See control.h and state.h for the full architecture.
//
//  BOOT SEQUENCE:
//    1. stateInit()       – load NVS → g_state
//    2. LittleFS mount
//    3. ssrInit()         – configure LEDC, all SSRs OFF
//    4. Display init
//    5. Touch init
//    6. LVGL v9 init
//    7. uiInit()          – build all LVGL panels
//    8. wifiConnect()     – connect + trigger webUiStartFetchTask()
//    9. sensorsTask       – Core 0, priority 2
//   10. controlTask       – Core 0, priority 3
//   11. httpServerInit()  – start HTTP server port 80
//   12. lvglTask          – Core 1, priority 1
//   13. Auto-restore check (see below)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>

// OTA
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

// ---- Arduino_GFX display driver (AXS15231B over QSPI) --------------------
#include <Arduino_GFX_Library.h>

// ---- LVGL v9 --------------------------------------------------------------
#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>

// ---- Touch driver (JC3248W535EN-Touch-LCD, AXS15231B I2C) ----------------
#include <JC3248W535EN-Touch-LCD.h>

// ---- Project headers ------------------------------------------------------
#include "config.h"
#include "ui_strings.h"
#include "state.h"
#include "sensors.h"
#include "expander.h"
#include "ssr.h"
#include "control.h"
#include "http_server.h"
#include "ui_lvgl.h"
#include "web_ui.h"

// ===========================================================================
//  AXS15231B display via QSPI + Canvas (frame buffer in PSRAM)
//  JC3248W535 wiring: CS=45, CLK=47, D0=21, D1=48, D2=40, D3=39
//  Panel physical size: 320 (width) × 480 (height) portrait
//  LVGL logical size:   480 × 320 landscape (rotation handled in flush_cb)
// ===========================================================================
static Arduino_DataBus* bus = new Arduino_ESP32QSPI(
    DISP_CS, DISP_CLK, DISP_D0, DISP_D1, DISP_D2, DISP_D3);

static Arduino_AXS15231B* gfx_raw = new Arduino_AXS15231B(
    bus, GFX_NOT_DEFINED, 0, false, 320, 480);

// Canvas acts as an intermediate frame buffer so we can rotate the image
// in software during the flush callback.
static Arduino_Canvas* gfx = new Arduino_Canvas(320, 480, gfx_raw, 0, 0, 0);

// ===========================================================================
//  Touch panel
// ===========================================================================
static JC3248W535EN touchPanel;

// ===========================================================================
//  LVGL v9 – buffers and handles
// ===========================================================================
static lv_color_t    lvgl_buf1[LVGL_BUF_PIXELS];   // double-buffer for tear-free rendering
static lv_color_t    lvgl_buf2[LVGL_BUF_PIXELS];
static lv_display_t* lv_disp  = nullptr;
static lv_indev_t*   lv_touch = nullptr;
static lv_obj_t*     touch_dot = nullptr;           // debug touch indicator (10 px red dot)

// ===========================================================================
//  Wi-Fi credentials (loaded from NVS, fallback to compile-time defaults)
// ===========================================================================
static Preferences prefs;
static String g_wifiSsid = WIFI_SSID_DEFAULT;
static String g_wifiPass = WIFI_PASS_DEFAULT;
// Deferred WiFi reconnect flag – set by wifiApplyConfig() (called from any
// task), consumed by loop() where blocking for up to WIFI_CONNECT_TIMEOUT_MS
// is acceptable.
static volatile bool s_wifiReconnectPending = false;

// Task handles – stored at file scope so loop() can report stack high-water marks
static TaskHandle_t h_flow2   = nullptr;
static TaskHandle_t h_sensors = nullptr;
static TaskHandle_t h_control = nullptr;
static TaskHandle_t h_lvgl    = nullptr;

// Command queue – LVGL callbacks post here; loop() drains and calls handleCommand().
// 128 bytes per slot covers the longest possible command (WIFI:SET with encoded creds).
QueueHandle_t g_cmdQueue = nullptr;

// ===========================================================================
//  Forward declarations
// ===========================================================================
static void lvgl_flush_cb(lv_display_t*, const lv_area_t*, uint8_t*);
static void lvgl_touch_cb(lv_indev_t*, lv_indev_data_t*);
void        wifiConnect();
static void lvglTask(void*);


// ===========================================================================
//  Wi-Fi helpers
// ===========================================================================

// Load saved WiFi credentials from NVS.
// Falls back to compile-time defaults if nothing is saved.
static void wifiLoadConfig()
{
    prefs.begin(NVS_NAMESPACE, true);
    g_wifiSsid = prefs.getString(NVS_KEY_WIFI_SSID, WIFI_SSID_DEFAULT);
    g_wifiPass = prefs.getString(NVS_KEY_WIFI_PASS, WIFI_PASS_DEFAULT);
    prefs.end();
}

// Persist new WiFi credentials to NVS and update in-memory copies.
static void wifiSaveConfig(const char* ssid, const char* pass)
{
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_WIFI_SSID, ssid ? ssid : "");
    prefs.putString(NVS_KEY_WIFI_PASS, pass ? pass : "");
    prefs.end();
    g_wifiSsid = ssid ? ssid : "";
    g_wifiPass = pass ? pass : "";
}

// Called from control.cpp via WIFI:SET command.
// Must be extern "C" to allow a C++ call from a different translation unit
// without name-mangling issues.
//
// IMPORTANT: this function may be called from the LVGL task (Core 1) when
// the operator uses the on-screen WiFi panel.  wifiConnect() blocks for up
// to WIFI_CONNECT_TIMEOUT_MS (15 s) which would freeze the display.
// Instead, we save the credentials and set a flag; loop() consumes the flag
// on the next iteration and performs the reconnect from a context where
// blocking is acceptable.
extern "C" void wifiApplyConfig(const char* ssid, const char* pass)
{
    wifiSaveConfig(ssid, pass);
    s_wifiReconnectPending = true;
    Serial.println("[WiFi] Reconnect scheduled (deferred to loop())");
}

// Expose current SSID/password for the WiFi config panel in the LVGL UI.
extern "C" const char* wifiGetSsid() { return g_wifiSsid.c_str(); }
extern "C" const char* wifiGetPass() { return g_wifiPass.c_str(); }


// ===========================================================================
//  OTA helpers
// ===========================================================================

// Perform an HTTPS OTA update from OTA_FIRMWARE_URL (config.h).
// Returns true on success (device reboots before return in practice).
// Returns false if WiFi is not connected or the update fails.
bool performHttpsOta(const char* url)
{
    Serial.printf("[OTA] HTTPS OTA from %s\n", url);
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected");
        return false;
    }
    WiFiClientSecure client;
    client.setInsecure();    // skip cert verification for simplicity
    client.setTimeout(10);
    httpUpdate.rebootOnUpdate(false);   // we reboot manually so we can log it
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    Update.onProgress([](int cur, int total) {
        Serial.printf("[OTA] %d%%\n", total ? cur*100/total : 0);
    });
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Failed (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            return false;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No update available");
            return false;
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] OK, rebooting...");
            delay(500);
            ESP.restart();
            return true;
    }
    return false;
}

// Convenience wrapper called from http_server.cpp
bool handleOtaFromHttp() { return performHttpsOta(OTA_FIRMWARE_URL); }


// ===========================================================================
//  LVGL v9 flush callback
//   The JC3248W535 panel is physically 320×480 portrait, but we run LVGL
//   in 480×320 landscape.  The flush callback manually rotates each pixel
//   90° clockwise: (x, y)_LVGL → (319-y, x)_panel.
// ===========================================================================
static void lvgl_flush_cb(lv_display_t* disp,
                           const lv_area_t* area,
                           uint8_t* px_map)
{
    uint32_t  w   = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t  h   = (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t* src = reinterpret_cast<uint16_t*>(px_map);

    for (uint32_t yy = 0; yy < h; ++yy) {
        for (uint32_t xx = 0; xx < w; ++xx) {
            int16_t x  = area->x1 + xx;
            int16_t y  = area->y1 + yy;
            int16_t px = 320 - 1 - y;  // 90° CW rotation
            int16_t py = x;
            if (px >= 0 && px < 320 && py >= 0 && py < 480)
                gfx->drawPixel(px, py, src[yy * w + xx]);
        }
    }
    if (lv_display_flush_is_last(disp)) gfx->flush();
    lv_display_flush_ready(disp);
}


// ===========================================================================
//  LVGL v9 touch read callback
//   Reads raw coordinates from the AXS15231B touch controller and maps them
//   to LVGL's 480×320 coordinate space.
//   The -10 Y offset compensates for a known touch calibration offset on this
//   board variant; adjust if touch feels off.
// ===========================================================================
static void lvgl_touch_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    uint16_t tx = 0, ty = 0;
    bool touched = touchPanel.getTouchPoint(tx, ty);
    if (touched) {
        int32_t lv_x = (int32_t)((float)tx * 1.00f);
        int32_t lv_y = (int32_t)((float)ty * 1.00f - 10.0f);
        // Clamp to display bounds
        if (lv_x < 0)   lv_x = 0;
        if (lv_x > 479) lv_x = 479;
        if (lv_y < 0)   lv_y = 0;
        if (lv_y > 319) lv_y = 319;
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = lv_x;
        data->point.y = lv_y;
        // Move the red touch-dot indicator to the touch point
        if (touch_dot) {
            lv_obj_set_pos(touch_dot, lv_x - 5, lv_y - 5);
            lv_obj_move_foreground(touch_dot);
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}


// ===========================================================================
//  FreeRTOS LVGL task  (Core 1, priority 1)
//   Calls lv_timer_handler() every 5 ms.
//   All LVGL widget operations must happen from this task.
// ===========================================================================
static void lvglTask(void* pvParams)
{
    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


// ===========================================================================
//  wifiConnect
//   Load credentials from NVS, connect, update g_state IP/SSID,
//   and trigger the web UI fetch task (no-op in embedded mode).
// ===========================================================================
void wifiConnect()
{
    wifiLoadConfig();
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifiSsid.c_str(), g_wifiPass.c_str());
    Serial.print("[WiFi] Connecting to "); Serial.println(g_wifiSsid);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - t0) < (uint32_t)WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    stateLock();
    if (WiFi.status() == WL_CONNECTED) {
        g_state.ip   = WiFi.localIP().toString();
        g_state.ssid = WiFi.SSID();
        Serial.print("[WiFi] IP: "); Serial.println(g_state.ip);
        webUiStartFetchTask();   // no-op – embedded UI doesn't need remote fetch
    } else {
        g_state.ip   = "0.0.0.0";
        g_state.ssid = "Not connected";
        Serial.println("[WiFi] Not connected.");
    }
    stateUnlock();
}


void postCommand(const char* cmd)
{
    if (!g_cmdQueue) {
        // Queue not yet created (called during setup) – execute directly
        handleCommand(String(cmd));
        return;
    }
    char buf[128];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (xQueueSend(g_cmdQueue, buf, 0) != pdTRUE) {
        // Queue full – shouldn't happen with 8 slots, log and drop
        Serial.printf("[CMD] postCommand queue full, dropped: %s\n", buf);
    }
}

// ===========================================================================
//  setup()
// ===========================================================================
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] DistillController " FW_VERSION);

    // Log the reason for this boot so crash-reboot cycles are immediately visible
    // in the serial monitor. Common values: POWERON, SW, PANIC, INT_WDT, TASK_WDT,
    // WDT, DEEPSLEEP, BROWNOUT, SDIO.
    esp_reset_reason_t resetReason = esp_reset_reason();
    const char* resetStr = "UNKNOWN";
    switch (resetReason) {
        case ESP_RST_POWERON:   resetStr = "POWERON";   break;
        case ESP_RST_SW:        resetStr = "SW";        break;
        case ESP_RST_PANIC:     resetStr = "PANIC";     break;
        case ESP_RST_INT_WDT:   resetStr = "INT_WDT";  break;
        case ESP_RST_TASK_WDT:  resetStr = "TASK_WDT"; break;
        case ESP_RST_WDT:       resetStr = "WDT";      break;
        case ESP_RST_DEEPSLEEP: resetStr = "DEEPSLEEP"; break;
        case ESP_RST_BROWNOUT:  resetStr = "BROWNOUT"; break;
        default: break;
    }
    Serial.printf("[BOOT] Reset reason: %s (%d)\n", resetStr, (int)resetReason);
    Serial.printf("[BOOT] Free heap: %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    // 1. Shared state + NVS
    //    stateInit() loads all persisted values (processMode, masterPower,
    //    wasRunning, lastTankTempC, thresholds, etc.) from NVS.
    stateInit();
    // Command queue – must exist before lvglTask or any LVGL callback fires
    g_cmdQueue = xQueueCreate(8, 128);
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    // 2. LittleFS (for barrel image and any cached assets)
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Mount failed");
    } else {
        Serial.printf("[LittleFS] OK  total=%lu  used=%lu\n",
                      (unsigned long)LittleFS.totalBytes(),
                      (unsigned long)LittleFS.usedBytes());
    }

    // 3. SSR outputs
    //    All 5 SSRs configured as LEDC PWM outputs and set to 0 (OFF).
    //    Never energise heaters before the control task is running.
    ssrInit();
    ssrAllOff();

    // 4. Display (AXS15231B QSPI)
    if (!gfx->begin()) {
        Serial.println("[DISPLAY] begin() FAILED");
    } else {
        Serial.println("[DISPLAY] OK");
    }
    pinMode(DISP_BL, OUTPUT);
    digitalWrite(DISP_BL, HIGH);   // backlight on
    gfx->fillScreen(BLACK);
    gfx->flush();

    // 5. Touch (AXS15231B I2C)
    if (!touchPanel.begin()) {
        Serial.println("[TOUCH] Init FAILED");
    } else {
        Serial.println("[TOUCH] OK");
    }

    // 6. LVGL v9
    lv_init();
#if LV_USE_LOG
    lv_log_register_print_cb([](lv_log_level_t, const char* buf) {
        Serial.println(buf);
    });
#endif
    // Tick source: millis() gives LVGL the system time for animations/timers
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    // Logical display size: 480×320 landscape
    lv_disp = lv_display_create(480, 320);
    lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);
    lv_display_set_buffers(lv_disp, lvgl_buf1, lvgl_buf2,
                           sizeof(lvgl_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_touch = lv_indev_create();
    lv_indev_set_type(lv_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_touch, lvgl_touch_cb);
    Serial.println("[LVGL] v9 init OK");

    // =========================================================================
    // TENTATIVE AUTO-RESTORE  (NVS data only, no sensor validation)
    //   Set isRunning=true if the process was active before power loss so that
    //   uiInit() and the first web UI poll both start on the Monitor panel.
    //   The full sensor-validated restore below will confirm this or revert it
    //   to false if tank temperature has dropped too far.
    //   masterPower > 0 is checked here so we never tentatively start with 0%.
    // =========================================================================
    {
        stateLock();
        if (g_state.wasRunning &&
            g_state.processMode != 0 &&
            !g_state.safetyTripped &&
            g_state.masterPower > 0.0f) {
            g_state.isRunning = true;
        }
        stateUnlock();
    }

    // 7. Build LVGL UI
    uiInit();

    // Debug touch dot: small red circle that tracks the finger position
    touch_dot = lv_obj_create(lv_screen_active());
    lv_obj_set_size(touch_dot, 10, 10);
    lv_obj_set_style_bg_color(touch_dot, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);

    // 8. Wi-Fi (also updates g_state.ip and triggers webUiStartFetchTask)
    wifiConnect();

    // 9. Sensor task – Core 0, priority 2
    sensorsInit();
    expander2Init();   // second PCF8574 for water flow sensors
    xTaskCreatePinnedToCore(flow2PollTask, "flow2",   4096, nullptr, 2, &h_flow2,   0);
    xTaskCreatePinnedToCore(sensorsTask,   "sensors", 4096, nullptr, 2, &h_sensors, 0);

    // 10. Control task – Core 0, priority 3
    //     Applies masterPower to active SSRs every CONTROL_LOOP_MS.
    controlInit();
    xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 3, &h_control, 0);

    // 11. HTTP server
    httpServerInit();

    // 12. LVGL handler task – Core 1, priority 1
    xTaskCreatePinnedToCore(lvglTask, "lvgl", 8192, nullptr, 1, &h_lvgl, 1);

    // =========================================================================
    // POWER-GLITCH AUTO-RESTORE
    //   Wait 1.2 s for sensorsTask to read the first temperatures, then check
    //   whether the process was interrupted by a short power blip and should
    //   resume automatically.
    //
    //   Conditions for auto-resume:
    //     1. wasRunning == true       (process was active before power loss)
    //     2. processMode != 0         (a valid mode was selected)
    //     3. safetyTripped == false   (no pre-existing safety condition)
    //     4. masterPower > 0          (there is a non-zero power level to resume at)
    //     5. lastTankTempC is valid   (saved tank temp was not offline)
    //     6. t2 is valid              (current tank sensor is online)
    //     7. Current tank temp >= lastTankTempC - AUTO_RESTORE_MAX_TEMP_DROP_C
    //        (tank has not cooled down too much → short blip, not a long outage)
    //
    //   If any condition fails, the process stays stopped and the operator
    //   must press START manually.
    // =========================================================================
    delay(1200);
    {
        stateLock();

        bool prevTankValid = (g_state.lastTankTempC > TEMP_OFFLINE_THRESH);
        bool currTankValid = (g_state.kettleTemp    > TEMP_OFFLINE_THRESH);

        // Only reject if BOTH temperatures are valid AND the drop is confirmed
        // too large. If either reading is unavailable, give the benefit of the
        // doubt — controlTask safety will catch real overtemperature once
        // sensors come online. This matches the design intent:
        //   "resume unless kettle temp dropped more than AUTO_RESTORE_MAX_TEMP_DROP_C"
        bool tempDropOk = true;
        if (prevTankValid && currTankValid) {
            float drop = g_state.lastTankTempC - g_state.kettleTemp;
            tempDropOk = (drop <= AUTO_RESTORE_MAX_TEMP_DROP_C);
        }

        bool shouldAutoResume =
            g_state.wasRunning    &&
            g_state.processMode != 0 &&
            !g_state.safetyTripped &&
            g_state.masterPower > 0.0f &&
            tempDropOk;   // prevTankValid intentionally NOT required

        Serial.printf("[BOOT] Auto-restore: wasRunning=%d mode=%d tripped=%d power=%.0f "
                      "prevValid=%d currValid=%d tempDropOk=%d → %s\n",
                      (int)g_state.wasRunning, g_state.processMode,
                      (int)g_state.safetyTripped, g_state.masterPower,
                      (int)prevTankValid, (int)currTankValid, (int)tempDropOk,
                      shouldAutoResume ? "RESUME" : "SKIP");

        if (shouldAutoResume) {
            g_state.isRunning = true;
            Serial.printf("[BOOT] Auto-resume: kettle %.1f°C (saved %.1f°C, drop OK), power %.0f%%\n",
                          g_state.kettleTemp, g_state.lastTankTempC, g_state.masterPower);
        } else {
            g_state.isRunning = false;   // revert tentative restore

            if (g_state.wasRunning) {
                float drop = (prevTankValid && currTankValid)
                             ? g_state.lastTankTempC - g_state.kettleTemp : 9999.0f;
                Serial.printf(
                    "[BOOT] Auto-resume rejected: "
                    "mode=%d tripped=%d power=%.0f prevTankValid=%d currTankValid=%d drop=%.1fC\n",
                    g_state.processMode, (int)g_state.safetyTripped,
                    g_state.masterPower, (int)prevTankValid, (int)currTankValid, drop
                );
            }
        }

        stateUnlock();
        uiRequestRefresh();   // immediately update LVGL panel after restore decision
    }

    Serial.println("[BOOT] Setup complete.");
}


// ===========================================================================
//  loop()
//   Core 1 (Arduino default).
//   Handles HTTP requests, triggers periodic LVGL refresh, and keeps
//   g_state.ip in sync with the live WiFi status.
// ===========================================================================
static uint32_t s_lastRefresh  = 0;
static uint32_t s_lastDiag     = 0;    // periodic diagnostic log interval

void loop()
{
// Deferred WiFi reconnect – triggered by WIFI:SET command from either UI.
    if (s_wifiReconnectPending) {
        s_wifiReconnectPending = false;
        WiFi.disconnect(true, true);
        delay(100);
        Serial.println("[WiFi] Reconnecting with new config...");
        wifiConnect();
    }

    // Drain LVGL-deferred commands (NVS writes must not run inside lvglTask)
    {
        char buf[128];
        while (xQueueReceive(g_cmdQueue, buf, 0) == pdTRUE) {
            handleCommand(String(buf));
        }
    }

    // Process any pending HTTP client requests
    httpServerHandle();

    // Trigger LVGL widget refresh at LVGL_STATE_REFRESH_MS interval.
    // The actual widget update happens inside the LVGL task via the timer
    // created in uiInit().
    uint32_t now = millis();
    if (now - s_lastRefresh >= (uint32_t)LVGL_STATE_REFRESH_MS) {
        s_lastRefresh = now;
        uiRequestRefresh();
    }

    // Keep g_state.ip current; WiFi can reconnect in the background
    if (WiFi.status() == WL_CONNECTED) {
        stateLock();
        g_state.ip   = WiFi.localIP().toString();
        g_state.ssid = WiFi.SSID();
        stateUnlock();
    }

    // Periodic diagnostic log every 60 s – heap free + task stack high-water marks.
    // Watch for heap trending downward over time (leak) or stack approaching zero
    // (overflow risk).  Remove or gate behind #ifdef once the system is stable.
    if (now - s_lastDiag >= 60000UL) {
        s_lastDiag = now;
        Serial.printf("[DIAG] Heap free: %u  min ever: %u\n",
                      heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                      heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
        Serial.printf("[DIAG] Stack HWM (bytes free) – flow2:%u  sensors:%u  control:%u  lvgl:%u  loop:%u\n",
                      h_flow2   ? uxTaskGetStackHighWaterMark(h_flow2)   : 0,
                      h_sensors ? uxTaskGetStackHighWaterMark(h_sensors) : 0,
                      h_control ? uxTaskGetStackHighWaterMark(h_control) : 0,
                      h_lvgl    ? uxTaskGetStackHighWaterMark(h_lvgl)    : 0,
                      uxTaskGetStackHighWaterMark(nullptr));  // nullptr = current task (loop)
    }

    delay(1);   // yield to other tasks on Core 1
}
