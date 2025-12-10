#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "Config.h"
#include "State.h"
#include "Sensors.h"

// ONE_WIRE_BUS is defined in Config.h (GPIO17)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Global discovery + mapping
DeviceAddress foundSensors[MAX_SENSORS];
int sensorCount = 0;

int idxT1 = -1;  // T1 - tank
int idxT2 = -1;  // T2 - room
int idxT3 = -1;  // T3 - column

static void printAddress(const DeviceAddress addr) {
  for (uint8_t i = 0; i < 8; i++) {
    if (addr[i] < 16) Serial.print("0");
    Serial.print(addr[i], HEX);
  }
}

void initSensors() {
  sensors.begin();

  Serial.println("=== Dallas scan ===");
  int count = sensors.getDeviceCount();
  Serial.print("Device count: ");
  Serial.println(count);

  sensorCount = 0;

  DeviceAddress a;
  for (uint8_t i = 0; i < count && sensorCount < MAX_SENSORS; i++) {
    if (sensors.getAddress(a, i)) {
      memcpy(foundSensors[sensorCount], a, 8);

      Serial.print("Idx ");
      Serial.print(sensorCount);
      Serial.print(" addr: ");
      printAddress(foundSensors[sensorCount]);
      Serial.println();

      sensorCount++;
    }
  }
  Serial.println("====================");

  sensors.setResolution(12);

  // Default mapping: first 3 sensors (if present)
  if (sensorCount > 0) {
    idxT1 = 0;
  } else {
    idxT1 = -1;
  }
  if (sensorCount > 1) {
    idxT2 = 1;
  } else {
    idxT2 = -1;
  }
  if (sensorCount > 2) {
    idxT3 = 2;
  } else {
    idxT3 = -1;
  }
}

float readByIndex(int idx);

float readByIndex(int idx) {
  if (idx < 0 || idx >= sensorCount) return -127.0f;
  return sensors.getTempC(foundSensors[idx]);
}


void updateTemperatures() {
  sensors.requestTemperatures();

  tankTemp = readByIndex(idxT1);
  roomTemp = readByIndex(idxT2);
  colTemp  = readByIndex(idxT3);

  Serial.print("T1=");
  Serial.print(tankTemp);
  Serial.print(" T2=");
  Serial.print(roomTemp);
  Serial.print(" T3=");
  Serial.println(colTemp);
}
