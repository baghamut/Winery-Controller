// =============================================================================
//  DistillController.ino  – JC3248W535 / ESP32-S3, LVGL v9, landscape UI
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
#include "ssr.h"
#include "control.h"
#include "http_server.h"
#include "ui_lvgl.h"
#include "web_ui.h"

// ===========================================================================
//  AXS15231B display via QSPI + Canvas (frame buffer in PSRAM)
// ===========================================================================

// QSPI bus
static Arduino_DataBus* bus = new Arduino_ESP32QSPI(
    DISP_CS /*CS*/, DISP_CLK /*CLK*/,
    DISP_D0 /*D0*/, DISP_D1 /*D1*/, DISP_D2 /*D2*/, DISP_D3 /*D3*/
);

// AXS15231B raw driver: native 320x480 portrait
static Arduino_AXS15231B* gfx_raw = new Arduino_AXS15231B(
    bus,
    GFX_NOT_DEFINED,  // RST
    0,                // rotation – keep 0 here
    false,            // IPS
    320,              // native width
    480               // native height
);

// Canvas: native size, NO rotation (we rotate in LVGL flush)
static Arduino_Canvas* gfx = new Arduino_Canvas(
    320,              // canvas width  = panel width
    480,              // canvas height = panel height
    gfx_raw,
    0, 0,
    0                 // rotation: 0
);

// ===========================================================================
//  Touch panel
// ===========================================================================
static JC3248W535EN touchPanel;

// ===========================================================================
//  LVGL v9 – buffers and handles
// ===========================================================================

// e.g. in config.h: #define LVGL_BUF_PIXELS (480*10)
static lv_color_t   lvgl_buf1[LVGL_BUF_PIXELS];
static lv_color_t   lvgl_buf2[LVGL_BUF_PIXELS];
static lv_display_t* lv_disp  = nullptr;
static lv_indev_t*   lv_touch = nullptr;
static lv_obj_t*     touch_dot = nullptr;

// ===========================================================================
//  Wi-Fi + NVS helpers
// ===========================================================================
static Preferences prefs;

// Current Wi-Fi config in RAM (defaults from config.h)
static String g_wifiSsid = WIFI_SSID_DEFAULT;
static String g_wifiPass = WIFI_PASS_DEFAULT;

// ===========================================================================
//  Forward declarations
// ===========================================================================
static void lvgl_flush_cb(lv_display_t* disp,
                          const lv_area_t* area,
                          uint8_t* px_map);
static void lvgl_touch_cb(lv_indev_t* indev, lv_indev_data_t* data);
void wifiConnect();
static void lvglTask(void* pvParams);

// ===========================================================================
//  Wi-Fi helpers
// ===========================================================================
static void wifiLoadConfig()
{
    prefs.begin(NVS_NAMESPACE, true); // read-only
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

// Exposed to UI for SAVE action
extern "C" void wifiApplyConfig(const char* ssid, const char* pass)
{
    wifiSaveConfig(ssid, pass);
    WiFi.disconnect(true, true);
    delay(100);
    Serial.println("[WiFi] Reconnecting with new config...");
    extern void wifiConnect();
    wifiConnect();
}

// Exposed to UI for initial text fields
extern "C" const char* wifiGetSsid()
{
    return g_wifiSsid.c_str();
}

extern "C" const char* wifiGetPass()
{
    return g_wifiPass.c_str();
}

// ===========================================================================
//  OTA Handler
// ===========================================================================
bool performHttpsOta(const char* url)
{
    Serial.printf("[OTA] HTTPS OTA from %s\n", url);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    Update.onProgress([](int cur, int total) {
        int pct = total ? (cur * 100 / total) : 0;
        Serial.printf("[OTA] Progress: %d%% (%d / %d)\n", pct, cur, total);
    });

    t_httpUpdate_return ret = httpUpdate.update(client, url);

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Update failed. Error (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            return false;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No update available");
            return false;

        case HTTP_UPDATE_OK:
            Serial.println("[OTA] Update OK, rebooting...");
            delay(500);
            ESP.restart();
            return true;
    }
    return false;
}

bool handleOtaFromHttp()
{
    return performHttpsOta(OTA_FIRMWARE_URL);
}

// ===========================================================================
//  LVGL v9 flush callback – rotate 480x320 LVGL into 320x480 panel
// ===========================================================================
static void lvgl_flush_cb(lv_display_t* disp,
                          const lv_area_t* area,
                          uint8_t* px_map)
{
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t* src = reinterpret_cast<uint16_t*>(px_map);

    for (uint32_t yy = 0; yy < h; ++yy) {
        for (uint32_t xx = 0; xx < w; ++xx) {
            int16_t x = area->x1 + xx;   // 0..479
            int16_t y = area->y1 + yy;   // 0..319

            int16_t px = 320 - 1 - y;    // 0..319
            int16_t py = x;              // 0..479

            if (px >= 0 && px < 320 && py >= 0 && py < 480) {
                uint16_t c = src[yy * w + xx];
                gfx->drawPixel(px, py, c);
            }
        }
    }

    if (lv_display_flush_is_last(disp)) {
        gfx->flush();
    }
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
        const float Y_OFFSET = 10.0f;
        const float Y_SCALE  = 1.00f;
        const float X_SCALE  = 1.00f;

        float lx = (float)tx * X_SCALE;
        float ly = (float)ty * Y_SCALE - Y_OFFSET;

        int32_t lv_x = (int32_t)lx;
        int32_t lv_y = (int32_t)ly;

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

        Serial.printf("[TOUCH] tx=%u ty=%u -> lv_x=%ld lv_y=%ld\n",
                      tx, ty, (long)lv_x, (long)lv_y);
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
//  Wi-Fi helper
// ===========================================================================
void wifiConnect()
{
    wifiLoadConfig();

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifiSsid.c_str(), g_wifiPass.c_str());
    Serial.print("[WiFi] Connecting to ");
    Serial.println(g_wifiSsid);
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

    // 1. Shared state + NVS
    stateInit();
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    // 2. LittleFS (можно оставить для других файлов, иконке он уже не нужен)
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Mount failed – filesystem unavailable");
    } else {
        Serial.printf("[LittleFS] OK  total=%lu  used=%lu  free=%lu bytes\n",
                      (unsigned long)LittleFS.totalBytes(),
                      (unsigned long)LittleFS.usedBytes(),
                      (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()));
    }

    // 3. SSR outputs
    ssrInit();
    ssrAllOff();

    // 4. Display init
    if (!gfx->begin()) {
        Serial.println("[DISPLAY] Arduino_GFX begin() FAILED – check PSRAM / wiring");
    } else {
        Serial.println("[DISPLAY] Arduino_GFX OK");
    }
    pinMode(DISP_BL, OUTPUT);
    digitalWrite(DISP_BL, HIGH);

    gfx->fillScreen(BLACK);
    gfx->flush();

    // 5. Touch init
    if (!touchPanel.begin()) {
        Serial.println("[TOUCH] Init FAILED – check wiring (SDA=4, SCL=8)");
    } else {
        Serial.println("[TOUCH] OK");
    }

    // 6. LVGL v9 init
    lv_init();

#if LV_USE_LOG
    lv_log_register_print_cb([](lv_log_level_t level, const char * buf) {
        Serial.println(buf);
    });
#endif

    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    lv_disp = lv_display_create(480, 320);
    lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);
    lv_display_set_buffers(lv_disp,
                           lvgl_buf1, lvgl_buf2,
                           sizeof(lvgl_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_touch = lv_indev_create();
    lv_indev_set_type(lv_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_touch, lvgl_touch_cb);

    Serial.println("[LVGL] v9 init OK");

    // 7. Build LVGL UI
    uiInit();

    // Touch debug dot
    touch_dot = lv_obj_create(lv_screen_active());
    lv_obj_set_size(touch_dot, 10, 10);
    lv_obj_set_style_bg_color(touch_dot, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);

    // 8. Wi-Fi
    wifiConnect();

    // 9. Sensor task – Core 0, priority 2
    sensorsInit();
    xTaskCreatePinnedToCore(sensorsTask, "sensors", 4096,
                            nullptr, 2, nullptr, 0);

    // 10. Control task – Core 0, priority 3
    controlInit();
    xTaskCreatePinnedToCore(controlTask, "control", 4096,
                            nullptr, 3, nullptr, 0);

    // 11. HTTP server
    httpServerInit();

    // 12. LVGL handler task – Core 1, priority 1
    xTaskCreatePinnedToCore(lvglTask, "lvgl", 8192,
                            nullptr, 1, nullptr, 1);

    Serial.println("[BOOT] Setup complete.");
}

// ===========================================================================
//  loop()
// ===========================================================================
static uint32_t s_lastRefresh = 0;

void loop()
{
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

    delay(1);
}