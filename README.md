### Evaluation

`valveEvaluateAll()` runs every 500 ms regardless of `isRunning` or `processMode`. Offline sensors never trigger conditions. Close takes priority over open if both conditions fire simultaneously.

### Sensor Catalog (valve rule sensor IDs)

| ID | Name | Kind | Enabled |
|---|---|---|---|
| 1 | Room Temp | TEMP | ✓ |
| 2 | Kettle Temp | TEMP | ✓ |
| 3 | Pillar Temp 1 | TEMP | ✓ |
| 4 | Pillar Temp 2 | TEMP | ✓ |
| 5 | Pillar Temp 3 | TEMP | ✓ |
| 6 | Dephlegmator Temp | TEMP | ✓ |
| 7 | Reflux Condenser Temp | TEMP | ✓ |
| 8 | Product Cooler Temp | TEMP | ✓ |
| 9 | Pillar Pressure | PRESSURE | ✓ |
| 10 | Pillar Base Pressure | PRESSURE | future |
| 11 | Kettle Level | LEVEL | ✓ |
| 12 | Reflux Drum Level | LEVEL | future |
| 13 | Product Flow | FLOW | ✓ |
| 14 | Water Dephl. Flow | FLOW | ✓ |
| 15 | Water Condenser Flow | FLOW | ✓ |
| 16 | Water Cooler Flow | FLOW | future |
| 17 | Water Inlet Temp | TEMP | future |

---

## 8. User Interfaces

Both UIs read from the same `/state` JSON and send the same text commands. They are always in sync with backend state. The UI never computes or stores its own process state.

### TFT Display (LVGL v9, 480×320 landscape)

**Header** — always visible: app title, status badge (RUNNING / STOPPED / SAFETY TRIP), room temperature, max active temperature, IP address.

**Mode Screen** — two large buttons to select Distillation or Rectification.

**Control Screen** — shows mode title, Master Power slider (0–100 %), editable sensor danger threshold rows (Pressure / Kettle / Pillar 1), BACK and START buttons.

**Monitor Screen** — live readings for all three core temperature sensors, pressure, level, flow rate, total volume, Master Power slider, STOP button.

**WiFi Panel** — SSID / password entry, triggered from the IP address in the header.

**Tmax Panel** — numeric keyboard overlay for editing per-sensor danger thresholds.

Screen transitions are driven exclusively by `/state` fields (`processMode`, `isRunning`). Overlay panels (WiFi, Tmax) suppress automatic transitions while open.

### Web UI (embedded HTML, Tailwind CSS dark theme)

Served from `GET /`, fully self-contained — no CDN assets are required except Tailwind via `cdn.tailwindcss.com`.

**Screen 0 — Mode Select** — two clickable cards.

**Screen 1 — Control** — Master Power slider, sensor threshold rows, BACK / START buttons. Header buttons: **Sensors** (opens sensor mapper modal) and **Valves Control** (switches to Screen 3).

**Screen 2 — Monitor** — live sensor readings with warn/danger colour coding, Master Power slider, STOP button.

**Screen 3 — Valves Control** — per-valve cards showing current open/closed state badge (updated every 600 ms poll), sensor dropdown, operator dropdown, threshold value input, and SET button. Dropdowns are never rebuilt during polling — only the state badge is updated, so open dropdowns stay stable.

**Sensor Mapper Modal** — SCAN BUS button calls `/api/sensor_scan`. Each of the 8 DS18B20 slots shows its current ROM hex, a dropdown of detected ROMs, and a SET button sending `SENSOR:MAP:N:ROMHEX`.

**WiFi Modal** — SSID / password fields, opened by clicking the IP address in the header.

The web UI polls `/state` every 600 ms. All display labels come from the `/state` JSON so that changing a string in `ui_strings.h` propagates to both UIs automatically.

---

## 9. NVS Persistence

All persistent data is stored under the `"distill"` NVS namespace.

| Key | Type | Contents |
|---|---|---|
| `lastValid` | uint8 | 1 = namespace contains valid saved state |
| `pmode` | int | processMode |
| `mPwr` | float | masterPower |
| `wasRunning` | bool | was running at last save |
| `lastTankC` | float | kettleTemp at last save |
| `tempMax` | float | safetyTempMaxC |
| `presMax` | float | safetyPresMaxBar |
| `tw_d0`–`tw_d2` | float | Distillation temp warn thresholds |
| `td_d0`–`td_d2` | float | Distillation temp danger thresholds |
| `tw_r0`–`tw_r2` | float | Rectification temp warn thresholds |
| `td_r0`–`td_r2` | float | Rectification temp danger thresholds |
| `pw_d`, `pd_d` | float | Distillation pressure warn/danger |
| `pw_r`, `pd_r` | float | Rectification pressure warn/danger |
| `rom0`–`rom7` | bytes[8] | DS18B20 ROM address per sensor slot |
| `v0os`–`v4os` | uint8 | Valve open-condition sensorId |
| `v0oo`–`v4oo` | uint8 | Valve open-condition operator |
| `v0ov`–`v4ov` | float | Valve open-condition value |
| `v0cs`–`v4cs` | uint8 | Valve close-condition sensorId |
| `v0co`–`v4co` | uint8 | Valve close-condition operator |
| `v0cv`–`v4cv` | float | Valve close-condition value |
| `wifiSsid` | String | WiFi SSID |
| `wifiPass` | String | WiFi password |

`valveOpen[]` and all live sensor readings are **not** persisted — valves always start closed on boot.

---

## 10. HTTP API

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Serves the embedded web UI (HTML) |
| `POST` | `/` | Send a plain-text command in `plain` form field |
| `GET` | `/state` | Full JSON state + labels + catalog |
| `POST` | `/api/flow_reset` | Reset `totalVolumeLiters` to 0 |
| `POST` | `/api/sensor_scan` | Rescan 1-Wire bus, return ROM list |
| `POST` | `/ota` | Trigger HTTPS OTA from `OTA_FIRMWARE_URL` |

### `/state` JSON fields (selected)
```json
{
  "fw": "3.5.2",
  "isRunning": false,
  "processMode": 1,
  "masterPower": 65.0,
  "roomTemp": 22.4,
  "kettleTemp": 78.1,
  "pillar1Temp": 76.3,
  "pillar2Temp": -20.0,
  "pillar3Temp": -20.0,
  "dephlegmTemp": -20.0,
  "refluxTemp": -20.0,
  "productTemp": -20.0,
  "pressureBar": 0.042,
  "levelHigh": true,
  "flowRateLPM": 0.12,
  "totalVolumeLiters": 3.450,
  "waterDephlLpm": -999.0,
  "waterCondLpm": -999.0,
  "sensorRoms": ["28AABBCCDDEEFF01", "28FF112233445506", "", "", "", "", "", ""],
  "tempSensorSlotNames": ["Room", "Kettle", "Pillar 1", "Pillar 2", "Pillar 3", "Dephlegmator", "Reflux Cond.", "Product Cooler"],
  "valveRules": [
    { "openWhen": { "sensorId": 2, "op": 1, "value": 78.5 }, "closeWhen": { "sensorId": 2, "op": 2, "value": 77.0 } }
  ],
  "valveOpen": [false, false, false, false, false],
  "valveNames": ["Dephlegmator", "Dripper", "Water", "Placeholder 1", "Placeholder 2"],
  "ruleSensors": [ { "id": 1, "kind": 1, "label": "Room Temp", "unit": "°C", "enabled": true }, "..." ],
  "threshDist": { "tempWarn": [80, 80, 80], "tempDanger": [92, 92, 92], "pressWarn": 0.06, "pressDanger": 0.08 },
  "threshRect": { "tempWarn": [78, 78, 78], "tempDanger": [88, 88, 88], "pressWarn": 0.06, "pressDanger": 0.08 },
  "safetyTempMaxC": 95.0,
  "safetyPresMaxBar": 0.09,
  "safetyTripped": false,
  "safetyMessage": ""
}
```

Offline sentinels: `TEMP_OFFLINE_THRESH = -20.0`, `SENSOR_OFFLINE = -999.0`. The JSON exports `tempOfflineThresh`, `pressureOfflineThresh`, `flowOfflineThresh` so the web UI never hardcodes these values.

---

## 11. Command Reference

All commands are plain UTF-8 strings sent via `POST /` with the string in the `plain` form field, or issued directly from the LVGL UI via `handleCommand()`. Both UIs use the identical command strings.

| Command | Description |
|---|---|
| `MODE:0` | Switch to Off. Stops run, resets masterPower, clears safety latch |
| `MODE:1` | Select Distillation mode |
| `MODE:2` | Select Rectification mode |
| `MASTER:NN.N` | Set master power to NN.N % (0–100). Applies to SSRs immediately |
| `START` | Start the process (requires mode ≠ 0, masterPower > 0, no safety trip) |
| `STOP` | Stop process, reset mode to 0, reset masterPower to 0, clear safety |
| `TMAX:NN.N` | Set `safetyTempMaxC` absolutely or ±relatively (e.g. `TMAX:+2`) |
| `TMAX:N:SET:val` | Set per-sensor danger threshold for active mode. N = 1, 2, or 3 |
| `THRESH:D:TW:N:val` | Set Distillation temp warn for sensor N (0-based) |
| `THRESH:D:TD:N:val` | Set Distillation temp danger for sensor N |
| `THRESH:R:TW:N:val` | Set Rectification temp warn for sensor N |
| `THRESH:R:TD:N:val` | Set Rectification temp danger for sensor N |
| `THRESH:D:PW:val` | Set Distillation pressure warn |
| `THRESH:D:PD:val` | Set Distillation pressure danger |
| `THRESH:R:PW:val` | Set Rectification pressure warn |
| `THRESH:R:PD:val` | Set Rectification pressure danger |
| `VALVE:N:OPENCFG:sensorId:op:value` | Configure valve N open condition |
| `VALVE:N:CLOSECFG:sensorId:op:value` | Configure valve N close condition |
| `SENSOR:MAP:N:ROMHEX` | Assign DS18B20 ROM to sensor slot N. ROMHEX = 16 hex chars |
| `WIFI:SET:ssid:pass` | Update WiFi credentials and reconnect. `:` is the delimiter — percent-encode any literal `:` in the SSID or password (e.g. `%3A`). The web UI applies `encodeURIComponent()` automatically; the LVGL panel uses an equivalent `urlEncode()` helper. The firmware decodes both fields with `urlDecodeString()` before saving. |

**Valve operator strings for `VALVE:` commands:**

| String | Meaning |
|---|---|
| `GT` | > |
| `LT` | < |
| `GTE` | >= |
| `LTE` | <= |
| `EQ` | == (±0.001 float tolerance) |
| `NONE` | Disabled (condition never triggers) |

---

## 12. Architecture

### FreeRTOS Tasks

| Task | Core | Priority | Stack | Function |
|---|---|---|---|---|
| `sensorsTask` | 0 | 2 | 4096 | DS18B20, pressure, level, product flow; triggers LVGL refresh |
| `flow2PollTask` | 0 | 2 | 4096 | Polls expander 2 every 2 ms for water flow pulses |
| `controlTask` | 0 | 3 | 4096 | Safety check, SSR apply, valve evaluation every 500 ms |
| `lvglTask` | 1 | 1 | 8192 | LVGL handler; calls `lv_timer_handler()` |
| Arduino `loop()` | 1 | — | — | HTTP client handling, LVGL refresh trigger, WiFi IP sync |

### Shared State

`AppState g_state` is the single source of truth. All tasks access it under `stateLock()` / `stateUnlock()` (FreeRTOS mutex). The recommended pattern is snapshot-then-release:
```cpp
stateLock();
AppState snap = g_state;
stateUnlock();
// use snap safely with no lock held
```

### UI Alignment

Both UIs derive all displayed values from `/state` JSON (web) or from an `AppState` snapshot (LVGL). Neither UI holds its own process state. String labels are centralised in `ui_strings.h`. DS18B20 slot names (`sensorName1`–`sensorName9`) and all common UI labels are exported as top-level fields in `/state` JSON. Non-temperature sensor labels (water flow, level, pressure catalog entries) are available via the `ruleSensors[]` array already in `/state`, which the web UI reads by `RuleSensorId` — new sensors become visible in the monitor automatically when enabled in `g_ruleSensors` without any JS change.

### Wire Bus Sharing

The I²C bus (IO17/IO18) is shared by expander 1 (0x20), expander 2 (0x21), and optionally future devices. A mutex in `expander.cpp` (`s_wireMutex`) protects all `Wire` transactions. `sensorsTask`, `controlTask` (valve writes), and `flow2PollTask` all acquire this mutex before any I²C operation.

---

## 13. File Reference

| File | Role |
|---|---|
| `DistillController.ino` | Entry point: boot sequence, WiFi, LVGL task, OTA |
| `config.h` | All pin assignments, timing constants, default thresholds |
| `ui_strings.h` | Centralised string constants for both LVGL and web UI |
| `state.h` / `state.cpp` | `AppState`, mutex helpers, NVS load/save, sensor catalog |
| `sensors.h` / `sensors.cpp` | DS18B20 (ROM-addressed), pressure, flow, level drivers |
| `expander.h` / `expander.cpp` | PCF8574 drivers (both expanders), Wire mutex, flow2PollTask |
| `ssr.h` / `ssr.cpp` | Software time-proportional SSR driver (esp_timer, 5 outputs) |
| `control.h` / `control.cpp` | Safety check, SSR apply, valve evaluation, command parser |
| `http_server.h` / `http_server.cpp` | WebServer routes, `/state` serialiser, `/api/sensor_scan` |
| `web_ui.h` / `web_ui.cpp` | Embedded HTML/JS web UI |
| `ui_lvgl.h` / `ui_lvgl.cpp` | LVGL panel layout, widget updates, screen transitions |
| `img_barrel.c` | Barrel graphic asset (LVGL image descriptor) |

---

## 14. Building

### Requirements

- Arduino IDE 2.x or arduino-cli
- ESP32 Arduino core **v3.x**
- Libraries:
  - `LVGL` v9
  - `Arduino_GFX_Library`
  - `JC3248W535EN-Touch-LCD`
  - `OneWire`
  - `DallasTemperature`
  - `ArduinoJson` v6
  - `Preferences` (bundled with ESP32 core)
  - `LittleFS` (bundled with ESP32 core)
  - `HTTPUpdate` (bundled with ESP32 core)

### Board Settings (Arduino IDE)

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 8 MB |
| Partition Scheme | Custom (to accommodate LittleFS) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

### First Boot

1. Flash firmware.
2. Connect to serial monitor (115200 baud) to confirm boot sequence.
3. Open the web UI at the device IP address.
4. Go to **Control → Sensors**, click **SCAN BUS** and assign DS18B20 ROM addresses to sensor slots.
5. Go to **Control → Valves Control** and configure open/close rules for each valve.
6. Select a process mode, set Master Power, and press START.

### OTA Update

Send `POST /ota` to trigger an HTTPS pull from `OTA_FIRMWARE_URL` (defined in `config.h`). The device reboots automatically on success.