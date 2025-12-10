#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "State.h"
#include "Sensors.h"
#include "WebUI.h"

// Web server instance
AsyncWebServer server(80);

void initWebServer() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  } else {
    Serial.println("LittleFS mounted successfully");
  }

  // Root: GET = UI, POST = commands
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/webui.html", "text/html");
  });

  server.on("/", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body;
      body.reserve(len + 1);
      for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
      }

      body.trim();
      Serial.print("HTTP CMD: ");
      Serial.println(body);

      if (body == "START") {
        if (!isRunning) {
          isRunning = true;
          Serial.println("Process START");
        }
      }
      else if (body == "STOP") {
        if (isRunning) {
          isRunning = false;
          Serial.println("Process STOP");
          resetProcessDefaults();
        }
      }
      else if (body == "OTA") {
        Serial.println("OTA requested");
        // trigger OTA here if needed
      }
      else if (body == "RESTART") {
        Serial.println("Restart requested");
        ESP.restart();
      }
      else if (body.startsWith("PROCESS:")) {
        int p = body.substring(8).toInt();
        if (p == 0) processMode = PROCESS_OFF;
        else if (p == 1) processMode = PROCESS_DISTILLER;
        else if (p == 2) processMode = PROCESS_RECTIFIER;
        Serial.print("Process mode set to ");
        Serial.println((int)processMode);
      }
      else if (body.startsWith("CONTROL:")) {
        int c = body.substring(8).toInt();
        if (c == 0) controlMode = CONTROL_POWER;
        else if (c == 1) controlMode = CONTROL_TEMP;
        Serial.print("Control mode set to ");
        Serial.println((int)controlMode);
      }
      else if (body.startsWith("SETPOINT:")) {
        float sp = body.substring(9).toFloat();
        setpointValue = sp;
        Serial.print("Setpoint set to ");
        Serial.println(sp);
      }
      else if (body.startsWith("SSR:")) {
        if (isRunning) {
          Serial.println("SSR change ignored: process running");
        } else {
          String which = body.substring(4);
          if      (which == "S1") ssr1Enabled = !ssr1Enabled;
          else if (which == "S2") ssr2Enabled = !ssr2Enabled;
          else if (which == "S3") ssr3Enabled = !ssr3Enabled;
          else if (which == "S4") ssr4Enabled = !ssr4Enabled;
          else if (which == "S5") ssr5Enabled = !ssr5Enabled;

          bool state =
            (which == "S1") ? ssr1Enabled :
            (which == "S2") ? ssr2Enabled :
            (which == "S3") ? ssr3Enabled :
            (which == "S4") ? ssr4Enabled :
                              ssr5Enabled;

          Serial.println("SSR " + which + " -> " + String(state));
        }
      }

      request->send(200, "text/plain", "OK");
    });

    // State endpoint
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<1024> doc;

    doc["fw"]          = FIRMWARE_VERSION;
    doc["processMode"] = (int)processMode;
    doc["controlMode"] = (int)controlMode;
    doc["setpoint"]    = setpointValue;
    doc["isRunning"]   = isRunning;
    doc["distPower"]   = distillerPower;
    doc["rectPower"]   = rectifierPower;

    doc["ssr1"] = ssr1Enabled;
    doc["ssr2"] = ssr2Enabled;
    doc["ssr3"] = ssr3Enabled;
    doc["ssr4"] = ssr4Enabled;
    doc["ssr5"] = ssr5Enabled;

    // Legacy 3-role temps (still used by main panel + control)
    doc["t1"] = tankTemp;
    doc["t2"] = roomTemp;
    doc["t3"] = colTemp;

    doc["ip"]   = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();

    // Dynamic DS18B20 list (idx + addr)
    JsonArray sensorsArr = doc.createNestedArray("sensors");
    for (int i = 0; i < sensorCount; i++) {
      char buf[17];
      for (uint8_t b = 0; b < 8; b++) {
        sprintf(buf + b * 2, "%02X", foundSensors[i][b]);
      }
      buf[16] = '\0';

      JsonObject o = sensorsArr.createNestedObject();
      o["idx"]  = i;
      o["addr"] = buf;
    }

    // Per-sensor temps aligned with sensors[]
    JsonArray tempsArr = doc.createNestedArray("temps");
    // Use the same mapping function as updateTemperatures()
    extern float readByIndex(int idx);  // declare helper from Sensors.cpp
    for (int i = 0; i < sensorCount; i++) {
      tempsArr.add(readByIndex(i));
    }

    // Current mapping indices
    doc["map_t1"] = idxT1;
    doc["map_t2"] = idxT2;
    doc["map_t3"] = idxT3;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // Mapping config endpoint
  server.on("/config/sensors", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body;
      body.reserve(len + 1);
      for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
      }

      StaticJsonDocument<128> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        request->send(400, "text/plain", "Bad JSON");
        return;
      }

      if (doc.containsKey("t1")) idxT1 = doc["t1"].as<int>();
      if (doc.containsKey("t2")) idxT2 = doc["t2"].as<int>();
      if (doc.containsKey("t3")) idxT3 = doc["t3"].as<int>();

      Serial.printf("Sensor map updated: T1=%d T2=%d T3=%d\n", idxT1, idxT2, idxT3);

      request->send(200, "application/json", "{\"ok\":true}");
    });

  server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico", "image/x-icon");

  server.begin();
  Serial.println("Web server started on port 80");
}
