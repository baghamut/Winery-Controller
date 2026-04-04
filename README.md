# Cask & Crown — DistillController

Firmware for the **JC3248W535EN ESP32-S3** board. Controls a distillation or rectification column with a 3.5″ LVGL touch display, an embedded web UI, rule-based valve automation, multi-sensor monitoring, and NVS-backed persistence.

---

## Table of Contents

1. [Hardware](#1-hardware)
2. [Pin Map](#2-pin-map)
3. [External Expanders](#3-external-expanders)
4. [Sensor System](#4-sensor-system)
5. [Process Control](#5-process-control)
6. [Safety System](#6-safety-system)
7. [Valve Automation](#7-valve-automation)
8. [User Interfaces](#8-user-interfaces)
9. [NVS Persistence](#9-nvs-persistence)
10. [HTTP API](#10-http-api)
11. [Command Reference](#11-command-reference)
12. [Architecture](#12-architecture)
13. [File Reference](#13-file-reference)
14. [Building](#14-building)

---

## 1. Hardware

| Component | Details |
|---|---|
| Board | JC3248W535EN (ESP32-S3, 8 MB Flash, 8 MB PSRAM) |
| Display | 3.5″ 480×320 IPS, AXS15231B controller, QSPI interface |
| Touch | AXS15231B I²C touch, integrated on display panel |
| Temperature | Up to 8 × DS18B20 on single 1-Wire bus |
| Pressure | Ratiometric 0.5–4.5 V analog sensor on ADC GPIO |
| Flow | Hall-effect pulse sensor (product line), native GPIO interrupt |
| Water flow | 2 × hall-effect sensors via PCF8574 polling (expander 2) |
| Level | Float switch via PCF8574 digital input (expander 1) |
| Valves | 3 × relay/SSR outputs via PCF8574 (expander 1) |
| Heaters | 5 × SSR outputs, LEDC 10 Hz PWM |

---

## 2. Pin Map

### Connector 1 — 8-pin

| GPIO | Function |
|---|---|
| IO5 | SSR1 — Distillation heater A |
| IO6 | SSR2 — Distillation heater B |
| IO7 | SSR3 — Distillation heater C |
| IO15 | SSR4 — Rectification heater A |
| IO16 | SSR5 — Rectification heater B |
| IO46 | Product flow sensor (interrupt) |
| IO9 | Pillar pressure sensor (ADC, 0.5–4.5 V) |
| IO14 | DS18B20 1-Wire bus |

### Connector 2 — 4-pin (I²C shared bus)

| GPIO | Function |
|---|---|
| IO17 | I²C SDA — both PCF8574 expanders |
| IO18 | I²C SCL — both PCF8574 expanders |

### Display (QSPI)

| GPIO | Function |
|---|---|
| IO45 | CS |
| IO47 | CLK |
| IO21, IO48, IO40, IO39 | D0–D3 |
| IO1 | Backlight |

### Touch (I²C, internal to display module)

| GPIO | Function |
|---|---|
| IO4 | SDA |
| IO8 | SCL |
| IO3 | Interrupt |

---

## 3. External Expanders

Both expanders share the Connector 2 I²C bus. A firmware mutex prevents concurrent bus access from sensorsTask, controlTask, and flow2PollTask.

### Expander 1 — PCF8574 at 0x20 (valves + level)

| Bit | Direction | Function |
|---|---|---|
| 0 | Input | Kettle level switch (LOW = OK) |
| 1 | Output | Valve 1 — Dephlegmator |
| 2 | Output | Valve 2 — Dripper |
| 3 | Output | Valve 3 — Water |
| 4–7 | — | Spare |

### Expander 2 — PCF8574 at 0x21 (water flow sensors)

| Bit | Direction | Function |
|---|---|---|
| 0 | Input | Water Dephlegmator flow pulses |
| 1 | Input | Water Condenser flow pulses |
| 2–7 | — | Spare |

Expander 2 is polled by a dedicated FreeRTOS task (`flow2PollTask`) every 2 ms. Falling edges are counted per bit and accumulated into `g_waterDephlPulses` / `g_waterCondPulses`, which `sensorsUpdate()` harvests every second.

---

## 4. Sensor System

### DS18B20 Temperature Sensors (1-Wire, GPIO14)

Up to 8 sensors are supported. Each sensor slot is addressed by its unique 64-bit ROM address, stored in NVS. This eliminates enumeration-order ambiguity when sensors are connected or disconnected.

**Slot assignment (slot index → sensor role):**

| Slot | Name | AppState field |
|---|---|---|
| 0 | Room Temp | `roomTemp` |
| 1 | Kettle Temp | `kettleTemp` |
| 2 | Pillar Temp 1 | `pillar1Temp` |
| 3 | Pillar Temp 2 | `pillar2Temp` |
| 4 | Pillar Temp 3 | `pillar3Temp` |
| 5 | Dephlegmator Temp | `dephlegmTemp` |
| 6 | Reflux Condenser Temp | `refluxTemp` |
| 7 | Product Cooler Temp | `productTemp` |

Unassigned slots (ROM all-zero) return `TEMP_OFFLINE_THRESH` (-20 °C sentinel). All systems treat any value ≤ `TEMP_OFFLINE_THRESH` as "offline" and skip it.

**Sensor Mapper:** ROM addresses are assigned via the web UI Sensor Mapping modal (on the Control screen). Clicking **SCAN BUS** issues a `POST /api/sensor_scan` which re-enumerates the 1-Wire bus and returns detected ROMs. The operator selects the correct ROM for each slot from a dropdown and clicks SET, which sends a `SENSOR:MAP:N:ROMHEX` command.

### Pressure Sensor (GPIO9, ADC)

Ratiometric analog sensor. Firmware applies jitter detection (8-sample rolling window), a hysteresis counter for online/offline transitions, and a low-pass filter (α = 0.2). Reports as `SENSOR_OFFLINE` (-999) until 12 consecutive stable in-range samples are received.

### Level Sensor

Float switch on expander 1 bit 0. LOW = level OK. Reports as `levelHigh` (bool) in AppState.

### Product Flow Sensor (GPIO46)

Hall-effect pulse sensor. Interrupt-driven pulse counting on rising edges. L/min calculated every `FLOW_COMPUTE_INTERVAL_MS` (1000 ms). Accumulated volume stored in `totalVolumeLiters`. Reports as `SENSOR_OFFLINE` until at least one non-zero reading.

### Water Flow Sensors (Expander 2)

Two hall-effect sensors polled via PCF8574 at 0x21. Same pulse-per-litre calibration constant as the product flow sensor (`FLOW_PULSES_PER_LITRE = 450`). Reports as `SENSOR_OFFLINE` until first non-zero reading. Fields: `waterDephlLpm`, `waterCondLpm`.

---

## 5. Process Control

### Modes

| Mode | SSRs active | Label |
|---|---|---|
| 0 (Off) | None | — |
| 1 | SSR1, SSR2, SSR3 | Distillation |
| 2 | SSR4, SSR5 | Rectification |

### Master Power

A single `masterPower` value (0–100 %) drives all active SSRs simultaneously at the same LEDC duty cycle (10 Hz, 8-bit). There are no per-SSR controls. Setting `masterPower = 0` while running turns heaters off without stopping the process.

### Starting a Run

START is accepted only when:
- `processMode` is 1 or 2
- `masterPower > 0`
- `safetyTripped` is false

### Stopping

STOP resets `isRunning`, `processMode`, and `masterPower` to 0, clears the safety latch, and records `kettleTemp` as `lastTankTempC` for auto-restore.

### Power-Glitch Auto-Restore

On boot, after sensors warm up (1.2 s delay), the firmware checks:

1. `wasRunning` was true at last save
2. `processMode ≠ 0`
3. `safetyTripped` is false
4. `masterPower > 0`
5. If both the saved kettle temperature (`lastTankC`) and the current kettle reading are valid, the drop between them must not exceed `AUTO_RESTORE_MAX_TEMP_DROP_C` (5 °C). If either reading is unavailable the temperature check is skipped and the process resumes — `controlTask` safety will catch real over-temperature once sensors come online.

If all conditions pass, the process resumes automatically — recovering from short power blips without operator intervention.

---

## 6. Safety System

`controlSafetyCheck()` runs every `CONTROL_LOOP_MS` (500 ms) while the process is running.

### Per-sensor danger check

Core sensors (Room, Kettle, Pillar 1) are checked against their individual `tempDanger[]` thresholds from the active mode's `SensorThresholds`. Extended sensors (Pillar 2/3, Dephlegmator, Reflux, Product Cooler) are checked against the global `safetyTempMaxC` ceiling.

### Pressure danger check

`pressureBar` is checked against `pressDanger` from the active threshold set. Offline pressure (≤ SENSOR_OFFLINE + 1) is skipped.

### Global temperature backstop

All online temperature sensors are compared against `safetyTempMaxC` regardless of per-sensor settings.

### Trip behaviour

On trip: `safetyTripped = true`, human-readable `safetyMessage` set, `isRunning = false`, `masterPower = 0`, all SSRs cut immediately. Valve evaluation continues (valves are not force-closed on trip — their rules remain active).

Safety is cleared by sending `MODE:0` (returns to idle).

### Configurable thresholds

Per-sensor warn/danger temperatures and pressure warn/danger values are stored in `threshDist` / `threshRect` inside AppState, persisted in NVS, and editable from both UIs. Default values are set in `config.h`.

---

## 7. Valve Automation

Five valves are supported:

| Index | Name | Hardware |
|---|---|---|
| 0 | Dephlegmator | Expander 1 bit 1 |
| 1 | Dripper | Expander 1 bit 2 |
| 2 | Water | Expander 1 bit 3 |
| 3 | Placeholder 1 | (no hardware yet) |
| 4 | Placeholder 2 | (no hardware yet) |

### Rule Model

Each valve has one **open condition** and one **close condition**. A condition specifies:

- **Sensor** — selected from the sensor catalog by stable ID
- **Operator** — `>`, `<`, `>=`, `<=`, `==`, or `--` (disabled)
- **Value** — float threshold

Using separate open and close thresholds creates a natural deadband, preventing chattering when a sensor reading oscillates near a single trip point.

**Example rules:**
```
Valve 0 (Dephlegmator):
  Open when:  Kettle Temp  >  78.5 °C
  Close when: Kettle Temp  <  77.0 °C

Valve 2 (Water):
  Open when:  Pillar Pressure  >  0.06 bar
  Close when: Pillar Pressure  <  0.04 bar
```

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
  "fw": "3.5.0",
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
| `ssr.h` / `ssr.cpp` | LEDC PWM driver for 5 SSR outputs |
| `control.h` / `control.cpp` | Safety check, SSR apply, valve evaluation, command parser |
| `http_server.h` / `http_server.cpp` | WebServer routes, `/state` serialiser, `/api/sensor_scan` |
| `web_ui.h` / `web_ui.cpp` | Embedded HTML/JS web UI |
| `ui_lvgl.h` / `ui_lvgl.cpp` | LVGL panel layout, widget updates, screen transitions |
| `img_barrel.c` | Barrel graphic asset (LVGL image descriptor) |

---

## 14. Building

### Requirements

- Arduino IDE 2.x or arduino-cli
- ESP32 Arduino core **v3.x** (uses pin-based LEDC API: `ledcAttach` / `ledcWrite`)
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
