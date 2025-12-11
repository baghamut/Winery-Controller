#pragma once

// --------- VERSION & GITHUB OTA CONFIG ---------
#define FIRMWARE_VERSION "2.0.0"
//- Unified Sensor System
//- Enhanced Web Interface
//- Hardware Remapped
//- Bug Fixes
#define GH_OWNER "baghamut"
#define GH_REPO "Winery-Controller"

// --------- PINS ---------
#define ONE_WIRE_BUS 17

// SSRs moved to free up ADC1 pins (5, 6, 7) for analog sensors
#define DISTILLER_PWM1 14  // Changed from 5
#define DISTILLER_PWM2 15  // Changed from 6
#define DISTILLER_PWM3 16  // Changed from 7
#define RECTIFIER_PWM1 18  // Changed from 14
#define RECTIFIER_PWM2 46  // Changed from 15

// --------- WIFI ---------
#define WIFI_SSID "SmartHome"
#define WIFI_PASSWORD "0544759839"
