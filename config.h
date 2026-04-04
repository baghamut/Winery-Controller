// =============================================================================
//  config.h  –  DistillController
//  All hardware pin assignments, timing constants, thresholds, and PWM config.
//  Edit this file only – no other file should hardcode pin numbers or tuning
//  constants. Central single source of truth.
// =============================================================================
#pragma once

// ---------------------------------------------------------------------------
// Firmware version
// ---------------------------------------------------------------------------
#define FW_VERSION  "3.5.0"

// ---------------------------------------------------------------------------
// OTA firmware URL
// ---------------------------------------------------------------------------
#define OTA_FIRMWARE_URL  "https://raw.githubusercontent.com/baghamut/Winery-Controller/main/DistillController.bin"

// ---------------------------------------------------------------------------
// Display / Touch pin mapping  (JC3248W535 / JC3248W535C)
//   Display uses QSPI (Quad-SPI) interface → AXS15231B controller
//   Touch uses I²C on dedicated pins
// ---------------------------------------------------------------------------
#define DISP_CS     45    // QSPI chip-select
#define DISP_CLK    47    // QSPI clock
#define DISP_D0     21    // QSPI data bit 0
#define DISP_D1     48    // QSPI data bit 1
#define DISP_D2     40    // QSPI data bit 2
#define DISP_D3     39    // QSPI data bit 3
#define DISP_BL      1    // Backlight GPIO (HIGH = on)

#define TOUCH_SDA    4    // I²C SDA for AXS15231B touch controller
#define TOUCH_SCL    8    // I²C SCL
#define TOUCH_INT    3    // Touch interrupt (active low)
#define TOUCH_RST   -1    // Not connected on this board variant

// ---------------------------------------------------------------------------
// LVGL display buffer
//   480 * 10 = 10 horizontal lines – good balance between RAM and tear-free
// ---------------------------------------------------------------------------
#define LVGL_BUF_PIXELS  (480 * 10)

// ---------------------------------------------------------------------------
// Updated connector pin map
//
// Connector 1 (8-pin): IO5, IO6, IO7, IO15, IO16, IO46, IO9, IO14
//   - SSR1..SSR5 stay native on IO5/6/7/15/16
//   - Flow sensor stays native on IO46 for interrupt-based pulse counting
//   - Pressure sensor moves to native ADC pin IO9
//   - 1-Wire DS18B20 bus moves to native GPIO14
//
// Connector 2 (4-pin): GND, 3.3V, IO17, IO18
//   - Dedicated I2C bus for external GPIO expander
//   - Level switch + Valve1/2/3 move to expander
// ---------------------------------------------------------------------------

// SSR outputs – native LEDC 10 Hz PWM
#define PIN_SSR1        5    // Distillation heater A – SSR1
#define PIN_SSR2        6    // Distillation heater B – SSR2
#define PIN_SSR3        7    // Distillation heater C – SSR3
#define PIN_SSR4        15   // Rectification heater A – SSR4
#define PIN_SSR5        16   // Rectification heater B – SSR5

// Native sensors that must stay on ESP32
#define PIN_PRESSURE    9    // Analog pressure sensor (0.5–4.5 V → ADC on GPIO9)
#define PIN_ONEWIRE     14   // 1-Wire DS18B20 bus (GPIO14)
#define PIN_FLOW        46   // Hall-effect flow sensor (pulse input, interrupt-capable)

// Level is now on the external I2C expander, not on a native GPIO.
#define PIN_LEVEL       (-1)

// ---------------------------------------------------------------------------
// External I2C expander bus (Connector 2)
// ---------------------------------------------------------------------------
#define EXT_I2C_SDA     17   // I2C SDA – shared by both expanders
#define EXT_I2C_SCL     18   // I2C SCL – shared by both expanders

// Expander 1 – PCF8574 at 0x20 (A0=A1=A2=GND): valves + level
#define EXT_I2C_ADDR    0x20
#define EXPANDER_LEVEL    0   // Input  – kettle level switch
#define EXPANDER_VALVE1   1   // Output – valve 1 (Dephlegmator)
#define EXPANDER_VALVE2   2   // Output – valve 2 (Dripper)
#define EXPANDER_VALVE3   3   // Output – valve 3 (Water)
#define EXPANDER_SPARE4   4
#define EXPANDER_SPARE5   5
#define EXPANDER_SPARE6   6
#define EXPANDER_SPARE7   7

// Expander 2 – PCF8574 at 0x21 (A0=HIGH): water flow sensors (polled)
#define EXT2_I2C_ADDR       0x21
#define EXPANDER2_FLOW_DEPHL  0   // Input – Water Dephlegmator flow pulses
#define EXPANDER2_FLOW_COND   1   // Input – Water Condenser flow pulses
// Bits 2–7 spare

// Flow2 polling task interval.  At max ~225 Hz (30 L/min × 450 ppl) a 2ms
// poll window catches every pulse with margin.
#define FLOW2_POLL_MS       2

// ---------------------------------------------------------------------------
// LEDC / SSR PWM
// ---------------------------------------------------------------------------
#define SSR_COUNT           5
#define SSR_LEDC_FREQ_HZ    10    // 10 Hz – safe for SSR zero-crossing on heaters
#define SSR_LEDC_RESOLUTION 8     // 8-bit → max duty = 255

// ---------------------------------------------------------------------------
// Master Power
// ---------------------------------------------------------------------------
#define MASTER_POWER_DEFAULT  0.0f     // Safe boot default – always start off
#define NVS_KEY_MASTER_POWER  "mPwr"   // NVS key – max 15 chars

// ---------------------------------------------------------------------------
// DS18B20 / temperature
// ---------------------------------------------------------------------------
#define DS18B20_RESOLUTION  12         // 12-bit: 0.0625 °C resolution
#define TEMP_OFFLINE_THRESH -20.0f     // Sensor is considered offline below this
#define MAX_SENSORS         8          // DS18B20 slots: Room,Kettle,Pillar1-3,Dephlegm,Reflux,Product

// ---------------------------------------------------------------------------
// Pressure sensor scaling
// ---------------------------------------------------------------------------
#define ADC_MAX             4095.0f
#define PRESSURE_MAX_BAR    0.1f
#define PRESSURE_OFFSET     0.0f

// ---------------------------------------------------------------------------
// Water flow sensor
// ---------------------------------------------------------------------------
#define FLOW_PULSES_PER_LITRE    450.0f    // Calibration: pulses per litre
#define FLOW_COMPUTE_INTERVAL_MS 1000      // Update rate (ms)

// ---------------------------------------------------------------------------
// Safety thresholds
// ---------------------------------------------------------------------------
#define SAFETY_TEMP_MAX_C       95.0f      // Hard temperature ceiling
#define SAFETY_PRESS_MAX_BAR    0.09f      // Hard pressure ceiling

// ---------------------------------------------------------------------------
// Power-glitch auto-restore
// ---------------------------------------------------------------------------
#define AUTO_RESTORE_MAX_TEMP_DROP_C   5.0f

// ---------------------------------------------------------------------------
// Sensor colour-coding thresholds (compile-time defaults)
// ---------------------------------------------------------------------------
// Distillation
#define THRESH_D_TW0   80.0f   // Sensor 1 (Room)  – warn
#define THRESH_D_TD0   92.0f   // Sensor 1 (Room)  – danger
#define THRESH_D_TW1   80.0f   // Sensor 2 (Tank)  – warn
#define THRESH_D_TD1   92.0f   // Sensor 2 (Tank)  – danger
#define THRESH_D_TW2   80.0f   // Sensor 3 (Pillar)– warn
#define THRESH_D_TD2   92.0f   // Sensor 3 (Pillar)– danger
// Rectification
#define THRESH_R_TW0   78.0f
#define THRESH_R_TD0   88.0f
#define THRESH_R_TW1   78.0f
#define THRESH_R_TD1   88.0f
#define THRESH_R_TW2   78.0f
#define THRESH_R_TD2   88.0f
// Pressure
#define THRESH_D_PW    0.06f
#define THRESH_D_PD    0.08f
#define THRESH_R_PW    0.06f
#define THRESH_R_PD    0.08f

// ---------------------------------------------------------------------------
// Control loop
// ---------------------------------------------------------------------------
#define CONTROL_LOOP_MS     500    // SSR apply + safety check interval (ms)

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
// #define WIFI_SSID_DEFAULT  "your_ssid_here"
// #define WIFI_PASS_DEFAULT  "***REMOVED***"
#define WIFI_SSID_DEFAULT        "Alex"
#define WIFI_PASS_DEFAULT        "0544759839"
#define WIFI_CONNECT_TIMEOUT_MS  15000

// ---------------------------------------------------------------------------
// NVS keys
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE      "distill"
#define NVS_KEY_VALID      "lastValid"    // 1 = NVS contains saved state
#define NVS_KEY_PMODE      "pmode"        // processMode (int)
#define NVS_KEY_TEMP_MAX   "tempMax"      // safetyTempMaxC (float)
#define NVS_KEY_PRES_MAX   "presMax"      // safetyPresMaxBar (float)
#define NVS_KEY_WIFI_SSID  "wifiSsid"     // WiFi SSID (String)
#define NVS_KEY_WIFI_PASS  "wifiPass"     // WiFi password (String)
// NVS_KEY_MASTER_POWER defined above (next to MASTER_POWER_DEFAULT)

// ---------------------------------------------------------------------------
// UI refresh
// ---------------------------------------------------------------------------
#define LVGL_STATE_REFRESH_MS  500    // How often loop() triggers uiRequestRefresh()
#define HTTP_POLL_INTERVAL_MS  3000   // Web UI JS poll interval (informational)


// ---------------------------------------------------------------------------
// Valve subsystem
// ---------------------------------------------------------------------------
#define VALVE_COUNT  5    // Dephlegmator(0) Dripper(1) Water(2) Placeholder1(3) Placeholder2(4)
// NVS keys are generated at runtime: "vNos"/"vNoo"/"vNov" (open), "vNcs"/"vNco"/"vNcv" (close)
// where N is the 0-based valve index.  All 4-char keys – well within the 15-char NVS limit.

// ---------------------------------------------------------------------------
// Colours (for GFX test fills, not used in LVGL)
// ---------------------------------------------------------------------------
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED   0xF800
