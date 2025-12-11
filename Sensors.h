#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Max supported sensors (all types)
static const uint8_t MAX_SENSORS = 16;

struct Sensor {
  String name;    // Editable display name
  String type;    // "temp", "pressure", "level_cap", ...
  int pin;        // GPIO (-1 for bus sensors)
  float value;    // Latest reading
  String addr;    // OneWire ROM as hex, or empty
  int idx;        // Discovery index
};

extern Sensor allSensors[MAX_SENSORS];
extern int sensorCount;

// Legacy T1/T2/T3 mappings
extern int idxT1;
extern int idxT2;
extern int idxT3;

void initSensors();
void updateSensors();
float readByIndex(int idx);
void registerSensor(const String &type, int pin);

// Enhanced configuration persistence
void loadSensorConfig();
void saveSensorConfig();
void updateSensorMapping(int logicalSlot, int physicalIdx, String name, String type);

// Legacy name-only persistence (kept for backward compatibility)
void loadSensorNames();
void saveSensorNames();
