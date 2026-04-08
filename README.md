# DistillController

ESP32-S3 firmware for controlling a home rectification and distillation column.  
Board: **JC3248W535EN** (3.5″ 480×320 IPS, landscape).  
Current firmware: **3.6.1**

---

## 1. Hardware

| Item | Detail |
|---|---|
| MCU | ESP32-S3, OPI PSRAM |
| Display | AXS15231B QSPI, 480×320 IPS, landscape |
| Touch | AXS15231B I²C |
| SSR outputs | 5× (GPIO 5/6/7/15/16), software time-proportional 2 s period |
| Temperature | 8× DS18B20 on 1-Wire GPIO14, ROM-addressed |
| Pressure | Analog, GPIO9 ADC |
| Product flow | Hall-effect, GPIO46 hardware interrupt |
| Expander 1 | PCF8574 @ 0x20 — kettle level switch + 3 valve outputs |
| Expander 2 | PCF8574 @ 0x21 — 2 water flow sensors polled every 2 ms |
| I²C bus | GPIO17/18, shared, Wire mutex in `expander.cpp` |

### DS18B20 slot map

| Slot | Sensor |
|---|---|
| 0 | Room / ambient |
| 1 | Kettle |
| 2 | Pillar 1 |
| 3 | Pillar 2 |
| 4 | Pillar 3 |
| 5 | Dephlegmator |
| 6 | Reflux Condenser |
| 7 | Product Cooler |

### SSR → mode mapping

| Mode | Active SSRs | GPIOs |
|---|---|---|
| 1 — Distillation | SSR4, SSR5 | 15, 16 |
| 2 — Rectification | SSR1, SSR2, SSR3 | 5, 6, 7 |

---

## 2. Software stack

- Arduino ESP32 core v3.x  
- LVGL v9  
- FreeRTOS dual-core  
- ArduinoJson v7  
- OneWire + DallasTemperature  
- `esp_https_server` (mbedTLS, port 443 only)  
- LittleFS for web assets  
- Tailwind CSS (CDN) for web UIs  

---

## 3. Architecture

### Shared state

`AppState g_state` is the single source of truth. All tasks access it under `stateLock()` / `stateUnlock()`. Recommended pattern:

```cpp
stateLock();
AppState snap = g_state;
stateUnlock();
// work on snap with no lock held
```

### FreeRTOS tasks

| Task | Core | Priority | Stack |
|---|---|---|---|
| `sensorsTask` | 0 | 2 | 4096 |
| `flow2PollTask` | 0 | 2 | 4096 |
| `controlTask` | 0 | 3 | 4096 |
| `ddnsTask` | 0 | 1 | 4096 |
| `ota_pull` | 0 | 1 | 6144 |
| `lvglTask` | 1 | 1 | 8192 |
| `esp_https_server` | — | — | 8192 |
| Arduino `loop()` | 1 | — | — |

### Control logic

`controlTask` runs every 500 ms:
1. Safety check — trips and shuts down on over-temp or over-pressure.
2. `applySsrFromState()` — writes duty cycle to all active SSRs.
3. `valveEvaluateAll()` — evaluates all valve rules regardless of run state.
4. Increments `timerElapsedMs` while running.

All active SSRs for the selected mode receive the **same** `masterPower` duty cycle (0–100 %). No per-SSR control.

### Wire bus

The I²C bus is shared by both PCF8574 expanders. A mutex in `expander.cpp` (`s_wireMutex`) protects all `Wire` transactions from `sensorsTask`, `controlTask`, and `flow2PollTask`.

---

## 4. User interfaces

Both UIs derive all state from `/state` JSON and send the same command strings to `handleCommand()`. They are always in sync.

### TFT display (LVGL v9, 480×320 landscape)

**Header** — always visible: status badge, room temp, max active temp, total volume, IP address (tap to open WiFi config).

**Mode screen** — two large buttons: Rectification (left, SSR1–3) and Distillation (right, SSR4–5).

**Control screen** — Master Power slider, sensor danger threshold rows (Pressure / Kettle / Pillar 1), BACK and START buttons.

**Monitor screen** — live readings for all sensors, Master Power slider, STOP button.

**WiFi panel** — SSID / password overlay, opened by tapping the IP address.

**Tmax panel** — numeric keyboard overlay for per-sensor danger threshold editing.

Screen transitions are driven exclusively by `/state` fields (`processMode`, `isRunning`). Overlay panels suppress automatic transitions while open.

### Web UI (`/` — `web_ui.html`)

Full interactive control interface. Polls `/state?client=interactive` every 1 s. Sends `GET /api/ping` on load and every 30 s as a keepalive so the server knows an interactive operator is present.

Screens: Mode Select → Control (power slider, threshold editing, sensor mapper, valve config) → Monitor (live readings, power slider, STOP).

### Monitor page (`/monitor.html`)

Always-on graphical monitoring page, designed for a wall-mounted screen. Not a full control interface.

- **Background**: apparatus schematic image with sensor data chips overlaid at physical positions.
- **Sensor chips**: all 13 sensor readings with warn/danger colour coding matching thresholds from `/state`.
- **Drag-and-drop layout**: edit mode lets the operator drag each sensor chip to its physical position on the image. Positions persist in browser `localStorage`.
- **Adaptive polling**: 3 s normally; backs off to 10 s when `interactiveActive` is `true` (an interactive operator is active on `web_ui.html`). Poll-rate badge in header shows current interval.
- **Limited controls**: Master Power slider (debounced, 3 s lock after release) and STOP button (visible only when `isRunning` is `true`). No START, no mode select, no threshold editing.
- **Client identification**: fetches `/state?client=monitor` — does NOT update `interactiveActive` on the server.

### Multi-client design

The server tracks the last time a non-monitor client fetched `/state` or hit `/api/ping`, exposed as `interactiveActive` (boolean) in every `/state` response. The monitor page uses this to self-throttle. The 3 available TLS sockets are thus never all held simultaneously by monitor polls.

---

## 5. HTTP API

Server runs on **port 443 (HTTPS only)** via `esp_https_server`. Port 80 is not open.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | `web_ui.html` from LittleFS (fallback: inline page) |
| `GET` | `/monitor.html` | `monitor.html` from LittleFS |
| `GET` | /Background.png | Apparatus background image from LittleFS, 24 h cache |
| `POST` | `/` | Plain-text command (`Content-Type: text/plain`) |
| `GET` | `/state` | Full JSON state + labels + catalog |
| `GET` | `/api/ping` | Interactive keepalive — updates `interactiveActive` timestamp |
| `GET` | `/Barrel_Big.png` | LittleFS asset, 24 h cache |
| `GET` | `/favicon.ico` | LittleFS asset, 24 h cache |
| `POST` | `/api/flow_reset` | Reset `totalVolumeLiters` to 0 |
| `POST` | `/api/sensor_scan` | Rescan 1-Wire bus, return ROM list |
| `POST` | `/api/update_cert` | Push new TLS cert+key JSON to NVS, reboot |
| `POST` | `/ota` | Flash firmware binary, reboot |

### `/state` — selected fields

```json
{
  "fw": "3.6.1",
  "isRunning": false,
  "processMode": 1,
  "masterPower": 65.0,
  "interactiveActive": true,
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
  "threshDist": { "tempWarn": [80,80,80], "tempDanger": [92,92,92], "pressWarn": 0.06, "pressDanger": 0.08 },
  "threshRect": { "tempWarn": [78,78,78], "tempDanger": [88,88,88], "pressWarn": 0.06, "pressDanger": 0.08 },
  "safetyTempMaxC": 95.0,
  "safetyPresMaxBar": 0.09,
  "safetyTripped": false,
  "safetyMessage": "",
  "tempOfflineThresh": -20.0,
  "pressureOfflineThresh": -999.0,
  "flowOfflineThresh": -999.0
}
```

Offline sentinels: `TEMP_OFFLINE_THRESH = -20.0`, `SENSOR_OFFLINE = -999.0`.  
Pressure is displayed in kPa in both UIs; stored internally in bar.

---

## 6. Command reference

All commands are plain UTF-8 strings sent via `POST /` with `Content-Type: text/plain`, or issued from the LVGL UI via `handleCommand()`.

| Command | Description |
|---|---|
| `MODE:0` | Switch to Off — stops run, resets power, clears safety latch |
| `MODE:1` | Select Distillation (SSR4–5) |
| `MODE:2` | Select Rectification (SSR1–3) |
| `MASTER:NN.N` | Set master power 0–100 % — applies to SSRs immediately |
| `START` | Start process (requires mode ≠ 0, masterPower > 0, no safety trip) |
| `STOP` | Stop process, reset mode to 0, clear safety, zero masterPower |
| `TMAX:NN.N` | Set `safetyTempMaxC` absolutely or ±relatively |
| `TMAX:N:SET:val` | Set per-sensor danger threshold for active mode (N = 1–3) |
| `THRESH:D/R:TW/TD:N:val` | Set temp warn/danger threshold for sensor N (0-based) |
| `THRESH:D/R:PW/PD:val` | Set pressure warn/danger threshold |
| `VALVE:N:OPENCFG:sId:op:val` | Configure valve N open condition |
| `VALVE:N:CLOSECFG:sId:op:val` | Configure valve N close condition |
| `SENSOR:MAP:N:ROMHEX` | Assign DS18B20 ROM to sensor slot N |
| `WIFI:SET:ssid:pass` | Update WiFi credentials and reconnect (percent-encode `:`) |

**Valve operators:** `GT` `LT` `GTE` `LTE` `EQ` `NONE`

---

## 7. Valve subsystem

`valveEvaluateAll()` runs every 500 ms regardless of `isRunning` or `processMode`. Offline sensors never trigger conditions. Close takes priority over open when both conditions fire simultaneously.

### Sensor catalog

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

## 8. NVS persistence

### `"distill"` namespace

| Key | Type | Contents |
|---|---|---|
| `lastValid` | uint8 | 1 = namespace contains valid saved state |
| `pmode` | int | processMode |
| `mPwr` | float | masterPower |
| `wasRunning` | bool | was running at last save |
| `lastTankC` | float | kettleTemp at last save |
| `tempMax` | float | safetyTempMaxC |
| `presMax` | float | safetyPresMaxBar |
| `tw_d0`–`tw_d2` | float | Distillation temp warn |
| `td_d0`–`td_d2` | float | Distillation temp danger |
| `tw_r0`–`tw_r2` | float | Rectification temp warn |
| `td_r0`–`td_r2` | float | Rectification temp danger |
| `pw_d`, `pd_d` | float | Distillation pressure warn/danger |
| `pw_r`, `pd_r` | float | Rectification pressure warn/danger |
| `rom0`–`rom7` | bytes[8] | DS18B20 ROM per slot |
| `v0os`–`v4os` | uint8 | Valve open sensor ID |
| `v0oo`–`v4oo` | uint8 | Valve open operator |
| `v0ov`–`v4ov` | float | Valve open threshold |
| `v0cs`–`v4cs` | uint8 | Valve close sensor ID |
| `v0co`–`v4co` | uint8 | Valve close operator |
| `v0cv`–`v4cv` | float | Valve close threshold |
| `wifiSsid` | String | WiFi SSID |
| `wifiPass` | String | WiFi password |

`valveOpen[]` and live sensor readings are **not** persisted — valves always start closed on boot.

### `"certs"` namespace

| Key | Type | Contents |
|---|---|---|
| `cert` | bytes | Server certificate PEM (fullchain) |
| `key` | bytes | RSA private key PEM |

Falls back to compiled-in `certs.h` when NVS is empty (first boot or after NVS erase).

---

## 9. File reference

| File | Role |
|---|---|
| `DistillController.ino` | Boot sequence, WiFi, mbedTLS PSRAM redirect, tasks |
| `config.h` | Pin assignments, timing constants, default thresholds |
| `ui_strings.h` | All display strings for both LVGL and web UI |
| `state.h` / `state.cpp` | AppState, mutex helpers, NVS load/save, sensor catalog |
| `sensors.h` / `sensors.cpp` | DS18B20, pressure, flow, level drivers |
| `expander.h` / `expander.cpp` | PCF8574 drivers, Wire mutex, `flow2PollTask` |
| `ssr.h` / `ssr.cpp` | Software time-proportional SSR driver (esp_timer, 5 outputs) |
| `control.h` / `control.cpp` | Safety check, SSR apply, valve evaluation, command parser |
| `http_server.h` / `http_server.cpp` | HTTPS routes, `/state` serialiser, interactive tracking, cert NVS |
| `web_ui.h` / `web_ui.cpp` | LittleFS file streaming for all web assets |
| `ddns.h` / `ddns.cpp` | GoDaddy DNS A-record updater (FreeRTOS task, Core 0) |
| `ota.h` / `ota.cpp` | Pull OTA from GitHub Releases (30 min check, stopped only) |
| `certs.h` | Compiled-in fallback TLS cert |
| `ui_lvgl.h` / `ui_lvgl.cpp` | LVGL panel layout, widget updates, screen transitions |
| `data/web_ui.html` | Interactive web UI — served from LittleFS at `/` |
| `data/monitor.html` | Always-on monitor page — served from LittleFS at `/monitor.html` |
| `data/Barrel_Big.png` | Barrel graphic — served from LittleFS, also loaded by LVGL |
| `data/favicon.ico` | Browser favicon |
| `version.json` | Repo root — updated by `bump-push-release.sh` on each release |
| `renew-winery-cert.sh` | macOS cron script: certbot renewal + cert push to device |
| `bump-push-release.sh` | Version bump, git commit, GitHub release, OTA binary build |
| `data/Background.png` | Apparatus background image for monitor page — served from LittleFS at `/Background.png` |

---

## 10. Building

### Requirements

- Arduino IDE 2.x or `arduino-cli`
- ESP32 Arduino core **v3.x** (platform v3.3.7)
- Libraries: `LVGL` v9, `Arduino_GFX_Library`, `JC3248W535EN-Touch-LCD`, `OneWire`, `DallasTemperature`, `ArduinoJson` v7

### Board settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16 MB |
| Partition Scheme | `app3M_fat9M_16MB` |
| PSRAM | OPI PSRAM |
| Upload Speed | 115200 |
| CDC On Boot | Enabled |
| FQBN | `esp32:esp32:esp32s3:UploadSpeed=115200,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi` |

### First boot

1. Flash firmware.
2. Upload LittleFS image (see below) — required for both web UIs.
3. Open `https://<device-IP>`. Accept the cert warning or install `ca.crt`.
4. Go to **Control → Sensors**, click **SCAN BUS**, assign DS18B20 ROM addresses.
5. Go to **Control → Valves**, configure open/close rules.
6. Select mode, set Master Power, press START.
7. Open `https://<device-IP>/monitor.html` on the wall-mounted screen.

### Uploading the LittleFS image

Contains `web_ui.html`, `monitor.html`, `Barrel_Big.png`, `Background.png`, `favicon.ico`. Re-flash whenever any file in `data/` changes — no firmware reflash needed.

**Arduino IDE 2.x** — install the LittleFS Upload plugin, then **Sketch → Upload Sketch Data**.

**arduino-cli / mklittlefs:**
```bash
mklittlefs -c data/ -b 4096 -p 256 -s 0x180000 littlefs.bin
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
           write_flash 0x670000 littlefs.bin
```

### OTA push

```bash
curl --insecure -X POST https://winery.baghamut.com/ota \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@DistillController.ino.bin" \
  --max-time 120
```

---

## 11. TLS certificate management

### Local network (self-signed CA)

```bash
# CA
openssl genrsa -out ca.key 4096
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
  -subj "/CN=DistillController CA/O=Winery/C=CZ"

# Device cert
openssl genrsa -out device.key 2048
openssl req -new -key device.key -out device.csr \
  -subj "/CN=distillcontroller.local/O=Winery/C=CZ"

cat > san.ext << 'EOF'
[SAN]
subjectAltName = DNS:distillcontroller.local, IP:192.168.1.XXX
EOF

openssl x509 -req -days 825 \
  -in device.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out device.crt -extfile san.ext -extensions SAN
```

Install `ca.crt` on each client:

| Platform | Method |
|---|---|
| macOS | `sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain ca.crt` |
| Windows | `Import-Certificate -FilePath ca.crt -CertStoreLocation Cert:\LocalMachine\Root` |
| iOS | AirDrop → Settings → VPN & Device Management → trust |
| Android | Settings → Security → Install from storage → CA Certificate |

### Public domain (Let's Encrypt)

Push renewed cert to device without firmware rebuild:

```bash
LIVE_DIR="$HOME/.certbot/config/live/winery.baghamut.com"
PAYLOAD=$(python3 -c "
import json
cert = open('$LIVE_DIR/fullchain.pem').read()
key  = open('$LIVE_DIR/privkey.pem').read()
print(json.dumps({'cert': cert, 'key': key}))
")
curl --insecure -X POST https://winery.baghamut.com/api/update_cert \
  -H "Content-Type: application/json" \
  -d "$PAYLOAD"
```

Automated renewal cron (every 2 months):
```
0 9 1 */2 * /Users/baghamut/renew-winery-cert.sh >> /tmp/certbot-renew.log 2>&1
```

---

## 12. DDNS

`ddnsTask` runs on Core 0 at priority 1. Updates the GoDaddy `winery` A record when the device IP changes, rechecks every 5 minutes. Only changed IPs trigger an API call. 15 s settle delay on boot to avoid updating with a stale DHCP lease.

Credentials configured in `config.h` — keep out of repository.

---

## 13. Pull OTA

`ota_pull` task runs on Core 0 at priority 1. Checks `version.json` from the GitHub repo every 30 minutes. If a newer version is found **and the process is stopped and no safety is tripped**, downloads and flashes the binary, then reboots.

```json
{
  "version": "3.6.1",
  "url": "https://github.com/baghamut/Winery-Controller/releases/download/v3.6.1/DistillController-v3.6.1.bin"
}
```

First check fires 60 s after boot. Updated automatically by `bump-push-release.sh` on each release.

---

## 14. Auto-restore after power loss

On boot, after a 1.2 s sensor settle delay, the firmware checks:

- `wasRunning` was `true` at last save
- `processMode` is 1 or 2
- `safetyTripped` is `false`
- `masterPower` > 0
- Kettle temperature drop since last save ≤ `AUTO_RESTORE_MAX_TEMP_DROP_C` (5 °C)

If all conditions are met the process resumes automatically at the saved `masterPower`. Boot log prints `RESUME` or `SKIP` with the reason.

---

## 15. Known pending items

| ID | Severity | Description |
|---|---|---|
| H4 | Medium | `sensorsScanBus` blocking loop — should yield during long scan |
| M7 | Low | Hardcoded flow unit label in one location |
| L1 | Low | Debug touch dot visible in production build |
| L4 | Low | `expanderWriteBit` TOCTOU on shadow state |
