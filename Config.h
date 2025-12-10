#pragma once

// --------- VERSION & GITHUB OTA CONFIG ---------
#define FIRMWARE_VERSION "1.3.0"
//- Dynamic DS18B20 discovery with per-sensor temps
//- Sensor mapping UI (assign any probe to T1/T2/T3)
//- Split UI into Main/Config screens; moved OTA/Restart to Config
#define GH_OWNER  "baghamut"
#define GH_REPO   "Winery-Controller"

// --------- PINS ---------
#define ONE_WIRE_BUS   17
#define DISTILLER_PWM1 5
#define DISTILLER_PWM2 6
#define DISTILLER_PWM3 7
#define RECTIFIER_PWM1 14
#define RECTIFIER_PWM2 15

// --------- WIFI ---------
#define WIFI_SSID     "SmartHome"
#define WIFI_PASSWORD "0544759839"
