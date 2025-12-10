// WebUI.cpp
// Winery Controller - Web UI (LittleFS + /state JSON + text POST commands)

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "State.h"
#include "Control.h"
#include "OtaGithub.h"
#include "WebUI.h"

// Global server instance
AsyncWebServer server(80);

bool canRunOta() {
  return !isRunning;
}

void initWebServer() {
  // Mount LittleFS
  if (!LittleFS.begin()) {
    Serial.println("ERROR: LittleFS mount failed!");
  } else {
    Serial.println("LittleFS mounted successfully");
  }

  // -------- GET /  -> serve webui.html from LittleFS --------
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/webui.html")) {
      request->send(LittleFS, "/webui.html", "text/html");
    } else {
      request->send(
        200, "text/html",
        "<h1>Winery Controller</h1><p>webui.html not found in LittleFS</p>"
      );
    }
  });

  // -------- GET /state -> JSON snapshot of current state --------
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;  // v7 shows deprecation warning; safe to use

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

    doc["t1"] = tankTemp;
    doc["t2"] = roomTemp;
    doc["t3"] = colTemp;

    doc["fw"]   = FIRMWARE_VERSION;
    doc["ip"]   = WiFi.localIP().toString();
    doc["ssid"] = WIFI_SSID;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // -------- POST / -> text-based commands --------
  server.on(
    "/", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // response sent in body handler
    },
    nullptr,
    [](AsyncWebServerRequest *request,
       uint8_t *data, size_t len, size_t index, size_t total) {

      String cmd;
      cmd.reserve(len);
      for (size_t i = 0; i < len; i++) cmd += (char)data[i];
      cmd.trim();
      Serial.println("POST: " + cmd);

      // ----- PROCESS:x -----
      if (cmd.startsWith("PROCESS:")) {
        int newProcess = cmd.substring(8).toInt();
        if (newProcess != (int)processMode) {
          processMode = (ProcessMode)newProcess;
          resetProcessDefaults();   // clears SSRs, setpoint, isRunning, outputs
          Serial.println("Process mode = " + String((int)processMode));
        }
      }

      // ----- CONTROL:x -----
      else if (cmd.startsWith("CONTROL:")) {
        int newMode = cmd.substring(8).toInt();
        controlMode = (ControlMode)newMode;
        pidISum     = 0;
        pidLastErr  = 0;
        Serial.println("Control mode = " + String((int)controlMode));
      }

      // ----- SETPOINT:x -----
      else if (cmd.startsWith("SETPOINT:")) {
        if (processMode == PROCESS_OFF) {
          Serial.println("Setpoint change ignored: process OFF");
        } else {
          setpointValue = cmd.substring(9).toFloat();
          Serial.println("Setpoint = " + String(setpointValue));
        }
      }

      // ----- SSR:S1..S5 -----
      else if (cmd.startsWith("SSR:")) {
        String which = cmd.substring(4);
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

      // ----- START -----
      else if (cmd == "START") {
        int enabledSSR = 0;
        if (processMode == PROCESS_DISTILLER) {
          enabledSSR = (ssr1Enabled ? 1 : 0) +
                       (ssr2Enabled ? 1 : 0) +
                       (ssr3Enabled ? 1 : 0);
        } else if (processMode == PROCESS_RECTIFIER) {
          enabledSSR = (ssr4Enabled ? 1 : 0) +
                       (ssr5Enabled ? 1 : 0);
        }

        if (processMode != PROCESS_OFF &&
            setpointValue > 0 &&
            enabledSSR > 0) {
          isRunning  = true;
          pidISum    = 0;
          pidLastErr = 0;
          if (controlMode == CONTROL_TEMP) {
            distillerPower = 100.0f;
            rectifierPower = 100.0f;
          } else {
            distillerPower = setpointValue;
            rectifierPower = setpointValue;
          }
          Serial.println("Process START");
        } else {
          Serial.println("START ignored: mode=" +
                         String((int)processMode) +
                         " sp=" + String(setpointValue) +
                         " enabledSSR=" + String(enabledSSR));
        }
      }

      // ----- STOP -----
      else if (cmd == "STOP") {
        isRunning      = false;
        distillerPower = 0.0f;
        rectifierPower = 0.0f;
        Serial.println("Process STOP");
      }

      // ----- OTA -----
      else if (cmd == "OTA") {
        if (!isRunning) {
          Serial.println("OTA: checking GitHub latest...");
          handleGitHubOTA();
        } else {
          Serial.println("OTA blocked: running");
        }
      }

      // ----- RESTART -----
      else if (cmd == "RESTART") {
        if (!isRunning) {
          Serial.println("Restarting...");
          delay(500);
          ESP.restart();
        } else {
          Serial.println("Restart blocked: running");
        }
      }

      request->send(200, "text/plain", "OK");
    }
  );

  server.begin();
  Serial.println("Web server started on port 80");
}
