// Sensors.h
#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Max supported sensors (all types)
static const uint8_t MAX_SENSORS = 16;

// Unified sensor structure
struct Sensor {
  String type;      // "temp", "pressure", "level_us", "level_cap"
  int pin;          // GPIO (-1 for bus sensors)
  float value;      // Latest reading
  String addr;      // For OneWire (empty for GPIO sensors)
  int idx;          // Discovery index 0..MAX_SENSORS-1
};

// Discovered sensors array + count
extern Sensor allSensors[MAX_SENSORS];
extern int sensorCount;

// Legacy temperature mapping (for backwards compat)
extern int idxT1, idxT2, idxT3;

// Public API
void initSensors();
void updateSensors();
float readByIndex(int idx);
void registerSensor(String type, int pin);
