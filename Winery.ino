// ============================================
// Winery Distillation Controller (ESPHome -> Arduino)
// ESP32-S3 + 5x SSR + 3x DS18B20 + PID + Beautiful UI
// ============================================
// Working one

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>

// === YOUR PINS (EXACT wiring) ===
#define ONE_WIRE_BUS   17   // Blue 4pin slot - DS18B20 data
#define DISTILLER_PWM1 5    // 8pin #1 Blue  - SSR1
#define DISTILLER_PWM2 6    // 8pin #2 White - SSR2
#define DISTILLER_PWM3 7    // 8pin #3 Green - SSR3
#define RECTIFIER_PWM1 14   // 8pin #8 Red   - SSR4
#define RECTIFIER_PWM2 15   // 8pin #4 Yellow- SSR5

// === WIFI ===
const char* ssid     = "SmartHome";
const char* password = "0544759839";

// === STATES ===
int   processMode   = 0;        // 0=OFF, 1=DISTILLER, 2=RECTIFIER
int   controlMode   = 0;        // 0=Power %, 1=Temp C
float setpointValue = 0.0;      // 1-100 % or C (starts at 0)
float distillerPower = 0.0;
float rectifierPower = 0.0;
bool  isRunning     = false;    // Start/Stop state

// === SSR ENABLE/DISABLE (per-relay control) ===
bool ssr1Enabled = false;  // S1
bool ssr2Enabled = false;  // S2
bool ssr3Enabled = false;  // S3
bool ssr4Enabled = false;  // S4
bool ssr5Enabled = false;  // S5

// === SENSORS (YOUR exact ROM codes) ===
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress tankAddr = {0xF9,0x00,0x00,0x00,0x59,0x1C,0xC2,0x28};
DeviceAddress roomAddr = {0x59,0x00,0x00,0x00,0x55,0xEF,0x57,0x28};
DeviceAddress colAddr  = {0xFA,0x00,0x00,0x00,0xBC,0xD3,0x53,0x28};
float tankTemp = 0, roomTemp = 0, colTemp = 0;

// === PID (ESPHome values) ===
float Kp = 0.3, Ki = 0.05, Kd = 2.0;
float pidISum = 0, pidLastErr = 0, pidOutput = 0;

AsyncWebServer server(80);

// ---------- PWM ----------
void setupPWM() {
  pinMode(DISTILLER_PWM1, OUTPUT);
  pinMode(DISTILLER_PWM2, OUTPUT);
  pinMode(DISTILLER_PWM3, OUTPUT);
  pinMode(RECTIFIER_PWM1, OUTPUT);
  pinMode(RECTIFIER_PWM2, OUTPUT);
}

void analogWriteCompat(uint8_t pin, float duty01) {
  uint8_t v = (uint8_t)constrain(duty01 * 255.0f, 0, 255);
  analogWrite(pin, v);
}

void updateOutputs() {
  if (!isRunning) {
    // Safety: all SSRs off when not running
    analogWriteCompat(DISTILLER_PWM1, 0);
    analogWriteCompat(DISTILLER_PWM2, 0);
    analogWriteCompat(DISTILLER_PWM3, 0);
    analogWriteCompat(RECTIFIER_PWM1, 0);
    analogWriteCompat(RECTIFIER_PWM2, 0);
    return;
  }

  float perSSR;
  
  if (processMode == 1) {  // DISTILLER SSR1-3
    int enabledCount = (ssr1Enabled ? 1 : 0) + (ssr2Enabled ? 1 : 0) + (ssr3Enabled ? 1 : 0);
    
    if (enabledCount > 0) {
      perSSR = distillerPower / enabledCount;
    } else {
      perSSR = 0;
    }
    
    analogWriteCompat(DISTILLER_PWM1, (ssr1Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(DISTILLER_PWM2, (ssr2Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(DISTILLER_PWM3, (ssr3Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(RECTIFIER_PWM1, 0);
    analogWriteCompat(RECTIFIER_PWM2, 0);
  }
  else if (processMode == 2) {  // RECTIFIER SSR4-5
    int enabledCount = (ssr4Enabled ? 1 : 0) + (ssr5Enabled ? 1 : 0);
    
    if (enabledCount > 0) {
      perSSR = rectifierPower / enabledCount;
    } else {
      perSSR = 0;
    }
    
    analogWriteCompat(RECTIFIER_PWM1, (ssr4Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(RECTIFIER_PWM2, (ssr5Enabled ? perSSR : 0.0f) / 100.0f);
    analogWriteCompat(DISTILLER_PWM1, 0);
    analogWriteCompat(DISTILLER_PWM2, 0);
    analogWriteCompat(DISTILLER_PWM3, 0);
  }
  else {  // OFF
    analogWriteCompat(DISTILLER_PWM1, 0);
    analogWriteCompat(DISTILLER_PWM2, 0);
    analogWriteCompat(DISTILLER_PWM3, 0);
    analogWriteCompat(RECTIFIER_PWM1, 0);
    analogWriteCompat(RECTIFIER_PWM2, 0);
  }
}

// ---------- PID ----------
void pidUpdate(float inputTemp) {
  float error = setpointValue - inputTemp;
  pidISum += error;
  if (pidISum > 50)  pidISum = 50;
  if (pidISum < -50) pidISum = -50;
  float dErr = error - pidLastErr;
  pidOutput = Kp * error + Ki * pidISum + Kd * dErr;
  if (pidOutput > 100) pidOutput = 100;
  if (pidOutput < 0)   pidOutput = 0;
  pidLastErr = error;
}

// ---------- RESET SSRs and Setpoint on process change ----------
void resetProcessDefaults() {
  ssr1Enabled = false;
  ssr2Enabled = false;
  ssr3Enabled = false;
  ssr4Enabled = false;
  ssr5Enabled = false;
  setpointValue = 0.0;
  isRunning = false;
  pidISum = 0;
  pidLastErr = 0;
  Serial.println("Process defaults reset: all SSRs OFF, setpoint 0, stopped");
}

// ---------- SETUP ArduinoOTA ----------
void setupOTA() {
  ArduinoOTA.setHostname("Winery");
  
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("OTA Start updating " + type);
    isRunning = false;  // Stop any process before OTA
    updateOutputs();    // Cut all SSRs
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready on Winery.local");
}

// ---------- HTTP ROOT (GET) ----------
void setupHttpRoot() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String t1 = String(tankTemp, 1);
    String t2 = String(roomTemp, 1);
    String t3 = String(colTemp, 1);
    String mode = processMode==0 ? "OFF" : (processMode==1 ? "DISTILLER" : "RECTIFIER");
    String ctrl = controlMode==0 ? "POWER %" : "TEMP C";
    String unit = controlMode==0 ? "%" : "C";
    String runState = isRunning ? "RUNNING" : "STOPPED";

    // Start button only enabled if process selected AND setpoint > 0
    bool startButtonEnabled = (processMode != 0) && (setpointValue > 0);

    String html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Winery Display Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root {
      --bg-main: #111111;
      --bg-card: #202020;
      --bg-input: #181818;
      --accent: #ff7a1a;
      --accent-danger: #e02424;
      --text-main: #f5f5f5;
      --text-muted: #b3b3b3;
      --text-green: #65ff7a;
      --border-subtle: #333333;
    }
    * { box-sizing:border-box; margin:0; padding:0; }
    body {
      font-family: system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
      background:#000000; color:var(--text-main);
      display:flex; justify-content:center;
      padding:24px 12px;
    }
    .app-shell { width:100%; max-width:520px; }
    .app-header { text-align:center; margin-bottom:12px; }
    .app-title {
      font-size:1.6rem; font-weight:700;
      letter-spacing:0.12em; text-transform:uppercase;
      color:var(--accent);
    }
    .app-subtitle {
      font-size:0.9rem; color:var(--text-muted); margin-top:4px;
    }
    .divider {
      border:none; border-top:2px solid var(--accent);
      margin:10px auto 18px; max-width:460px;
    }
    .card {
      background:var(--bg-card); border-radius:12px;
      padding:16px 16px 18px; margin-bottom:14px;
      box-shadow:0 2px 8px rgba(0,0,0,0.5);
    }
    .card-title {
      font-size:0.85rem; font-weight:600;
      text-transform:uppercase; letter-spacing:0.14em;
      color:var(--accent); margin-bottom:12px;
    }
    .metric-primary { font-size:1.4rem; font-weight:700; margin-bottom:10px; }
    .status-badge {
      display:inline-block; padding:6px 12px; border-radius:20px;
      font-size:0.75rem; font-weight:600; text-transform:uppercase;
      margin-bottom:10px;
    }
    .status-running { background:var(--text-green); color:#000000; }
    .status-stopped { background:#555555; color:#f5f5f5; }
    .field { display:flex; flex-direction:column; gap:4px; margin-bottom:10px; }
    .field-label {
      font-size:0.8rem; text-transform:uppercase;
      letter-spacing:0.06em; color:var(--text-muted);
    }
    .field select,
    .field input[type="number"] {
      width:100%; padding:8px 10px;
      border-radius:6px; border:1px solid var(--border-subtle);
      background-color:var(--bg-input); color:var(--text-main);
      font-size:0.9rem;
    }
    .field select {
      appearance:none; -webkit-appearance:none;
      background-image:
        linear-gradient(45deg,transparent 50%,var(--accent) 50%),
        linear-gradient(135deg,var(--accent) 50%,transparent 50%);
      background-position:calc(100% - 16px) 50%, calc(100% - 10px) 50%;
      background-size:6px 6px, 6px 6px;
      background-repeat:no-repeat;
      padding-right:28px;
    }
    .field select:focus,
    .field input[type="number"]:focus {
      outline:none; border-color:var(--accent);
      box-shadow:0 0 0 1px rgba(255,122,26,0.4);
    }
    .slider-row {
      display:flex; align-items:center; gap:10px; margin-top:4px;
    }
    .slider-row input[type="range"] { flex:1; accent-color:var(--accent); }
    .slider-row .numeric-box {
      width:64px; text-align:center; padding:7px 6px;
      border-radius:6px; border:1px solid var(--border-subtle);
      background:var(--bg-input); font-size:0.9rem;
    }
    .slider-row .unit-box {
      padding:7px 10px; border-radius:6px;
      border:1px solid var(--border-subtle);
      background:var(--bg-input); font-size:0.9rem;
    }
    .row {
      display:flex; justify-content:space-between; align-items:center;
      font-size:0.9rem; padding:4px 0; border-bottom:1px solid #262626;
    }
    .row:last-child { border-bottom:none; }
    .row-label { color:var(--text-muted); }
    .row-value { color:var(--text-green); font-weight:500; }
    .btn {
      width:100%; padding:10px 12px; border-radius:6px; border:none;
      font-size:0.9rem; font-weight:600; text-transform:uppercase;
      letter-spacing:0.06em; cursor:pointer; margin-top:8px;
    }
    .btn-primary { background:var(--accent); color:#111111; }
    .btn-danger  { background:var(--accent-danger); color:#ffffff; }
    .btn-start   { background:var(--text-green); color:#000000; }
    .btn-stop    { background:var(--accent-danger); color:#ffffff; }
    .btn-ssr-on  { background:var(--text-green); color:#000000; border:1px solid var(--text-green); }
    .btn-ssr-off { background:#555555; color:#f5f5f5; border:1px solid #666666; }
    .btn:disabled {
      opacity:0.5; cursor:not-allowed;
    }
    .btn:active:not(:disabled) { transform:translateY(1px); }
    .diag-row {
      display:flex; justify-content:space-between;
      font-size:0.8rem; padding:3px 0; color:var(--text-muted);
    }
    .diag-row span:last-child { color:var(--text-main); }
  </style>
</head>
<body>
  <div class="app-shell">
    <header class="app-header">
      <div class="app-title">VINODELNYA</div>
      <div class="app-subtitle">Winery Display Control</div>
    </header>

    <hr class="divider">

    <section class="card">
      <div class="card-title">Sensor &amp; Control</div>

      <div class="status-badge )html" + String(isRunning ? "status-running" : "status-stopped") + R"html(">)html" + runState + R"html(</div>

      <div class="metric-primary">Active Power )html" + String((processMode==0 || !isRunning)?0:setpointValue,1) + R"html(%</div>

      <label class="field">
        <span class="field-label">Active Process</span>
        <select id="process" onchange="setProcess(this.value)">
          <option value="0")html" + String(processMode==0?" selected":"") + R"html(>Off</option>
          <option value="1")html" + String(processMode==1?" selected":"") + R"html(>Distiller</option>
          <option value="2")html" + String(processMode==2?" selected":"") + R"html(>Rectifier</option>
        </select>
      </label>

      <label class="field">
        <span class="field-label">Control Mode</span>
        <select id="mode" onchange="setControl(this.value)">
          <option value="0")html" + String(controlMode==0?" selected":"") + R"html(>Power-Driven</option>
          <option value="1")html" + String(controlMode==1?" selected":"") + R"html(>Temperature-Driven</option>
        </select>
      </label>

      <div class="field">
        <span class="field-label">Setpoint</span>
        <div class="slider-row">
          <input type="range" id="setpoint" min="0" max="100" step="0.5"
                 value=")html" + String(setpointValue,1) + R"html("
                 oninput="setSetpoint(this.value)">
          <div class="numeric-box" id="setpointValueBox">)html" + String(setpointValue,1) + R"html(</div>
          <div class="unit-box">)html" + unit + R"html(</div>
        </div>
      </div>

      <div style="font-size:0.8rem;color:var(--text-muted);margin-top:10px;margin-bottom:4px;">
        Temperature Readings
      </div>

      <div class="row">
        <span class="row-label">T1 - Tank:</span>
        <span class="row-value">)html" + t1 + R"html( &deg;C</span>
      </div>
      <div class="row">
        <span class="row-label">T2 - Outdoor:</span>
        <span class="row-value">)html" + t2 + R"html( &deg;C</span>
      </div>
      <div class="row">
        <span class="row-label">T3 - Column:</span>
        <span class="row-value">)html" + t3 + R"html( &deg;C</span>
      </div>
    </section>
)html";

    // ---------- SSR SWITCHES CARD ----------
    html += R"html(
    <section class="card">
      <div class="card-title">SSR Switches</div>
)html";

    if (processMode == 0) {
      html += R"html(
      <div style="font-size:0.85rem;color:var(--text-muted);">
        No process selected
      </div>
)html";
    } else if (processMode == 1) {
      html += R"html(
      <div style="font-size:0.85rem;color:var(--text-muted);margin-bottom:10px;">
        DISTILLER SSRS (1–3)
      </div>
      <div style="display:flex;gap:10px;">
        <button class="btn )html" + String(ssr1Enabled ? "btn-ssr-on" : "btn-ssr-off") + R"html(" 
                onclick="send('SSR:S1')" )html" + String(setpointValue==0 ? "disabled" : "") + R"html(>S1</button>
        <button class="btn )html" + String(ssr2Enabled ? "btn-ssr-on" : "btn-ssr-off") + R"html(" 
                onclick="send('SSR:S2')" )html" + String(setpointValue==0 ? "disabled" : "") + R"html(>S2</button>
        <button class="btn )html" + String(ssr3Enabled ? "btn-ssr-on" : "btn-ssr-off") + R"html(" 
                onclick="send('SSR:S3')" )html" + String(setpointValue==0 ? "disabled" : "") + R"html(>S3</button>
      </div>
)html";
    } else { // processMode == 2
      html += R"html(
      <div style="font-size:0.85rem;color:var(--text-muted);margin-bottom:10px;">
        RECTIFIER SSRS (4–5)
      </div>
      <div style="display:flex;gap:10px;">
        <button class="btn )html" + String(ssr4Enabled ? "btn-ssr-on" : "btn-ssr-off") + R"html(" 
                onclick="send('SSR:S4')" )html" + String(setpointValue==0 ? "disabled" : "") + R"html(>S4</button>
        <button class="btn )html" + String(ssr5Enabled ? "btn-ssr-on" : "btn-ssr-off") + R"html(" 
                onclick="send('SSR:S5')" )html" + String(setpointValue==0 ? "disabled" : "") + R"html(>S5</button>
      </div>
)html";
    }

    html += R"html(
    </section>

    <section class="card">
      <div class="card-title">Control</div>
)html";

    // Start/Stop buttons
    if (isRunning) {
      html += R"html(
      <button class="btn btn-stop" onclick="send('STOP')">Stop Process</button>
)html";
    } else {
      html += R"html(
      <button class="btn btn-start" onclick="send('START')" )html" + String(startButtonEnabled ? "" : "disabled") + R"html(>Start Process</button>
)html";
    }

    html += R"html(
    </section>

    <section class="card">
      <div class="card-title">Configuration</div>

      <label class="field">
        <span class="field-label">Log Level</span>
        <select id="logLevel">
          <option>INFO</option>
          <option>DEBUG</option>
          <option>WARN</option>
          <option>ERROR</option>
        </select>
      </label>

      <button class="btn btn-primary" onclick="send('OTA')" )html" + String(isRunning ? "disabled" : "") + R"html(>Remote OTA Update</button>
      <button class="btn btn-danger" onclick="send('RESTART')" )html" + String(isRunning ? "disabled" : "") + R"html(>Restart Device</button>
    </section>

    <section class="card">
      <div class="card-title">Diagnostic</div>
      <div class="diag-row">
        <span>WiFi IP:</span>
        <span>)html" + WiFi.localIP().toString() + R"html(</span>
      </div>
      <div class="diag-row">
        <span>WiFi SSID:</span>
        <span>)html" + String(ssid) + R"html(</span>
      </div>
    </section>
  </div>

  <script>
    async function send(cmd) {
      await fetch('/', {method:'POST', headers:{'Content-Type':'text/plain'}, body:cmd});
      setTimeout(() => location.reload(), 150);
    }
    function setProcess(v)  { send('PROCESS:'+v); }
    function setControl(v)  { send('CONTROL:'+v); }
    function setSetpoint(v) {
      document.getElementById('setpointValueBox').textContent = v;
      send('SETPOINT:'+v);
    }
  </script>
</body>
</html>
)html";

    request->send(200, "text/html", html);
  });
}

// ---------- HTTP ROOT (POST) ----------
void setupHttpPost() {
  server.on(
    "/", HTTP_POST,
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      String cmd;
      cmd.reserve(len);
      for (size_t i = 0; i < len; i++) cmd += (char)data[i];
      cmd.trim();
      Serial.println("POST body: " + cmd);

      if (cmd.startsWith("PROCESS:")) {
        int newProcess = cmd.substring(8).toInt();
        if (newProcess != processMode) {
          processMode = newProcess;
          resetProcessDefaults();
          Serial.println("Process Mode: " + String(processMode));
        }
      } else if (cmd.startsWith("CONTROL:")) {
        controlMode = cmd.substring(8).toInt();
        Serial.println("Control Mode: " + String(controlMode));
      } else if (cmd.startsWith("SETPOINT:")) {
        setpointValue = cmd.substring(9).toFloat();
        Serial.println("Setpoint: " + String(setpointValue));
      } else if (cmd.startsWith("SSR:")) {
        String which = cmd.substring(4);
        Serial.println("SSR toggle: " + which);
        
        if (which == "S1") ssr1Enabled = !ssr1Enabled;
        else if (which == "S2") ssr2Enabled = !ssr2Enabled;
        else if (which == "S3") ssr3Enabled = !ssr3Enabled;
        else if (which == "S4") ssr4Enabled = !ssr4Enabled;
        else if (which == "S5") ssr5Enabled = !ssr5Enabled;
        
        Serial.printf("SSR1:%d SSR2:%d SSR3:%d SSR4:%d SSR5:%d\n",
                      ssr1Enabled, ssr2Enabled, ssr3Enabled, ssr4Enabled, ssr5Enabled);
      } else if (cmd == "START") {
        if (processMode != 0 && setpointValue > 0) {
          isRunning = true;
          pidISum = 0;
          pidLastErr = 0;
          
          if (controlMode == 1) {  // Temperature-driven
            distillerPower = 100.0;
            rectifierPower = 100.0;
          } else {  // Power-driven
            distillerPower = setpointValue;
            rectifierPower = setpointValue;
          }
          
          Serial.println("Process STARTED");
          Serial.printf("Mode:%d ControlMode:%d Setpoint:%.1f\n", processMode, controlMode, setpointValue);
        }
      } else if (cmd == "STOP") {
        isRunning = false;
        distillerPower = 0.0;
        rectifierPower = 0.0;
        Serial.println("Process STOPPED");
      } else if (cmd == "OTA") {
        if (!isRunning) {
          Serial.println("OTA mode initiated. Upload new sketch from Arduino IDE.");
        } else {
          Serial.println("OTA blocked: process is running");
        }
      } else if (cmd == "RESTART") {
        if (!isRunning) {
          Serial.println("Restart requested");
          ESP.restart();
        } else {
          Serial.println("Restart blocked: process is running");
        }
      }

      request->send(200, "text/plain", "OK");
    }
  );
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(921600);
  setupPWM();

  sensors.begin();
  sensors.setResolution(12);
  sensors.setWaitForConversion(false);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Winery IP: " + WiFi.localIP().toString());

  setupHttpRoot();
  setupHttpPost();
  setupOTA();

  server.begin();
  Serial.println("Web UI ready.");
}

void loop() {
  // Handle OTA requests
  ArduinoOTA.handle();

  // SENSORS (750ms)
  static unsigned long lastTemp = 0;
  if (millis() - lastTemp > 750) {
    sensors.requestTemperatures();
    tankTemp = sensors.getTempC(tankAddr);
    roomTemp = sensors.getTempC(roomAddr);
    colTemp  = sensors.getTempC(colAddr);
    lastTemp = millis();
  }

  // CONTROL (100ms)
  static unsigned long lastControl = 0;
  if (millis() - lastControl > 100) {
    if (isRunning) {
      if (processMode == 1) {  // DISTILLER
        if (controlMode == 0) {  // Power-driven
          distillerPower = setpointValue;
        } else {  // Temperature-driven
          pidUpdate(tankTemp);
          distillerPower = pidOutput;
        }
        rectifierPower = 0;
      } else if (processMode == 2) {  // RECTIFIER
        if (controlMode == 0) {  // Power-driven
          rectifierPower = setpointValue;
        } else {  // Temperature-driven
          pidUpdate(colTemp);
          rectifierPower = pidOutput;
        }
        distillerPower = 0;
      }
    } else {
      distillerPower = 0;
      rectifierPower = 0;
    }
    updateOutputs();
    lastControl = millis();
  }

  // DEBUG (2s)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    Serial.printf("T1:%.1f T2:%.1f T3:%.1f | Mode:%d/%d | Running:%d | D:%.1f%% R:%.1f%% | S1:%d S2:%d S3:%d S4:%d S5:%d\n",
                  tankTemp, roomTemp, colTemp,
                  processMode, controlMode, isRunning,
                  distillerPower, rectifierPower,
                  ssr1Enabled, ssr2Enabled, ssr3Enabled, ssr4Enabled, ssr5Enabled);
    lastPrint = millis();
  }

  delay(50);
}
