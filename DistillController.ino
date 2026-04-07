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
//    6a. lvglFsInit()     – register 'L:' LittleFS driver for barrel.png
//    7. uiInit()          – build all LVGL panels
//    8. wifiConnect()     – connect + trigger webUiStartFetchTask()
//    9. sensorsTask       – Core 0, priority 2
//   10. controlTask       – Core 0, priority 3
//   11. httpServerInit()  – start HTTPS server port 443
//   12. lvglTask          – Core 1, priority 1
//   13. Auto-restore check (see below)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include "mbedtls/platform.h"   // mbedtls_platform_set_calloc_free

// DDNS
#include "ddns.h"

// OTA
#include "ota.h"

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
//  mbedTLS PSRAM allocator
//  Redirects all mbedTLS heap allocations (SSL record buffers, handshake
//  state, certificate parsing — ~40 KB per session) to PSRAM instead of
//  internal DRAM.  Frees ~160 KB of internal DRAM for up to 4 concurrent
//  TLS sessions, eliminating the MBEDTLS_ERR_MEM_ALLOC_FAILED (-0x0050)
//  failures that blocked POST commands while SSE held a socket.
//  psram_calloc / psram_free are registered before httpServerInit().
// ===========================================================================
static void* psram_calloc(size_t n, size_t size)
{
    return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
}

static void psram_free(void* ptr)
{
    free(ptr);
}

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
static lv_color_t    lvgl_buf1[LVGL_BUF_PIXELS];
static lv_color_t    lvgl_buf2[LVGL_BUF_PIXELS];
static lv_display_t* lv_disp  = nullptr;
static lv_indev_t*   lv_touch = nullptr;
static lv_obj_t*     touch_dot = nullptr;

// ===========================================================================
//  Wi-Fi credentials (loaded from NVS, fallback to compile-time defaults)
// ===========================================================================
static Preferences prefs;
static String g_wifiSsid = WIFI_SSID_DEFAULT;
static String g_wifiPass = WIFI_PASS_DEFAULT;
static volatile bool s_wifiReconnectPending = false;

// Task handles
static TaskHandle_t h_flow2   = nullptr;
static TaskHandle_t h_sensors = nullptr;
static TaskHandle_t h_control = nullptr;
static TaskHandle_t h_lvgl    = nullptr;

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

static void wifiLoadConfig()
{
    prefs.begin(NVS_NAMESPACE, true);
    g_wifiSsid = prefs.getString(NVS_KEY_WIFI_SSID, WIFI_SSID_DEFAULT);
    g_wifiPass = prefs.getString(NVS_KEY_WIFI_PASS, WIFI_PASS_DEFAULT);
    prefs.end();
}

static void wifiSaveConfig(const char* ssid, const char* pass)
{
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_WIFI_SSID, ssid ? ssid : "");
    prefs.putString(NVS_KEY_WIFI_PASS, pass ? pass : "");
    prefs.end();
    g_wifiSsid = ssid ? ssid : "";
    g_wifiPass = pass ? pass : "";
}

extern "C" void wifiApplyConfig(const char* ssid, const char* pass)
{
    wifiSaveConfig(ssid, pass);
    s_wifiReconnectPending = true;
    Serial.println("[WiFi] Reconnect scheduled (deferred to loop())");
}

extern "C" const char* wifiGetSsid() { return g_wifiSsid.c_str(); }
extern "C" const char* wifiGetPass() { return g_wifiPass.c_str(); }


// ===========================================================================
//  LVGL v9 flush callback
//   90° CW rotation: (x, y)_LVGL → (319-y, x)_panel
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
            int16_t px = 320 - 1 - y;
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
// ===========================================================================
static void lvgl_touch_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    uint16_t tx = 0, ty = 0;
    bool touched = touchPanel.getTouchPoint(tx, ty);
    if (touched) {
        int32_t lv_x = (int32_t)((float)tx * 1.00f);
        int32_t lv_y = (int32_t)((float)ty * 1.00f - 10.0f);
        if (lv_x < 0)   lv_x = 0;
        if (lv_x > 479) lv_x = 479;
        if (lv_y < 0)   lv_y = 0;
        if (lv_y > 319) lv_y = 319;
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = lv_x;
        data->point.y = lv_y;
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
// ===========================================================================
static void lvglTask(void* pvParams)
{
    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


// ===========================================================================
//  postCommand — called from ui_lvgl.cpp (LVGL task, Core 1)
// ===========================================================================
void postCommand(const char* cmd)
{
    handleCommand(String(cmd));
}


// ===========================================================================
//  lvglFsInit — registers LittleFS as LVGL 'L:' driver
// ===========================================================================
static void lvglFsInit()
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'L';

    drv.open_cb = [](lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) -> void* {
        File* fp = new File(LittleFS.open(path, mode == LV_FS_MODE_WR ? "w" : "r"));
        if (!*fp) { delete fp; return nullptr; }
        return fp;
    };

    drv.close_cb = [](lv_fs_drv_t*, void* f) -> lv_fs_res_t {
        ((File*)f)->close();
        delete (File*)f;
        return LV_FS_RES_OK;
    };

    drv.read_cb = [](lv_fs_drv_t*, void* f,
                     void* buf, uint32_t btr, uint32_t* br) -> lv_fs_res_t {
        *br = ((File*)f)->read((uint8_t*)buf, btr);
        return LV_FS_RES_OK;
    };

    drv.seek_cb = [](lv_fs_drv_t*, void* f,
                     uint32_t pos, lv_fs_whence_t w) -> lv_fs_res_t {
        SeekMode m = (w == LV_FS_SEEK_CUR) ? SeekCur :
                     (w == LV_FS_SEEK_END) ? SeekEnd : SeekSet;
        ((File*)f)->seek(pos, m);
        return LV_FS_RES_OK;
    };

    drv.tell_cb = [](lv_fs_drv_t*, void* f, uint32_t* pos) -> lv_fs_res_t {
        *pos = ((File*)f)->position();
        return LV_FS_RES_OK;
    };

    lv_fs_drv_register(&drv);
    Serial.println("[LVGL-FS] LittleFS driver registered as 'L:'");
}


// ===========================================================================
//  wifiConnect
// ===========================================================================
void wifiConnect()
{
    wifiLoadConfig();
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // discard saved lease
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
        webUiStartFetchTask();
    } else {
        g_state.ip   = "0.0.0.0";
        g_state.ssid = "Not connected";
        Serial.println("[WiFi] Not connected.");
    }
    stateUnlock();
}


// ===========================================================================
//  setup()
// ===========================================================================
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] DistillController " FW_VERSION);

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
    stateInit();
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    // 2. LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Mount failed");
    } else {
        Serial.printf("[LittleFS] OK  total=%lu  used=%lu\n",
                      (unsigned long)LittleFS.totalBytes(),
                      (unsigned long)LittleFS.usedBytes());
    }

    // 3. SSR outputs
    ssrInit();
    ssrAllOff();

    // 4. Display (AXS15231B QSPI)
    if (!gfx->begin()) {
        Serial.println("[DISPLAY] begin() FAILED");
    } else {
        Serial.println("[DISPLAY] OK");
    }
    pinMode(DISP_BL, OUTPUT);
    digitalWrite(DISP_BL, HIGH);
    gfx->fillScreen(BLACK);
    gfx->flush();

    // 5. Touch
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
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    lv_disp = lv_display_create(480, 320);
    lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);
    lv_display_set_buffers(lv_disp, lvgl_buf1, lvgl_buf2,
                           sizeof(lvgl_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_touch = lv_indev_create();
    lv_indev_set_type(lv_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_touch, lvgl_touch_cb);
    Serial.println("[LVGL] v9 init OK");

    // 6a. LittleFS driver for LVGL
    lvglFsInit();

    // =========================================================================
    // TENTATIVE AUTO-RESTORE
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

    touch_dot = lv_obj_create(lv_screen_active());
    lv_obj_set_size(touch_dot, 10, 10);
    lv_obj_set_style_bg_color(touch_dot, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);

    // 8. Wi-Fi
    wifiConnect();

    // 9. Sensor task – Core 0, priority 2
    sensorsInit();
    expander2Init();
    xTaskCreatePinnedToCore(flow2PollTask, "flow2",   4096, nullptr, 2, &h_flow2,   0);
    xTaskCreatePinnedToCore(sensorsTask,   "sensors", 4096, nullptr, 2, &h_sensors, 0);

    // 10. Control task – Core 0, priority 3
    controlInit();
    xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 3, &h_control, 0);

    // 11. HTTP server
    //     Redirect all mbedTLS allocations to PSRAM before starting the server.
    //     Each TLS session needs ~40 KB; with 4 max_open_sockets that's 160 KB
    //     which would exhaust internal DRAM.  PSRAM has 7+ MB free.
    mbedtls_platform_set_calloc_free(psram_calloc, psram_free);
    Serial.println("[BOOT] mbedTLS → PSRAM allocator active");

    httpServerInit();
    ddnsInit();   // GoDaddy DDNS updater, Core 0
    otaInit();    // Pull OTA check task, Core 0

    // 12. LVGL handler task – Core 1, priority 1
    xTaskCreatePinnedToCore(lvglTask, "lvgl", 8192, nullptr, 1, &h_lvgl, 1);

    // =========================================================================
    // POWER-GLITCH AUTO-RESTORE
    // =========================================================================
    delay(1200);
    {
        stateLock();

        bool prevTankValid = (g_state.lastTankTempC > TEMP_OFFLINE_THRESH);
        bool currTankValid = (g_state.kettleTemp    > TEMP_OFFLINE_THRESH);

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
            tempDropOk;

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
            g_state.isRunning = false;

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
        uiRequestRefresh();
    }

    Serial.println("[BOOT] Setup complete.");
}


// ===========================================================================
//  loop()
// ===========================================================================
static uint32_t s_lastRefresh = 0;
static uint32_t s_lastDiag   = 0;

void loop()
{
    if (s_wifiReconnectPending) {
        s_wifiReconnectPending = false;
        WiFi.disconnect(true, true);
        delay(100);
        Serial.println("[WiFi] Reconnecting with new config...");
        wifiConnect();
    }

    httpServerHandle();

    uint32_t now = millis();
    if (now - s_lastRefresh >= (uint32_t)LVGL_STATE_REFRESH_MS) {
        s_lastRefresh = now;
        uiRequestRefresh();
    }

    if (WiFi.status() == WL_CONNECTED) {
        stateLock();
        g_state.ip   = WiFi.localIP().toString();
        g_state.ssid = WiFi.SSID();
        stateUnlock();
    }

    if (now - s_lastDiag >= 60000UL) {
        s_lastDiag = now;
        Serial.printf("[DIAG] Heap free: %u  min ever: %u\n",
                      heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                      heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
        Serial.printf("[DIAG] Internal DRAM free: %u  largest block: %u\n",
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        Serial.printf("[DIAG] Stack HWM (bytes free) – flow2:%u  sensors:%u  control:%u  lvgl:%u  loop:%u\n",
                      h_flow2   ? uxTaskGetStackHighWaterMark(h_flow2)   : 0,
                      h_sensors ? uxTaskGetStackHighWaterMark(h_sensors) : 0,
                      h_control ? uxTaskGetStackHighWaterMark(h_control) : 0,
                      h_lvgl    ? uxTaskGetStackHighWaterMark(h_lvgl)    : 0,
                      uxTaskGetStackHighWaterMark(nullptr));
    }

    delay(1);
}
