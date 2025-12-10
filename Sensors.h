#pragma once

#include <OneWire.h>
#include <DallasTemperature.h>

// Max supported DS18B20 sensors on the bus
static const uint8_t MAX_SENSORS = 8;

// Discovered sensors and mapping, used by WebUI.cpp too
extern DeviceAddress foundSensors[MAX_SENSORS];
extern int sensorCount;

// Logical mapping indices for legacy T1/T2/T3 roles
extern int idxT1;
extern int idxT2;
extern int idxT3;

void initSensors();
void updateTemperatures();
