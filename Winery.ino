// ============================================
// Winery Controller with GitHub OTA
// ESP32-S3 + 5x SSR + DS18B20 + PID + Web UI
// ============================================

#include <WiFi.h>
#include "Config.h"
#include "State.h"
#include "Sensors.h"
#include "Control.h"
#include "WebUI.h"
#include "OtaGithub.h"
#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("BOOTING Winery...");
  
  // Mount LittleFS FIRST
  if (!LittleFS.begin(true)) {
    Serial.println("[ERROR] LittleFS mount failed");
  } else {
    Serial.println("[OK] LittleFS mounted");
  }
  
  initState();
  initPins();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  // Discover Dallas sensors but DON'T load config yet
  initSensors();

  // Register analog sensors
  registerSensor("pressure", 5);
  registerSensor("level_cap", 6);

  // NOW load config after ALL sensors are registered
  loadSensorConfig();
  
  Serial.printf("[DEBUG] After final config load - T1:%d T2:%d T3:%d\n", idxT1, idxT2, idxT3);
  for (int i = 0; i < sensorCount; i++) {
    Serial.printf("[DEBUG] Final sensor %d: name='%s' type='%s'\n", 
                  i, allSensors[i].name.c_str(), allSensors[i].type.c_str());
  }

  initWebServer();
}

void loop() {
  updateSensors();
  updateControlLoop();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    Serial.printf("FW %s | T1:%.1f T2:%.1f T3:%.1f | Mode:%d/%d Run:%d | D:%.1f R:%.1f\n",
                  FIRMWARE_VERSION, tankTemp, roomTemp, colTemp,
                  processMode, controlMode, isRunning,
                  distillerPower, rectifierPower);
    lastPrint = millis();
  }

  delay(50);
}
