#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "State.h"
#include "Sensors.h"

// ONE_WIRE_BUS is defined in Config.h (GPIO17)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature dsSensors(&oneWire);

Sensor allSensors[MAX_SENSORS];
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

// ============================================
// ENHANCED CONFIGURATION PERSISTENCE
// ============================================

void loadSensorConfig() {
  if (!LittleFS.exists("/config/sensors.json")) {
    Serial.println("[INFO] No /config/sensors.json, using defaults");
    return;
  }

  File f = LittleFS.open("/config/sensors.json", "r");
  if (!f) {
    Serial.println("[ERROR] Failed to open /config/sensors.json");
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("[ERROR] Failed to parse /config/sensors.json: %s\n", err.c_str());
    return;
  }

  // DEBUG: Print the entire JSON content
  String debugJson;
  serializeJson(doc, debugJson);
  Serial.println("[DEBUG] Loaded JSON content:");
  Serial.println(debugJson);

  // Load sensor map (logical slots T1, T2, T3 -> physical sensor indices)
  JsonArray mapArray = doc["map"];
  if (mapArray && mapArray.size() >= 3) {
    idxT1 = mapArray[0].as<int>();
    idxT2 = mapArray[1].as<int>();
    idxT3 = mapArray[2].as<int>();
    Serial.printf("[INFO] Loaded mapping: T1->%d T2->%d T3->%d\n", idxT1, idxT2, idxT3);
  }

  // Load sensor details (names and types)
  JsonArray sensorsArray = doc["sensors"];
  if (sensorsArray) {
    Serial.printf("[DEBUG] Sensors array size: %d, current sensorCount: %d\n", 
                  sensorsArray.size(), sensorCount);
    
    for (size_t i = 0; i < sensorsArray.size(); i++) {
      JsonObject sensorObj = sensorsArray[i].as<JsonObject>();
      
      // Get values explicitly
      int idx = sensorObj["idx"].as<int>();
      String name = sensorObj["name"].as<String>();
      String type = sensorObj["type"].as<String>();
      
      Serial.printf("[DEBUG] JSON[%d]: idx=%d name='%s' type='%s'\n", 
                    i, idx, name.c_str(), type.c_str());
      
      // Only update if the sensor exists in current array
      if (idx >= 0 && idx < sensorCount) {
        if (name.length() > 0) {
          allSensors[idx].name = name;
          Serial.printf("[DEBUG] ✓ Updated allSensors[%d].name to '%s'\n", 
                        idx, allSensors[idx].name.c_str());
        }
        if (type.length() > 0) {
          allSensors[idx].type = type;
          Serial.printf("[DEBUG] ✓ Updated allSensors[%d].type to '%s'\n", 
                        idx, allSensors[idx].type.c_str());
        }
      } else {
        Serial.printf("[DEBUG] ✗ Skipping idx=%d (sensorCount=%d, will be added later)\n", 
                      idx, sensorCount);
      }
    }
    Serial.println("[INFO] Sensor configuration loaded successfully");
  }
}

void saveSensorConfig() {
  // Ensure LittleFS is mounted
  if (!LittleFS.begin()) {
    Serial.println("[ERROR] LittleFS not mounted, attempting mount...");
    if (!LittleFS.begin(true)) {
      Serial.println("[ERROR] LittleFS mount failed");
      return;
    }
  }

  // Create /config directory if it doesn't exist
  if (!LittleFS.exists("/config")) {
    Serial.println("[INFO] Creating /config directory");
    if (!LittleFS.mkdir("/config")) {
      Serial.println("[ERROR] Failed to create /config directory");
      return;
    }
  }

  DynamicJsonDocument doc(2048);

  // Save sensor map
  JsonArray mapArray = doc.createNestedArray("map");
  mapArray.add(idxT1);
  mapArray.add(idxT2);
  mapArray.add(idxT3);

  // Save sensor details
  JsonArray sensorsArray = doc.createNestedArray("sensors");
  for (int i = 0; i < sensorCount; i++) {
    JsonObject sensor = sensorsArray.createNestedObject();
    sensor["idx"] = allSensors[i].idx;
    sensor["name"] = allSensors[i].name;
    sensor["type"] = allSensors[i].type;
    sensor["pin"] = allSensors[i].pin;
    sensor["addr"] = allSensors[i].addr;
  }

  File f = LittleFS.open("/config/sensors.json", "w");
  if (!f) {
    Serial.println("[ERROR] Failed to open /config/sensors.json for write");
    return;
  }

  size_t written = serializeJson(doc, f);
  f.close();

  if (written > 0) {
    Serial.printf("[INFO] Sensor configuration saved (%d bytes) to /config/sensors.json\n", written);
    
    // Verify the file was written
    if (LittleFS.exists("/config/sensors.json")) {
      Serial.println("[OK] Configuration file verified on filesystem");
    } else {
      Serial.println("[ERROR] Configuration file not found after write!");
    }
  } else {
    Serial.println("[ERROR] Failed to write sensor configuration");
  }
}

void updateSensorMapping(int logicalSlot, int physicalIdx, String name, String type) {
  if (logicalSlot < 0 || logicalSlot >= 3) return;

  // Update mapping for logical slots (T1=0, T2=1, T3=2)
  if (logicalSlot == 0) idxT1 = physicalIdx;
  else if (logicalSlot == 1) idxT2 = physicalIdx;
  else if (logicalSlot == 2) idxT3 = physicalIdx;

  // Update physical sensor properties if valid index
  if (physicalIdx >= 0 && physicalIdx < sensorCount) {
    if (name.length() > 0) {
      allSensors[physicalIdx].name = name;
    }
    if (type.length() > 0) {
      allSensors[physicalIdx].type = type;
    }
  }

  Serial.printf("[INFO] Mapping updated: T%d -> sensor %d (%s, %s)\n", 
                logicalSlot + 1, physicalIdx, name.c_str(), type.c_str());
}

// ============================================
// LEGACY NAME-ONLY PERSISTENCE
// ============================================

void loadSensorNames() {
  if (!LittleFS.exists("/sensors.json")) {
    Serial.println("No /sensors.json, using default names");
    return;
  }

  File f = LittleFS.open("/sensors.json", "r");
  if (!f) {
    Serial.println("Failed to open /sensors.json");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.println("Failed to parse /sensors.json");
    return;
  }

  JsonArray names = doc["names"].as<JsonArray>();
  if (!names.isNull()) {
    for (int i = 0; i < sensorCount && i < (int)names.size(); i++) {
      const char *n = names[i] | nullptr;
      if (n && strlen(n) > 0) {
        allSensors[i].name = String(n);
      }
    }
    Serial.println("Sensor names loaded from /sensors.json");
  }
}

void saveSensorNames() {
  StaticJsonDocument<512> doc;
  JsonArray names = doc.createNestedArray("names");
  for (int i = 0; i < sensorCount; i++) {
    names.add(allSensors[i].name);
  }

  File f = LittleFS.open("/sensors.json", "w");
  if (!f) {
    Serial.println("Failed to open /sensors.json for write");
    return;
  }

  serializeJson(doc, f);
  f.close();
  Serial.println("Sensor names saved to /sensors.json");
}

// ============================================
// SENSOR INITIALIZATION AND READING
// ============================================

void initSensors() {
  dsSensors.begin();
  Serial.println("=== Dallas scan ===");
  int count = dsSensors.getDeviceCount();
  Serial.print("Device count: ");
  Serial.println(count);

  sensorCount = 0;
  DeviceAddress a;
  for (uint8_t i = 0; i < count && sensorCount < MAX_SENSORS; i++) {
    if (dsSensors.getAddress(a, i)) {
      Sensor &s = allSensors[sensorCount];
      s.type = "temp";
      s.pin = -1;
      s.addr = "";
      for (uint8_t j = 0; j < 8; j++) {
        if (a[j] < 16) s.addr += "0";
        s.addr += String(a[j], HEX);
      }
      s.idx = sensorCount;
      s.value = -127.0f;
      s.name = "S" + String(sensorCount + 1);  // Changed from "T" to "S"

      Serial.print("Idx ");
      Serial.print(sensorCount);
      Serial.print(" addr: ");
      printAddress(a);
      Serial.println();
      sensorCount++;
    }
  }
  Serial.println("====================");

  dsSensors.setResolution(12);

  // Default mapping: first 3 sensors (if present)
  idxT1 = (sensorCount > 0) ? 0 : -1;
  idxT2 = (sensorCount > 1) ? 1 : -1;
  idxT3 = (sensorCount > 2) ? 2 : -1;

  // DON'T load config here anymore - moved to main setup()
}

void registerSensor(const String &type, int pin) {
  if (sensorCount >= MAX_SENSORS) {
    Serial.println("MAX_SENSORS reached, cannot register more");
    return;
  }

  Sensor &s = allSensors[sensorCount];
  s.type = type;
  s.pin = pin;
  s.addr = "";
  s.idx = sensorCount;
  s.value = 0.0f;
  s.name = "S" + String(sensorCount + 1);

  pinMode(pin, INPUT);
  Serial.print("Registered sensor ");
  Serial.print(type);
  Serial.print(" on GPIO");
  Serial.println(pin);
  sensorCount++;
}

float readByIndex(int idx) {
  if (idx < 0 || idx >= sensorCount) return -127.0f;

  Sensor &s = allSensors[idx];
  if (s.type == "temp") {
    DeviceAddress addr;
    if (dsSensors.getAddress(addr, idx)) {
      return dsSensors.getTempC(addr);
    }
    return -127.0f;
  } else if (s.type == "pressure") {
    int raw = analogRead(s.pin);
    float voltage = (raw * 3.3f) / 4095.0f;
    float pressure = (voltage - 0.2f) / 4.5f * 10.0f;
    return pressure;
  } else if (s.type == "level_cap") {
    int raw = analogRead(s.pin);
    return (raw / 4095.0f) * 100.0f;
  }

  return 0.0f;
}

void updateSensors() {
  if (sensorCount > 0) {
    // Batch DS18B20 read once
    dsSensors.requestTemperatures();
    for (int i = 0; i < sensorCount; i++) {
      allSensors[i].value = readByIndex(i);
    }

    tankTemp = readByIndex(idxT1);
    roomTemp = readByIndex(idxT2);
    colTemp = readByIndex(idxT3);
  }

  Serial.print("Sensors: ");
  for (int i = 0; i < sensorCount; i++) {
    Serial.print(allSensors[i].name);
    Serial.print("=");
    Serial.print(allSensors[i].value, 1);
    if (i < sensorCount - 1) Serial.print(" ");
  }
  Serial.println();
}
