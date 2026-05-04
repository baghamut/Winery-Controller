# DistillController

ESP32-S3 firmware for a distillation / rectification column controller,
running on the **JC3248W535EN** board (3.5″ 480 × 320 IPS touchscreen).

| | |
|---|---|
| **Board** | JC3248W535EN (ESP32-S3, 16 MB flash, OPI PSRAM) |
| **Display** | 480 × 320 IPS, landscape, capacitive touch |
| **Framework** | Arduino ESP32 core v3.x, PlatformIO / arduino-cli |
| **UI** | LVGL v9 (TFT) + Tailwind dark-theme web UI + always-on monitor page |
| **Server** | `esp_https_server` (mbedTLS, port 443) |
| **Connectivity** | Cloudflare DDNS, Let's Encrypt TLS, pull + push OTA |
| **Domain** | `winery.baghamut.com` |

---

## 1. Hardware

| Subsystem | Detail |
|---|---|
| SSR Outputs | 5 × SSR, software time-proportional (2 s period / 100 ms tick via `esp_timer`), GPIO 5 / 6 / 7 / 15 / 16 |
| Temperature | 8 × DS18B20 on 1-Wire GPIO 14, ROM-addressed (not index-based) |
| Pressure | Analog sensor on GPIO 9 (ADC) |
| Product Flow | Pulse sensor on GPIO 46 (hardware interrupt) |
| Expander 1 | PCF8574 @ 0x20 — level switch (bit 0) + 3 valves (bits 1–3) |
| Expander 2 | PCF8574 @ 0x21 — 2 water flow sensors polled every 2 ms |
| I²C Bus | GPIO 17 (SDA) / GPIO 18 (SCL), shared, Wire mutex in `expander.cpp` |

### DS18B20 Slot Assignments

| Slot | Name | Location |
|---|---|---|
| 0 | Room | Ambient |
| 1 | Kettle | Boiler vessel |
| 2 | Pillar 1 | Column bottom |
| 3 | Pillar 2 | Column middle |
| 4 | Pillar 3 | Column top |
| 5 | Dephlegmator | Partial condenser |
| 6 | Reflux | Reflux condenser |
| 7 | Product Cooler | Product output cooler |

---

## 2. Process Modes

| Mode | Value | SSRs Active | Description |
|---|---|---|---|
| Off | 0 | None | Idle — no heaters, valves close |
| Distillation | 1 | SSR 4–5 | Simple pot still run |
| Rectification | 2 | SSR 1–3 | Column rectification run |

`STOP` resets mode to 0, power to 0, and clears any safety trip. `START` requires mode ≠ 0, `masterPower > 0`, and no active safety trip.

---

## 3. Safety System

`controlTask` runs a safety check every 500 ms. If any **online** sensor exceeds its configured danger threshold, or if `safetyTempMaxC` / `safetyPresMaxBar` global limits are breached:

1. All SSRs set to 0 %.
2. `safetyTripped = true`, `safetyMessage` set to the trigger reason.
3. `isRunning = false`.
4. Safety latch remains until `MODE:0` or `STOP` clears it.

Offline sensors (reading `TEMP_OFFLINE_THRESH = −20.0` or `SENSOR_OFFLINE = −999.0`) never trigger safety conditions.

### Temperature Sensor Debounce

A single missed read holds the last known good value silently. After 3 consecutive misses the sensor goes to `TEMP_OFFLINE_THRESH`. Any valid read restores the sensor instantly and resets the miss counter. This prevents transient 1-Wire glitches from flickering the display or triggering false safety trips.

---

## 4. Valve Automation

Five valves (3 physical on expander 1, 2 placeholder). Each valve has an independent open-condition and close-condition, both configurable via `VALVE:N:OPENCFG:` / `VALVE:N:CLOSECFG:` commands.

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

## 5. Flow Counter

The product flow sensor counts pulses via hardware interrupt on GPIO 46. `totalVolumeLiters` accumulates during a run. On `START`, the current total is saved to `lastRunVolumeLiters` and then `totalVolumeLiters` resets to 0. After `STOP`, the header area shows the frozen `lastRunVolumeLiters` so operators can read the final volume of the completed run. `POST /api/flow_reset` resets `totalVolumeLiters` to 0 manually.

---

## 6. User Interfaces

Both UIs read from the same `/state` JSON and send the same text commands. They are always in sync with backend state. The UI never computes or stores its own process state.

### TFT Display (LVGL v9, 480 × 320 landscape)

**Header** — always visible: app title, status badge (RUNNING / STOPPED / SAFETY TRIP), room temperature, max active temperature, IP address.

**Mode Screen** — two large buttons to select Distillation or Rectification.

**Control Screen** — shows mode title, Master Power slider (0–100 %), editable sensor danger threshold rows (Pressure / Kettle / Pillar 1), BACK and START buttons.

**Monitor Screen** — live readings for all three core temperature sensors, pressure, level, flow rate, total volume (or last-run volume when stopped), Master Power slider, STOP button.

**WiFi Panel** — SSID / password entry, triggered from the IP address in the header.

**Tmax Panel** — numeric keyboard overlay for editing per-sensor danger thresholds.

Screen transitions are driven exclusively by `/state` fields (`processMode`, `isRunning`). Overlay panels (WiFi, Tmax) suppress automatic transitions while open.

### Web UI (`data/web_ui.html`, Tailwind CSS dark theme)

Served from LittleFS at `GET /`, fully self-contained except Tailwind via `cdn.tailwindcss.com`. Falls back to a minimal inline page if the LittleFS file is missing.

**Screen 0 — Mode Select** — two clickable cards.

**Screen 1 — Control** — Master Power slider, sensor threshold rows, BACK / START buttons. Header buttons: **Sensors** (opens sensor mapper modal) and **Valves Control** (switches to Screen 3).

**Screen 2 — Monitor** — live sensor readings with warn/danger colour coding, Master Power slider, STOP button.

**Screen 3 — Valves Control** — per-valve cards showing current open/closed state badge (updated every poll), sensor dropdown, operator dropdown, threshold value input, and SET button. Dropdowns are never rebuilt during polling — only the state badge is updated, so open dropdowns stay stable.

**Sensor Mapper Modal** — SCAN BUS button calls `/api/sensor_scan`. Each of the 8 DS18B20 slots shows its current ROM hex, a dropdown of detected ROMs, and a SET button sending `SENSOR:MAP:N:ROMHEX`.

**WiFi Modal** — SSID / password fields, opened by clicking the IP address in the header.

The web UI polls `/state` every 3 seconds. All display labels come from the `/state` JSON so that changing a string in `ui_strings.h` propagates to both UIs automatically. Commands are sent as raw text with `Content-Type: text/plain` via `POST /`.

### Monitor Page (`data/monitor.html`)

A dedicated always-on page for wall-mounted screens, served from LittleFS at `GET /monitor.html`. Shows a background image of the physical apparatus (`Background.png`) with draggable sensor overlays positioned at the physical sensor locations. Overlay positions are saved to `localStorage`.

**Controls**: Master Power slider and STOP button only — the monitor page cannot start a process or change mode.

**Adaptive Polling**: Identifies itself via `?client=monitor` query parameter. When an interactive client (web UI) is active, the monitor backs off to 10-second polling to reduce server load. When no interactive client is active, it polls at the normal 3-second interval.

**Client tracking**: The server tracks `s_lastInteractiveMs` — updated on every `/state` request from a non-monitor client. The `/state` response includes `interactiveActive` (true if an interactive client was seen within the last 15 seconds). `GET /api/ping` is available for lightweight keep-alive.

---

## 7. NVS Persistence

### `"distill"` Namespace

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
| `cf_token` | String | Cloudflare API token |
| `cf_zone_id` | String | Cloudflare Zone ID |
| `cf_rec_id` | String | Cloudflare DNS record ID |

`valveOpen[]` and all live sensor readings are **not** persisted — valves always start closed on boot.

### `"certs"` Namespace

| Key | Type | Contents |
|---|---|---|
| `cert` | bytes | Server certificate PEM (fullchain) |
| `key` | bytes | RSA private key PEM |

Falls back to compiled-in `certs.h` when NVS is empty (first boot or after NVS erase).

---

## 8. HTTP API

All endpoints are HTTPS (port 443). The server uses `esp_https_server` with mbedTLS.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Serves the web UI from LittleFS (`web_ui.html`) |
| `POST` | `/` | Send a plain-text command (body = raw command string) |
| `GET` | `/state` | Full JSON state + labels + catalog |
| `GET` | `/monitor.html` | Serves the monitor page from LittleFS |
| `GET` | `/Background.png` | Monitor background image from LittleFS |
| `GET` | `/Barrel_Big.png` | Barrel graphic from LittleFS |
| `GET` | `/Logo.png` | Logo graphic from LittleFS |
| `GET` | `/favicon.ico` | Browser favicon from LittleFS |
| `GET` | `/api/ping` | Lightweight keep-alive (returns `{"ok":true}`) |
| `POST` | `/api/flow_reset` | Reset `totalVolumeLiters` to 0 |
| `POST` | `/api/sensor_scan` | Rescan 1-Wire bus, return ROM list |
| `POST` | `/api/ota/trigger` | Wake pull-OTA task for immediate version check |
| `POST` | `/api/ota/upload` | Push OTA — multipart upload of `.bin` firmware |
| `POST` | `/api/update_cert` | Push new TLS cert + key (JSON body: `{"cert":"...","key":"..."}`) |

### `/state` JSON Fields (selected)

```json
{
  "fw": "3.5.8",
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
  "lastRunVolumeLiters": 2.100,
  "waterDephlLpm": -999.0,
  "waterCondLpm": -999.0,
  "interactiveActive": false,
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

## 9. Command Reference

All commands are plain UTF-8 strings sent via `POST /` with the command as the raw request body (`Content-Type: text/plain`), or issued directly from the LVGL UI via `handleCommand()`. Both UIs use the identical command strings.

| Command | Description |
|---|---|
| `MODE:0` | Switch to Off. Stops run, resets masterPower, clears safety latch |
| `MODE:1` | Select Distillation mode |
| `MODE:2` | Select Rectification mode |
| `MASTER:NN.N` | Set master power to NN.N % (0–100). Applies to SSRs immediately |
| `START` | Start the process (requires mode ≠ 0, masterPower > 0, no safety trip). Saves current `totalVolumeLiters` to `lastRunVolumeLiters` and resets flow counter to 0 |
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
| `WIFI:SET:ssid:pass` | Update WiFi credentials and reconnect. `:` is the delimiter — percent-encode any literal `:` in the SSID or password (e.g. `%3A`). The web UI applies `encodeURIComponent()` automatically; the LVGL panel uses an equivalent `urlEncode()` helper. The firmware decodes both fields with `urlDecodeString()` before saving |

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

## 10. Architecture

### FreeRTOS Tasks

| Task | Core | Priority | Stack | Function |
|---|---|---|---|---|
| `sensorsTask` | 0 | 2 | 4096 | DS18B20 (with debounce), pressure, level, product flow; triggers LVGL refresh |
| `flow2PollTask` | 0 | 2 | 4096 | Polls expander 2 every 2 ms for water flow pulses |
| `controlTask` | 0 | 3 | 4096 | Safety check, SSR apply, valve evaluation every 500 ms |
| `ddnsTask` | 0 | 1 | 8192 | Cloudflare DDNS A-record updater (every 5 min, changed-IP only) |
| `otaPullTask` | 0 | 1 | 8192 | Pull OTA from GitHub Releases (30 min check, stopped-only) |
| `lvglTask` | 1 | 1 | 8192 | LVGL handler; calls `lv_timer_handler()` |
| `esp_https_server` | — | — | 12288 | mbedTLS HTTPS server (internal task, not user-created) |
| Arduino `loop()` | 1 | — | — | HTTP client handling, LVGL refresh trigger, WiFi IP sync |

### Shared State

`AppState g_state` is the single source of truth. All tasks access it under `stateLock()` / `stateUnlock()` (FreeRTOS mutex). The recommended pattern is snapshot-then-release:

```cpp
stateLock();
AppState snap = g_state;
stateUnlock();
// use snap safely with no lock held
```

NVS writes must not occur on the LVGL task (e.g. slider release callbacks). Use the command queue pattern: `g_cmdQueue` is drained in `loop()` to decouple LVGL callbacks from NVS writes.

### UI Alignment

Both UIs derive all displayed values from `/state` JSON (web) or from an `AppState` snapshot (LVGL). Neither UI holds its own process state. String labels are centralised in `ui_strings.h`. DS18B20 slot names and all common UI labels are exported as top-level fields in `/state` JSON. Non-temperature sensor labels are available via the `ruleSensors[]` array, which the web UI reads by `RuleSensorId` — new sensors become visible in the monitor automatically when enabled in `g_ruleSensors` without any JS change.

### HTTPS Server

`esp_https_server` (mbedTLS) serves all HTTP endpoints on port 443. All mbedTLS allocations (SSL record buffers, handshake state, certificate parsing — ~40 KB per session) are redirected to PSRAM via `mbedtls_platform_set_calloc_free()`, keeping internal DRAM free for lwIP and FreeRTOS. TLS sessions are capped at 3 concurrent connections (`max_open_sockets = 3`) with LRU eviction enabled. TCP accept backlog is limited to 1 to prevent concurrent handshake storms. TLS session tickets are disabled to save ~10 KB DRAM per session. Request header buffer is 2048 bytes.

### Wire Bus Sharing

The I²C bus (IO17/IO18) is shared by expander 1 (0x20), expander 2 (0x21), and optionally future devices. A mutex in `expander.cpp` (`s_wireMutex`) protects all `Wire` transactions. `sensorsTask`, `controlTask` (valve writes), and `flow2PollTask` all acquire this mutex before any I²C operation.

---

## 11. File Reference

| File | Role |
|---|---|
| `DistillController.ino` | Boot sequence, WiFi, mbedTLS PSRAM redirect, task creation |
| `config.h` | Pin assignments, timing constants, default thresholds, NVS key names |
| `config.example.h` | Sanitised copy of `config.h` (credentials scrubbed), tracked in git |
| `ui_strings.h` | All display strings for both LVGL and web UI |
| `state.h` / `state.cpp` | `AppState`, mutex helpers, NVS load/save, sensor catalog |
| `sensors.h` / `sensors.cpp` | DS18B20 (ROM-addressed, debounce), pressure, flow, level drivers |
| `expander.h` / `expander.cpp` | PCF8574 drivers (both expanders), Wire mutex, `flow2PollTask` |
| `ssr.h` / `ssr.cpp` | Software time-proportional SSR driver (`esp_timer`, 5 outputs) |
| `control.h` / `control.cpp` | Safety check, SSR apply, valve evaluation, command parser |
| `http_server.h` / `http_server.cpp` | HTTPS routes, `/state` serialiser, interactive client tracking, push OTA handler, cert NVS storage |
| `web_ui.h` / `web_ui.cpp` | LittleFS file streaming for all web assets |
| `ddns.h` / `ddns.cpp` | Cloudflare DNS A-record updater (FreeRTOS task, Core 0) |
| `ota.h` / `ota.cpp` | Pull OTA from GitHub Releases (30 min check, stopped-only) |
| `certs.h` | Compiled-in fallback TLS certificate |
| `ui_lvgl.h` / `ui_lvgl.cpp` | LVGL panel layout, widget updates, screen transitions |
| `data/web_ui.html` | Interactive web UI — served from LittleFS at `/` |
| `data/monitor.html` | Always-on monitor page — served from LittleFS at `/monitor.html` |
| `data/Background.png` | Apparatus photo — monitor page background |
| `data/Barrel_Big.png` | Barrel graphic — served from LittleFS, also loaded by LVGL |
| `data/Logo.png` | Logo graphic — served from LittleFS |
| `data/favicon.ico` | Browser favicon |
| `version.json` | Repo root — OTA version metadata, updated by `bump-push-release.sh` |
| `partitions.csv` | Custom partition table (app + LittleFS) |
| `flash-ota.sh` | Compile + push firmware to device over HTTP OTA |
| `bump-push-release.sh` | Version bump, config sync, git commit, GitHub release, OTA binary build |
| `renew-winery-cert.sh` | macOS cron script: certbot renewal + cert push to device |

---

## 12. Building

### Requirements

- Arduino IDE 2.x or `arduino-cli`
- ESP32 Arduino core **v3.x**
- Libraries: `LVGL` v9, `Arduino_GFX_Library`, `JC3248W535EN-Touch-LCD`, `OneWire`, `DallasTemperature`, `ArduinoJson` v7, `Preferences` (bundled), `LittleFS` (bundled), `HTTPUpdate` (bundled)

### Board Settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16 MB |
| Partition Scheme | `app3M_fat9M_16MB` |
| PSRAM | OPI PSRAM |
| Upload Speed | 115200 |
| CDC On Boot | Enabled |
| FQBN | `esp32:esp32:esp32s3:UploadSpeed=115200,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi` |

### First Boot

1. Flash firmware via USB.
2. Upload LittleFS image (see below) — required for both web UIs.
3. Connect to serial monitor (115200 baud) to confirm boot sequence.
4. Open `https://<device-IP>`. Accept the cert warning or install the CA cert.
5. Go to **Control → Sensors**, click **SCAN BUS**, assign DS18B20 ROM addresses.
6. Go to **Control → Valves**, configure open/close rules.
7. Select a process mode, set Master Power, and press START.
8. Open `https://<device-IP>/monitor.html` on the wall-mounted screen.

### Uploading the Filesystem Image

The LittleFS image contains `web_ui.html`, `monitor.html`, `Background.png`, `Barrel_Big.png`, `Logo.png`, and `favicon.ico`. Flash it once after firmware, and again whenever any asset file changes — no firmware reflash needed.

**Arduino IDE 2.x** — Install the *LittleFS Upload* plugin, then use **Sketch → Upload Sketch Data**.

**arduino-cli / mklittlefs:**

```bash
mklittlefs -c data/ -b 4096 -p 256 -s 0x180000 littlefs.bin
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
           write_flash 0x670000 littlefs.bin
```

Partition offset `0x670000` and size `0x180000` match `partitions.csv`.

### OTA Update

**Pull OTA** — the device checks `version.json` on GitHub every 30 minutes and self-updates when a newer version is found (process must be stopped). Trigger an immediate check:

```bash
curl -k -X POST https://<device-IP>/api/ota/trigger
```

**Push OTA** — compile and flash directly over the network:

```bash
./flash-ota.sh <device-IP>
```

This compiles via `arduino-cli`, then pushes the binary to `/api/ota/upload` using multipart `curl -F`. Build artifacts go to `/tmp/arduino_fw_build`.

---

## 13. TLS Certificate Management

### NVS Cert Storage

The device loads TLS certificates from NVS (`"certs"` namespace) on boot. If NVS is empty, it falls back to the compiled-in cert in `certs.h`. Push a new cert at any time:

```bash
curl -k -X POST https://<device-IP>/api/update_cert \
  -H "Content-Type: application/json" \
  -d '{"cert":"<fullchain PEM>","key":"<private key PEM>"}'
```

The device saves to NVS and reboots. Serial output confirms: `[HTTPS] Cert loaded from NVS`.

### Let's Encrypt + Cloudflare DNS

Certificates are issued via `certbot` with the `certbot-dns-cloudflare` plugin (Python venv at `~/.certbot-venv`). The renewal script `renew-winery-cert.sh` handles unattended renewal and pushes the new cert to the device automatically. Run it via cron:

```
0 3 1 * * /path/to/renew-winery-cert.sh >> /var/log/certbot-renew.log 2>&1
```

---

## 14. DDNS (Cloudflare)

`ddnsTask` runs on Core 0 at priority 1 with 8 KB stack. On boot it waits 15 seconds for the DHCP lease to stabilise, then:

1. Fetches public WAN IP via `api.ipify.org` (fallback: `ifconfig.me/ip`).
2. If the IP has changed since the last update, PATCHes the Cloudflare DNS A-record for `winery.baghamut.com`.
3. Rechecks every 5 minutes.

Cloudflare credentials (`cf_token`, `cf_zone_id`, `cf_rec_id`) are stored in NVS under the `"distill"` namespace — never in source code. Write them once via the `PROVISION_CF_CREDENTIALS` block in `setup()`, then remove the define and reflash.

The DNS A record for `winery.baghamut.com` must exist in Cloudflare before the updater can patch it.

---

## 15. Release Workflow

### `bump-push-release.sh`

Automated version bump, build, and GitHub release:

1. Selects project interactively from `~/Documents/Arduino/`.
2. Reads `FW_VERSION` from `config.h`, auto-increments PATCH.
3. Updates `config.h` in-place with the new version.
4. Copies `config.h` → `config.example.h` with credentials scrubbed via `sed`.
5. Compiles firmware via `arduino-cli` — reverts all modified files on build failure.
6. Updates `version.json` with the new version and download URL.
7. Commits, force-pushes to `origin main`.
8. Creates annotated git tag and GitHub Release with the `.bin` attached.

Build artifacts go to `/tmp/arduino_fw_build`. `config.h` is in `.gitignore` — only `config.example.h` is tracked.

### `flash-ota.sh`

Quick compile-and-push for development:

```bash
./flash-ota.sh 192.168.1.42
```

Compiles the sketch, then pushes the binary to the device's `/api/ota/upload` endpoint via `curl -F`. The device reboots automatically on success.
