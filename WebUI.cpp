#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "Config.h"
#include "State.h"
#include "Sensors.h"
#include "WebUI.h"

// Web server instance
AsyncWebServer server(80);

void initWebServer() {
  // LittleFS already mounted in setup(), just verify
  if (!LittleFS.begin()) {
    Serial.println("[WARN] LittleFS not mounted in initWebServer");
  }

  // Root: GET = UI, POST = commands
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/webui.html", "text/html");
  });

  // Serve favicon
  server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico", "image/x-icon");
  
  // Serve logo
  server.serveStatic("/Logo.png", LittleFS, "/Logo.png", "image/png");

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
          if (which == "S1") ssr1Enabled = !ssr1Enabled;
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

  // /state: core + sensors + generic map[]
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);

    // Core
    doc["fw"] = FIRMWARE_VERSION;
    doc["processMode"] = (int)processMode;
    doc["controlMode"] = (int)controlMode;
    doc["setpoint"] = setpointValue;
    doc["isRunning"] = isRunning;
    doc["distPower"] = distillerPower;
    doc["rectPower"] = rectifierPower;
    doc["ssr1"] = ssr1Enabled;
    doc["ssr2"] = ssr2Enabled;
    doc["ssr3"] = ssr3Enabled;
    doc["ssr4"] = ssr4Enabled;
    doc["ssr5"] = ssr5Enabled;

    // Legacy T1/T2/T3 temps
    doc["t1"] = tankTemp;
    doc["t2"] = roomTemp;
    doc["t3"] = colTemp;

    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["board"] = "JC3248W535";

    JsonObject pinOpts = doc.createNestedObject("pinOptions");
    JsonArray tempPins = pinOpts.createNestedArray("temp");
    tempPins.add(17);  // OneWire bus
    JsonArray analogPins = pinOpts.createNestedArray("analog");
    analogPins.add(5);
    analogPins.add(6);
    analogPins.add(7);

    // Generic sensor map
    JsonArray mapArray = doc.createNestedArray("map");
    mapArray.add(idxT1);
    mapArray.add(idxT2);
    mapArray.add(idxT3);

    // Generic sensor list
    JsonArray sensorsArray = doc.createNestedArray("sensors");
    for (int i = 0; i < sensorCount; i++) {
      JsonObject s = sensorsArray.createNestedObject();
      s["idx"] = allSensors[i].idx;
      s["name"] = allSensors[i].name;
      s["type"] = allSensors[i].type;
      s["value"] = allSensors[i].value;
      s["pin"] = allSensors[i].pin;
      s["addr"] = allSensors[i].addr;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  // /config/sensors POST: update mapping and names/types
  server.on("/config/sensors", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body;
      body.reserve(len + 1);
      for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
      }

      Serial.printf("[POST /config/sensors] Receiving %d bytes\n", len);

      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        Serial.printf("[ERROR] JSON parse failed: %s\n", err.c_str());
        request->send(400, "text/plain", "Invalid JSON");
        return;
      }

      JsonArray mapArr = doc["map"];
      JsonArray namesArr = doc["names"];
      JsonArray typesArr = doc["types"];

      if (!mapArr || !namesArr || !typesArr) {
        Serial.println("[ERROR] Missing required arrays in payload");
        request->send(400, "text/plain", "Missing map/names/types");
        return;
      }

      Serial.println("[INFO] Updating sensor mapping:");
      for (int i = 0; i < 3 && i < (int)mapArr.size(); i++) {
        int physicalIdx = mapArr[i] | -1;
        String name = (i < (int)namesArr.size()) ? namesArr[i].as<String>() : "";
        String type = (i < (int)typesArr.size()) ? typesArr[i].as<String>() : "temp";

        Serial.printf("  T%d -> Physical sensor %d, name='%s', type='%s'\n",
                      i + 1, physicalIdx, name.c_str(), type.c_str());

        updateSensorMapping(i, physicalIdx, name, type);
      }

      saveSensorConfig();
      Serial.println("[INFO] Sensor configuration saved");

      request->send(200, "text/plain", "OK");
    });

  server.begin();
  Serial.println("Web server started on port 80");
}
