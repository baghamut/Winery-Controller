// =============================================================================
//  config.h  –  DistillController
//  All hardware pin assignments, timing constants, thresholds, and PWM config.
//  Edit this file only – no other file should hardcode pin numbers.
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// Firmware version
// ---------------------------------------------------------------------------
#define FW_VERSION  "1.0.0"

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
// Example: direct link to a .bin on GitHub Releases or GitHub Pages
#define OTA_FIRMWARE_URL  "https://raw.githubusercontent.com/<user>/<repo>/main/DistillController.bin"

// ---------------------------------------------------------------------------
// Embedded Pics
// ---------------------------------------------------------------------------
#define ICON_CROWN_PATH   "S:/icons/crown.png"
#define ICON_BARREL_PATH  "S:/icons/barrel.png"

// ---------------------------------------------------------------------------
// Display / Touch pin mapping  (JC3248W535 / JC3248W535C schematic)
// ---------------------------------------------------------------------------

// QSPI display (used by Arduino_GFX_Library in DistillController.ino)
#define DISP_CS     45
#define DISP_CLK    47
#define DISP_D0     21
#define DISP_D1     48
#define DISP_D2     40
#define DISP_D3     39
#define DISP_BL      1    // backlight GPIO

// Touch I2C (used by JC3248W535EN-Touch-LCD library)
#define TOUCH_SDA    4
#define TOUCH_SCL    8
#define TOUCH_INT    3
#define TOUCH_RST   -1

// ---------------------------------------------------------------------------
// LVGL display buffer
// ---------------------------------------------------------------------------
// 480 × 10 lines × 2 bytes (RGB565) = 9600 bytes per buffer
#define LVGL_BUF_PIXELS  (480 * 10)

// ---------------------------------------------------------------------------
// External connector pin mapping
// ---------------------------------------------------------------------------

// --- SSR outputs ---
#define PIN_SSR1        5
#define PIN_SSR2        6
#define PIN_SSR3        7
#define PIN_SSR4        15
#define PIN_SSR5        16

// --- 1-Wire DS18B20 ---
#define PIN_ONEWIRE     18

// --- Pressure sensor (ADC) ---
#define PIN_PRESSURE    17

// --- Water level ---
#define PIN_LEVEL       14

// --- Water flow sensor ---
#define PIN_FLOW        46

// ---------------------------------------------------------------------------
// LEDC / SSR PWM  (ESP32 Arduino core v3: pin-based API)
// ---------------------------------------------------------------------------
#define SSR_COUNT           5
#define SSR_LEDC_FREQ_HZ    10      // 10 Hz time-proportional period
#define SSR_LEDC_RESOLUTION 8       // 8-bit duty (0–255)

// ---------------------------------------------------------------------------
// DS18B20 / temperature
// ---------------------------------------------------------------------------
#define DS18B20_RESOLUTION  12
#define TEMP_OFFLINE_THRESH -120.0f
#define MAX_SENSORS         5

// ---------------------------------------------------------------------------
// Pressure sensor scaling
// ---------------------------------------------------------------------------
#define ADC_MAX             4095.0f
#define PRESSURE_MAX_BAR    0.1f   // 0–0.1 bar sensor, 0–3.3 V output
#define PRESSURE_OFFSET     0.0f

// ---------------------------------------------------------------------------
// Water flow sensor
// ---------------------------------------------------------------------------
#define FLOW_PULSES_PER_LITRE    450.0f
#define FLOW_COMPUTE_INTERVAL_MS 1000

// ---------------------------------------------------------------------------
// Safety thresholds
// ---------------------------------------------------------------------------
#define SAFETY_TEMP_MAX_C       95.0f
#define SAFETY_PRESS_MAX_BAR    0.09f  // 90% of 0.1 bar FS

// ---------------------------------------------------------------------------
// Sensor color-coding thresholds (defaults – overridden at runtime via web UI)
// Temperatures: green < WARN <= orange < DANGER <= red
// Pressure:     green < WARN <= orange < DANGER <= red
// Two sets: Distillation (_D) and Rectification (_R)
// ---------------------------------------------------------------------------
// Distillation temp thresholds (°C) – per sensor 0/1/2
#define THRESH_D_TW0   80.0f
#define THRESH_D_TD0   92.0f
#define THRESH_D_TW1   80.0f
#define THRESH_D_TD1   92.0f
#define THRESH_D_TW2   80.0f
#define THRESH_D_TD2   92.0f
// Rectification temp thresholds (°C) – per sensor 0/1/2
#define THRESH_R_TW0   78.0f
#define THRESH_R_TD0   88.0f
#define THRESH_R_TW1   78.0f
#define THRESH_R_TD1   88.0f
#define THRESH_R_TW2   78.0f
#define THRESH_R_TD2   88.0f
// Pressure thresholds (bar)
#define THRESH_D_PW    0.06f
#define THRESH_D_PD    0.08f
#define THRESH_R_PW    0.06f
#define THRESH_R_PD    0.08f

// ---------------------------------------------------------------------------
// Control loop
// ---------------------------------------------------------------------------
#define CONTROL_LOOP_MS     500

// (PI constants kept in case you later reintroduce temperature control)
#define PI_KP               1.5f
#define PI_KI               0.05f
#define PI_INTEGRAL_CLAMP   100.0f

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
#define WIFI_SSID_DEFAULT        "Alex"
#define WIFI_PASS_DEFAULT        "0544759839"
#define WIFI_CONNECT_TIMEOUT_MS  15000

// ---------------------------------------------------------------------------
// NVS keys
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE      "distill"
#define NVS_KEY_VALID      "lastValid"
#define NVS_KEY_PMODE      "pmode"
#define NVS_KEY_TEMP_MAX   "tempMax"
#define NVS_KEY_PRES_MAX   "presMax"
// Wi-Fi (user-configurable)
#define NVS_KEY_WIFI_SSID  "wifiSsid"
#define NVS_KEY_WIFI_PASS  "wifiPass"
// ssrOn[i]   → "on0".."on4"
// ssrPower[i]→ "pwr0".."pwr4"

// ---------------------------------------------------------------------------
// UI refresh
// ---------------------------------------------------------------------------
#define LVGL_STATE_REFRESH_MS  500
#define HTTP_POLL_INTERVAL_MS  3000

// ---------------------------------------------------------------------------
// UI Colors (for Arduino_GFX test fills etc.)
// ---------------------------------------------------------------------------
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED   0xF800
// etc.