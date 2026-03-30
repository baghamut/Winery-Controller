# DistillController

ESP32-S3 firmware for the **JC3248W535** board (3.5" 480×320 capacitive touch display).  
Controls a distillation column with dual-process support (Distillation / Rectification).  
Two interfaces share the same state: an on-device **LVGL touch UI** and a **browser-based web UI** hosted on GitHub Pages.

---

## Architecture

```
┌─────────────────────────────────┐
│         AppState (g_state)      │  ← single source of truth, mutex-protected
│  processMode / isRunning / SSRs │
│  temps / pressure / level / flow│
│  threshDist / threshRect        │
└──────┬──────────────────┬───────┘
       │                  │
       ▼                  ▼
 LVGL Touch UI      HTTP Server (port 80)
 (ui_lvgl.cpp)      (http_server.cpp)
                         │
               GET /state → JSON
               POST /     → command
               GET /      → redirect to GitHub Pages UI
                         │
                         ▼
          https://baghamut.github.io/Winery-Controller/ui.html
          (full HTML/JS, fetches /state cross-origin via CORS)
```

### Web UI flow

1. User opens `http://<device-ip>/` in a browser.
2. Device serves a tiny redirect page that sends the browser to GitHub Pages, passing the device IP in the URL hash (`ui.html#192.168.x.x`).
3. The hosted `ui.html` reads the hash, prefixes all API calls with `http://<device-ip>/`, and renders the full UI.
4. CORS headers (`Access-Control-Allow-Origin: *`) on `/state` and `POST /` allow the cross-origin fetch.

**Benefit:** the HTML/JS UI can be updated by pushing to GitHub — no firmware reflash required.

---

## Pin Mapping

| Function                  | GPIO | Notes                                            |
|---------------------------|------|--------------------------------------------------|
| **SSR 1** (Distiller)     | IO5  | LEDC, 10 Hz time-proportional PWM               |
| **SSR 2** (Distiller)     | IO6  | LEDC                                            |
| **SSR 3** (Distiller)     | IO7  | LEDC                                            |
| **SSR 4** (Rectifier)     | IO15 | LEDC                                            |
| **SSR 5** (Rectifier)     | IO16 | LEDC                                            |
| **DS18B20 1-Wire bus**    | IO18 | 4.7 kΩ pull-up to 3.3 V; supports 1–3 sensors   |
| **Pressure sensor** (ADC) | IO17 | 0–3.3 V; add 10 kΩ pull-down to GND             |
| **Water level** (digital) | IO14 | INPUT_PULLUP; LOW = level OK                    |
| **Water flow** (Hall)     | IO46 | Rising-edge interrupt                           |

> **Display/touch pins (board-reserved):** IO1 (backlight), IO4 (touch SDA), IO8 (touch SCL), IO3 (touch INT).

---

## Project File Structure

```
DistillController/
├── DistillController.ino   Main sketch: setup(), loop(), LVGL + Wi-Fi + NVS
├── config.h                All pins, thresholds, timing, Wi-Fi defaults, NVS keys
├── ui_strings.h            All user-visible strings – single source for LVGL and web
├── state.h / state.cpp     AppState, mutex, NVS persistence
├── sensors.h / sensors.cpp DS18B20, pressure, level, flow → g_state
├── ssr.h / ssr.cpp         LEDC SSR driver
├── control.h / control.cpp Control loop, safety, command dispatcher
├── http_server.h / .cpp    WebServer: /state JSON, POST commands, CORS
├── web_ui.h / web_ui.cpp   Bootstrap redirect page + CORS helper
└── ui_lvgl.h / ui_lvgl.cpp LVGL v9 UI: Mode, Control, Monitor, WiFi Setup
```

---

## Command Reference (`handleCommand`)

All commands are plain text, sent by the LVGL UI or as HTTP POST `/`.

| Command | Effect |
|---------|--------|
| `MODE:0` | Idle – clears safety latch |
| `MODE:1` | Distillation (SSR1–3) |
| `MODE:2` | Rectification (SSR4–5) |
| `SSR:N:ON` | Enable SSR N (1–5) |
| `SSR:N:OFF` | Disable SSR N |
| `SSR:N:PWR:XX` | Set SSR N power 0–100 % |
| `START` | Begin process (requires mode + active SSR) |
| `STOP` | Stop process, clear safety, reset SSRs |
| `TMAX:N:SET:XX.X` | Set danger threshold for sensor N (1–3) for active mode |
| `TMAX:XX.X` | Set `safetyTempMaxC` absolute (legacy) |
| `TMAX:+X` / `TMAX:-X` | Adjust `safetyTempMaxC` relative (legacy) |
| `THRESH:D/R:TW:N:XX` | Set temp warn for sensor N in Dist/Rect |
| `THRESH:D/R:TD:N:XX` | Set temp danger for sensor N in Dist/Rect |
| `THRESH:D/R:PW:XX` | Set pressure warn in Dist/Rect |
| `THRESH:D/R:PD:XX` | Set pressure danger in Dist/Rect |

---

## LVGL Touch UI

Screen selection is driven by `processMode` and `isRunning`:

| Condition | Screen shown |
|-----------|-------------|
| `processMode == 0` | Mode screen |
| `processMode != 0 && !isRunning` | Control screen |
| `processMode != 0 && isRunning` | Monitor screen |
| Tap IP in header | WiFi Setup screen |
| Tap sensor limit button | Tmax editor panel |

### Header bar (all screens)

Four items distributed evenly with `LV_FLEX_ALIGN_SPACE_EVENLY`:

- **Status** – `STOPPED` / `RUNNING` / `SAFETY TRIP`
- **T1** – Room sensor reading (green) or `--` (red when offline)
- **Max** – Highest current reading across all three sensors (or safety message)
- **IP** – Device IP (green when connected, red when `0.0.0.0`); tap to open WiFi Setup

### Mode screen

Two large buttons: **DISTILLATION** and **RECTIFICATION**.  
Active mode is highlighted with the accent colour.

### Control screen

- SSR rows (label / power slider / % / ON-OFF switch).  
  Only the 3 rows relevant to the selected mode are shown.
- **Limits row** – three buttons, one per sensor, each showing `SensorName: XX.X°C`.  
  Tapping a button opens the **Tmax editor panel**.
- **Tmax editor panel** (full-screen overlay):  
  `-5` / `-1` / `[text area]` / `+1` / `+5` controls + BACK / SAVE.  
  SAVE sends `TMAX:N:SET:XX.X` and writes to `threshDist` or `threshRect` depending on active mode.
- **BACK** → Mode screen. **START** → Monitor screen (requires at least one SSR on with >0 power).

### Monitor screen

Rows: T1, T2, T3 (3-colour threshold colouring), Pressure, Level, Flow, Total, SSRs.  
**STOP** → sends `STOP` + `MODE:0`, returns to Mode screen.

### WiFi Setup screen

Edit SSID and password via on-screen keyboard. **SAVE** stores to NVS and reconnects.

---

## Web UI (GitHub Pages)

The hosted UI at `https://baghamut.github.io/Winery-Controller/ui.html` mirrors the LVGL UI exactly:

- Same three screens: Mode / Control / Monitor
- Same sensor limit buttons and inline Tmax editor
- Same threshold card (Distillation / Rectification tabs, Warn + Danger per sensor + pressure)
- Same command set (`TMAX:N:SET:`, `THRESH:D/R:...`)
- Auto-refreshes every 3 seconds via `GET /state`

To update the web UI: push a new `ui.html` to the `main` branch — no firmware change needed.

---

## `/state` JSON Response

```json
{
  "fw": "1.0.0",
  "isRunning": false,
  "processMode": 1,
  "ssrOn":    [true, false, false, false, false],
  "ssrPower": [75.0, 0.0, 0.0, 0.0, 0.0],
  "t1": 24.5, "t2": 78.3, "t3": 21.1,
  "pressureBar": 0.042,
  "levelHigh": true,
  "flowRateLPM": 1.23,
  "totalVolumeLiters": 0.456,
  "ip": "192.168.1.42",
  "safetyTempMaxC": 95.0,
  "safetyTripped": false,
  "safetyMessage": "",
  "threshDist": {
    "tempWarn":   [80.0, 80.0, 80.0],
    "tempDanger": [92.0, 92.0, 92.0],
    "pressWarn": 0.06, "pressDanger": 0.08
  },
  "threshRect": {
    "tempWarn":   [78.0, 78.0, 78.0],
    "tempDanger": [88.0, 88.0, 88.0],
    "pressWarn": 0.06, "pressDanger": 0.08
  }
}
```

---

## Build Instructions (Arduino IDE)

### Board support

- Add ESP32 board URL:  
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Install **esp32 by Espressif Systems** v3.x.

### Required libraries (Library Manager)

| Library | Author |
|---------|--------|
| JC3248W535EN-Touch-LCD | AudunKodehode |
| lvgl | LVGL (v9.x) |
| OneWire | Paul Stoffregen |
| DallasTemperature | Miles Burton |
| ArduinoJson | bblanchon (v6/7) |

### LVGL v9 configuration (`lv_conf.h`)

```c
#define LV_COLOR_DEPTH              16
#define LV_USE_FONT_MONTSERRAT_14   1
#define LV_FONT_DEFAULT             &lv_font_montserrat_14
#define LV_USE_LABEL   1
#define LV_USE_BTN     1
#define LV_USE_SLIDER  1
#define LV_USE_SWITCH  1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
```

Also enable Montserrat 12, 16, 16-bold, 22, 22-bold if used.

### Board settings

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| CPU Frequency | 240 MHz (WiFi) |
| Flash Size | 16 MB (128 Mb) |
| Partition Scheme | 8M with spiffs (3MB APP/1.5MB SPIFFS) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

---

## Wi-Fi Configuration

Initial credentials in `config.h`:
```c
#define WIFI_SSID_DEFAULT  "YourSSID"
#define WIFI_PASS_DEFAULT  "YourPassword"
```

Change at runtime via the **WiFi Setup** screen (tap the IP in the header).  
Credentials are stored in NVS (`wifiSsid` / `wifiPass`).

---

## NVS Keys

Namespace: `distill`

| Key | Value |
|-----|-------|
| `lastValid` | `1` when NVS has been written |
| `pmode` | Last process mode |
| `tempMax` | `safetyTempMaxC` |
| `presMax` | `safetyPresMaxBar` |
| `on0`–`on4` | SSR enabled flags |
| `pwr0`–`pwr4` | SSR power values |
| `tw_d0`–`tw_d2` | Distillation temp warn |
| `td_d0`–`td_d2` | Distillation temp danger |
| `tw_r0`–`tw_r2` | Rectification temp warn |
| `td_r0`–`td_r2` | Rectification temp danger |
| `pw_d`, `pd_d` | Distillation pressure thresholds |
| `pw_r`, `pd_r` | Rectification pressure thresholds |
| `wifiSsid` | Wi-Fi SSID |
| `wifiPass` | Wi-Fi password |

To erase all settings:
```cpp
Preferences prefs;
prefs.begin("distill", false);
prefs.clear();
prefs.end();
```

---

## OTA Updates

Set the firmware URL in `config.h`:
```c
#define OTA_FIRMWARE_URL  "https://raw.githubusercontent.com/baghamut/Winery-Controller/main/DistillController.bin"
```

Trigger via HTTP:
```bash
curl -X POST http://<device-ip>/ota
```

The device downloads and flashes the binary, then reboots automatically on success.

---

## Safety

The control loop (`CONTROL_LOOP_MS` = 500 ms) checks:

- **Over-temperature** – any sensor ≥ `safetyTempMaxC` → trip
- Trip: all SSRs off, `isRunning = false`, `safetyTripped = true`, header shows message
- Clear: send `STOP` or `MODE:0`

Per-sensor danger thresholds (`threshDist/Rect.tempDanger[]`) drive the 3-colour display colouring (green → orange → red) but do **not** independently trip the safety; only `safetyTempMaxC` does.

---

## Hardware Notes

- **Pressure sensor:** Add a 10 kΩ pull-down from IO17 to GND. Without it a disconnected pin can settle to a stable non-zero voltage and pass the online-detection heuristic.
- **DS18B20:** All sensors share one bus on IO18 with a 4.7 kΩ pull-up to 3.3 V.
- **SSRs:** Control input between GPIO and GND. LEDC PWM output is 3.3 V; most SSRs accept 3–32 V DC control.

---

## License

MIT – free to use, modify, and distribute.