// Sensors.cpp
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "Config.h"
#include "State.h"
#include "Sensors.h"

// ONE_WIRE_BUS from Config.h (GPIO17)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature dsSensors(&oneWire);

// Unified sensor storage
Sensor allSensors[MAX_SENSORS];
int sensorCount = 0;

// Legacy mapping (T1/T2/T3 indices into allSensors[])
int idxT1 = -1;  // Tank
int idxT2 = -1;  // Room  
int idxT3 = -1;  // Column

static void printAddress(const DeviceAddress addr) {
  for (uint8_t i = 0; i < 8; i++) {
    if (addr[i] < 16) Serial.print("0");
    Serial.print(addr[i], HEX);
  }
}

void initSensors() {
  // Scan OneWire bus for DS18B20s
  dsSensors.begin();
  
  Serial.println("=== OneWire DS18B20 scan ===");
  int dsCount = dsSensors.getDeviceCount();
  Serial.print("DS18B20 count: ");
  Serial.println(dsCount);
  
  sensorCount = 0;
  
  DeviceAddress addr;
  for (uint8_t i = 0; i < dsCount && sensorCount < MAX_SENSORS; i++) {
    if (dsSensors.getAddress(addr, i)) {
      allSensors[sensorCount].type = "temp";
      allSensors[sensorCount].pin = -1;  // Bus sensor
      allSensors[sensorCount].addr = "";
      for (uint8_t j = 0; j < 8; j++) {
        if (addr[j] < 16) allSensors[sensorCount].addr += "0";
        allSensors[sensorCount].addr += String(addr[j], HEX);
      }
      allSensors[sensorCount].idx = sensorCount;
      allSensors[sensorCount].value = -127.0f;
      
      Serial.print("Temp sensor ");
      Serial.print(sensorCount);
      Serial.print(" addr: ");
      printAddress(addr);
      Serial.println();
      
      sensorCount++;
    }
  }
  
  dsSensors.setResolution(12);
  Serial.println("=============================");
  
  // Default temp mapping
  if (sensorCount > 0) idxT1 = 0;
  if (sensorCount > 1) idxT2 = 1; 
  if (sensorCount > 2) idxT3 = 2;
}

void registerSensor(String type, int pin) {
  if (sensorCount >= MAX_SENSORS) {
    Serial.println("MAX_SENSORS reached");
    return;
  }
  
  allSensors[sensorCount].type = type;
  allSensors[sensorCount].pin = pin;
  allSensors[sensorCount].addr = "";
  allSensors[sensorCount].idx = sensorCount;
  allSensors[sensorCount].value = 0.0f;
  
  Serial.print("Registered ");
  Serial.print(type.c_str());
  Serial.print(" on GPIO");
  Serial.println(pin);
  
  sensorCount++;
}

// Read single sensor by index
float readByIndex(int idx) {
  if (idx < 0 || idx >= sensorCount) return -127.0f;
  
  Sensor& s = allSensors[idx];
  
  if (s.type == "temp") {
    // DS18B20 via OneWire index
    DeviceAddress addr;
    if (dsSensors.getAddress(addr, idx)) {
      return dsSensors.getTempC(addr);
    }
    return -127.0f;
  }
  else if (s.type == "pressure") {
    // MPX5010: 0.2V-4.7V → 0-10kPa (3.3V ADC)
    int raw = analogRead(s.pin);
    float voltage = (raw * 3.3f) / 4095.0f;
    float pressure = (voltage - 0.2f) / 4.5f * 10.0f;  // kPa
    return pressure;
  }
  else if (s.type == "level_cap") {
    // Capacitive strip: raw → 0-100%
    int raw = analogRead(s.pin);
    return (raw / 4095.0f) * 100.0f;
  }
  // Ultrasonic needs trig/echo pair - not implemented yet
  
  return 0.0f;
}

void updateSensors() {
  // Update all discovered sensors
  if (sensorCount > 0) {
    if (allSensors[0].type == "temp") {
      dsSensors.requestTemperatures();  // Batch read all DS18B20s
    }
    
    // Read each sensor
    for (int i = 0; i < sensorCount; i++) {
      allSensors[i].value = readByIndex(i);
    }
    
    // Update legacy temps
    tankTemp = readByIndex(idxT1);
    roomTemp = readByIndex(idxT2);
    colTemp  = readByIndex(idxT3);
  }
  
  // Debug output
  Serial.print("Sensors: ");
  for (int i = 0; i < sensorCount; i++) {
    Serial.print(allSensors[i].type[0]);
    Serial.print(allSensors[i].value, 1);
    if (i < sensorCount-1) Serial.print(" ");
  }
  Serial.print(" | T1=");
  Serial.print(tankTemp, 1);
  Serial.print(" T2=");
  Serial.print(roomTemp, 1);
  Serial.print(" T3=");
  Serial.println(colTemp, 1);
}
