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

### Web UI (Tailwind CSS dark theme, served from LittleFS)

Served from `GET /` via LittleFS (`/data/web_ui.html`). Static assets (`barrel.png`, `favicon.ico`) are also served from LittleFS with a 24-hour cache header. A minimal inline fallback page is shown if the filesystem image has not been uploaded.

Commands are sent as plain-text `POST /` with `Content-Type: text/plain` and the command string as the raw body. The web UI polls `/state` every 3 seconds. A connectivity dot next to the IP address turns green when the last poll succeeded and red on failure. Post-command refresh fires after 250 ms for immediate feedback.

**Screen 0 — Mode Select** — two clickable cards.

**Screen 1 — Control** — Master Power slider, sensor threshold rows, BACK / START buttons. Header buttons: **Sensors** (opens sensor mapper modal) and **Valves Control** (switches to Screen 3).

**Screen 2 — Monitor** — live sensor readings with warn/danger colour coding, Master Power slider, STOP button.

**Screen 3 — Valves Control** — per-valve cards showing current open/closed state badge (updated every 3 s poll), sensor dropdown, operator dropdown, threshold value input, and SET button. Dropdowns are never rebuilt during polling — only the state badge is updated, so open dropdowns stay stable.

**Sensor Mapper Modal** — SCAN BUS button calls `/api/sensor_scan`. Each of the 8 DS18B20 slots shows its current ROM hex, a dropdown of detected ROMs, and a SET button sending `SENSOR:MAP:N:ROMHEX`.

**WiFi Modal** — SSID / password fields, opened by clicking the IP address in the header.

Pressure is displayed in kPa in both UIs. Internal storage, NVS keys, and command formats remain in bar; the UI layer applies the `×100` conversion.

All display labels come from the `/state` JSON so that changing a string in `ui_strings.h` propagates to both UIs automatically.

---

## 9. NVS Persistence

### `"distill"` namespace — process state

All persistent process data is stored under the `"distill"` NVS namespace.

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

### `"certs"` namespace — TLS certificate

TLS certificate and private key are stored separately in the `"certs"` NVS namespace so that cert renewal never requires a firmware rebuild.

| Key | Type | Contents |
|---|---|---|
| `cert` | bytes | Server certificate PEM (fullchain) |
| `key` | bytes | RSA private key PEM |

On first boot (or after NVS erase) these keys are absent and the server falls back to the compiled-in cert in `certs.h`. After the first `POST /api/update_cert`, subsequent boots always load from NVS. See section 15 for the renewal workflow.

---

## 10. HTTP API

The web server runs on **port 443 (HTTPS only)** via `esp_https_server` (mbedTLS). Port 80 is not open.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Serves `web_ui.html` from LittleFS (fallback: inline page) |
| `POST` | `/` | Send a plain-text command (`Content-Type: text/plain`, raw body) |
| `GET` | `/state` | Full JSON state + labels + catalog |
| `GET` | `/Barrel_Big.png` | Barrel graphic from LittleFS (24 h cache) |
| `GET` | `/favicon.ico` | Favicon from LittleFS (24 h cache) |
| `POST` | `/api/flow_reset` | Reset `totalVolumeLiters` to 0 |
| `POST` | `/api/sensor_scan` | Rescan 1-Wire bus, return ROM list |
| `POST` | `/api/update_cert` | Push new TLS cert+key to NVS, reboot |
| `POST` | `/ota` | Flash new firmware binary, reboot |

### `/api/update_cert` request body

```json
{ "cert": "<fullchain PEM string>", "key": "<privkey PEM string>" }
```

The cert is validated with mbedTLS before saving. On success the device responds `Cert saved. Rebooting.` (HTTP 200) and reboots within 1 second. Use `curl --insecure` when pushing — the old cert being replaced may be near expiry.

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

All commands are plain UTF-8 strings sent via `POST /` with `Content-Type: text/plain` and the command as the raw request body, or issued directly from the LVGL UI via `handleCommand()`. Both UIs use identical command strings.

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
| `ddnsTask` | 0 | 1 | 4096 | Updates GoDaddy DNS A record when IP changes (every 5 min) |
| `ota_pull` | 0 | 1 | 6144 | Checks GitHub version.json every 30 min, flashes if newer and stopped |
| `lvglTask` | 1 | 1 | 8192 | LVGL handler; calls `lv_timer_handler()` |
| `esp_https_server` | — | — | 8192 | Internal task created by `httpd_ssl_start`; handles all HTTPS requests |
| Arduino `loop()` | 1 | — | — | Deferred WiFi reconnect, LVGL refresh trigger, WiFi IP sync |

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

### HTTPS Server

`esp_https_server` (mbedTLS) replaces `WebServer.h`. All mbedTLS allocations (SSL record buffers, handshake state, certificate parsing — ~40 KB per session) are redirected to PSRAM via `mbedtls_platform_set_calloc_free()`, keeping internal DRAM free for lwIP and FreeRTOS. TLS sessions are capped at 3 concurrent connections (`max_open_sockets = 3`) with LRU eviction enabled. Request header buffer is 2048 bytes. The server runs on its own internal FreeRTOS task; `httpServerHandle()` in `loop()` is a retained no-op.

---

## 13. File Reference

| File | Role |
|---|---|
| `DistillController.ino` | Entry point: boot sequence, WiFi, mDNS, LVGL task, OTA |
| `config.h` | All pin assignments, timing constants, default thresholds |
| `ui_strings.h` | Centralised string constants for both LVGL and web UI |
| `state.h` / `state.cpp` | `AppState`, mutex helpers, NVS load/save, sensor catalog |
| `sensors.h` / `sensors.cpp` | DS18B20 (ROM-addressed), pressure, flow, level drivers |
| `expander.h` / `expander.cpp` | PCF8574 drivers (both expanders), Wire mutex, flow2PollTask |
| `ssr.h` / `ssr.cpp` | Software time-proportional SSR driver (esp_timer, 5 outputs) |
| `control.h` / `control.cpp` | Safety check, SSR apply, valve evaluation, command parser |
| `http_server.h` / `http_server.cpp` | HTTPS routes, `/state` serialiser, cert NVS load/save, OTA handler |
| `web_ui.h` / `web_ui.cpp` | LittleFS file streaming for web UI HTML and static assets |
| `ddns.h` / `ddns.cpp` | GoDaddy DNS A-record updater (FreeRTOS task, Core 0) |
| `certs.h` | Compiled-in fallback TLS cert — used only when NVS cert is absent |
| `ui_lvgl.h` / `ui_lvgl.cpp` | LVGL panel layout, widget updates, screen transitions |
| `data/web_ui.html` | Web UI — served from LittleFS at runtime |
| `data/Barrel_Big.png` | Barrel graphic — served from LittleFS, also loaded by LVGL lodepng |
| `data/favicon.ico` | Browser favicon — served from LittleFS |
| `renew-winery-cert.sh` | Mac cron script: certbot renewal + cert push to device via HTTPS |

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
  - `ArduinoJson` v7
  - `Preferences` (bundled with ESP32 core)
  - `LittleFS` (bundled with ESP32 core)
  - `HTTPUpdate` (bundled with ESP32 core)
  - `ESPmDNS` (bundled with ESP32 core)

### Board Settings (Arduino IDE)

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16 MB |
| Partition Scheme | `app3M_fat9M_16MB` |
| PSRAM | OPI PSRAM |
| Upload Speed | 115200 |
| CDC On Boot | Enabled |

### First Boot

1. Flash firmware.
2. Connect to serial monitor (115200 baud) to confirm boot sequence.
3. Upload the LittleFS image (see below) — required for the web UI to load.
4. Open `https://<device-IP>` or `https://distillcontroller.local`. Accept the browser cert warning on first visit (before CA is installed), or install `ca.crt` — see section 15.
5. Go to **Control → Sensors**, click **SCAN BUS** and assign DS18B20 ROM addresses to sensor slots.
6. Go to **Control → Valves Control** and configure open/close rules for each valve.
7. Select a process mode, set Master Power, and press START.

### Uploading the Filesystem Image

The LittleFS image contains `web_ui.html`, `Barrel_Big.png`, and `favicon.ico`. Flash it once after firmware, and again whenever any file in `data/` changes — no firmware reflash needed.

**Arduino IDE 2.x** — install the *LittleFS Upload* plugin, then use **Sketch → Upload Sketch Data**.

**arduino-cli / mklittlefs**
```bash
mklittlefs -c data/ -b 4096 -p 256 -s 0x180000 littlefs.bin
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
           write_flash 0x670000 littlefs.bin
```
Partition offset `0x670000` and size `0x180000` match `partitions.csv`.

### OTA Firmware Update

Push a compiled binary directly to the device:
```bash
curl --insecure -X POST https://winery.baghamut.com/ota \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@DistillController.ino.bin" \
  --max-time 120
```
The device reboots automatically on success.

---

## 15. TLS Certificate Management

The HTTPS server requires a TLS certificate. Two modes of operation exist depending on deployment.

### Local network (LAN only)

Generate a local CA and self-signed device cert. Install `ca.crt` once per client machine for a trusted green padlock with no browser warning.

```bash
# Generate CA
openssl genrsa -out ca.key 4096
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
  -subj "/CN=DistillController CA/O=Winery/C=CZ"

# Generate device key + CSR
openssl genrsa -out device.key 2048
openssl req -new -key device.key -out device.csr \
  -subj "/CN=distillcontroller.local/O=Winery/C=CZ"

# SAN extension — add all hostnames and IPs the device will be reached by
cat > san.ext << 'EOF'
[SAN]
subjectAltName = DNS:distillcontroller.local, IP:192.168.1.XXX
EOF

# Sign cert (825 days = Apple/Chrome max)
openssl x509 -req -days 825 \
  -in device.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out device.crt -extfile san.ext -extensions SAN

openssl verify -CAfile ca.crt device.crt
```

Paste `device.crt` and `device.key` into `certs.h` as the compiled-in fallback. Install `ca.crt` on each client machine:

| Platform | Command / Steps |
|---|---|
| macOS | `sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain ca.crt` |
| Windows (PowerShell admin) | `Import-Certificate -FilePath ca.crt -CertStoreLocation Cert:\LocalMachine\Root` |
| Linux + Chrome | `certutil -d sql:$HOME/.pki/nssdb -A -t "C,," -n "DistillController CA" -i ca.crt` |
| Linux + Firefox | Settings → Privacy → Certificates → Import → trust for websites |
| iOS | AirDrop `ca.crt` → Settings → General → VPN & Device Management → trust |
| Android | Settings → Security → Install from storage → CA Certificate |

### Remote access with public domain (winery.baghamut.com)

Uses Let's Encrypt via GoDaddy DNS challenge. No CA installation required on client machines.

**One-time setup:**
```bash
python3 -m venv ~/.certbot-venv
~/.certbot-venv/bin/pip install certbot certbot-dns-godaddy

mkdir -p ~/.certbot
cat > ~/.certbot/godaddy.ini << 'EOF'
dns_godaddy_secret = YOUR_API_SECRET
dns_godaddy_key    = YOUR_API_KEY
EOF
chmod 600 ~/.certbot/godaddy.ini
```

**First issuance:**
```bash
~/.certbot-venv/bin/certbot certonly \
  --authenticator dns-godaddy \
  --dns-godaddy-credentials ~/.certbot/godaddy.ini \
  -d winery.baghamut.com \
  --config-dir ~/.certbot/config \
  --work-dir   ~/.certbot/work \
  --logs-dir   ~/.certbot/logs
```

**Push cert to device (no rebuild needed):**
```bash
LIVE_DIR="$HOME/.certbot/config/live/winery.baghamut.com"
PAYLOAD=$($HOME/.certbot-venv/bin/python3 -c "
import json
cert = open('$LIVE_DIR/fullchain.pem').read()
key  = open('$LIVE_DIR/privkey.pem').read()
print(json.dumps({'cert': cert, 'key': key}))
")
curl --insecure -X POST https://winery.baghamut.com/api/update_cert \
  -H "Content-Type: application/json" \
  -d "$PAYLOAD" -w "\nHTTP %{http_code}\n"
```

Device responds `Cert saved. Rebooting.` and reboots within 1 second. On next boot serial shows `[HTTPS] Cert loaded from NVS`.

**Automated renewal (cron):**

`renew-winery-cert.sh` handles certbot renewal and device push in one step. Add to crontab to run every 2 months (Let's Encrypt certs are valid 90 days):
```
0 9 1 */2 * /Users/baghamut/renew-winery-cert.sh >> /tmp/certbot-renew.log 2>&1
```

The device logs the cert expiry date at every boot:
```
[HTTPS] Cert expires: 2026-07-05
```

**Important:** `certs.h`, `ca.key`, `device.key`, and `~/.certbot/godaddy.ini` must never be committed to the repository. Add them all to `.gitignore`.

---

## 16. DDNS (Dynamic DNS)

`ddnsTask` runs on Core 0 at priority 1. It calls the GoDaddy REST API to update the `winery` A record whenever the device's IP changes, and rechecks every 5 minutes. Only changed IPs trigger an API call.

The DNS A record for `winery.baghamut.com` must be created manually in GoDaddy DNS before the updater can patch it (the API updates existing records; it does not create them).

DDNS credentials are defined in `config.h`:
```cpp
#define DDNS_HOSTNAME   "winery"
#define DDNS_DOMAIN     "baghamut.com"
#define DDNS_API_KEY    "your_key"
#define DDNS_API_SECRET "your_secret"
#define DDNS_UPDATE_INTERVAL_MS  (5 * 60 * 1000UL)
```

Keep `DDNS_API_KEY` and `DDNS_API_SECRET` out of the repository.

---

## 17. Pull OTA (GitHub Releases)

`ota_pull` task runs on Core 0 at priority 1. It fetches `version.json` from the GitHub repo every 30 minutes and compares the `version` field against the running `FW_VERSION`. If a newer version is found **and the process is stopped and no safety is tripped**, it downloads the binary from the `url` field and flashes it via the Arduino `Update` API, then reboots.

`version.json` lives in the repo root and is updated automatically by `bump-push-release.sh` on every release:
```json
{
  "version": "3.5.2",
  "url": "https://github.com/baghamut/Winery-Controller/releases/download/v3.5.2/DistillController.ino.bin"
}
```

The first check fires 60 seconds after boot. GitHub HTTPS uses `setInsecure()` — acceptable because the binary CRC is verified by the `Update` library and the source (GitHub releases) is trusted by construction.

OTA configuration in `ota.h` / `config.h`:
```cpp
#define OTA_VERSION_URL          "https://raw.githubusercontent.com/baghamut/Winery-Controller/main/version.json"
#define OTA_CHECK_INTERVAL_MS    (30 * 60 * 1000UL)   // 30 minutes
#define OTA_FIRST_CHECK_DELAY_MS (60 * 1000UL)        // 1 minute after boot
```
