// ============================================
// Winery Controller with GitHub OTA
// ESP32-S3 + 5x SSR + 3x DS18B20 + PID + Web UI
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

  initSensors();
  
  // NEW: Register additional sensor types (v1.4.0)
  registerSensor("pressure", 34);    // MPX5010 on GPIO34 (ADC1_CH6)
  registerSensor("level_cap", 36);   // Capacitive strip on GPIO36 (ADC1_CH0)
  
  initWebServer();
}

void loop() {
  updateSensors();  // CHANGED: unified sensor update (all types)
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
