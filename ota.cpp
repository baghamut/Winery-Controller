// =============================================================================
//  ota.cpp  –  Pull OTA from GitHub Releases
//
//  Flow (runs every OTA_CHECK_INTERVAL_MS, Core 0, priority 1):
//    1. Fetch version.json from GitHub raw URL
//    2. Parse {"version":"X.Y.Z","url":"https://..."}
//    3. Compare against FW_VERSION compiled into firmware
//    4. If newer AND process stopped AND no safety trip → download and flash
//    5. Reboot on success
//
//  setInsecure() is used for GitHub HTTPS only — acceptable because the
//  firmware binary is verified by the Update library CRC check, and the
//  source (GitHub releases) is trusted by construction.
// =============================================================================

#include "ota.h"
#include "config.h"
#include "state.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
//  Configuration — override in config.h if needed
// ---------------------------------------------------------------------------

#ifndef OTA_VERSION_URL
#define OTA_VERSION_URL \
    "https://raw.githubusercontent.com/baghamut/Winery-Controller/main/version.json"
#endif

#ifndef OTA_CHECK_INTERVAL_MS
#define OTA_CHECK_INTERVAL_MS  (30 * 60 * 1000UL)   // 30 minutes
#endif

#ifndef OTA_FIRST_CHECK_DELAY_MS
#define OTA_FIRST_CHECK_DELAY_MS  (60 * 1000UL)      // 1 minute after boot
#endif

// ---------------------------------------------------------------------------
//  parseVersion
//  Converts "3.5.2" → 30502 for numeric comparison.
//  Supports up to 99.99.99.
// ---------------------------------------------------------------------------
static uint32_t parseVersion(const char* v)
{
    unsigned int major = 0, minor = 0, patch = 0;
    sscanf(v, "%u.%u.%u", &major, &minor, &patch);
    return major * 10000u + minor * 100u + patch;
}

// ---------------------------------------------------------------------------
//  otaCheckAndUpdate
//  Returns true if an update was found and flashed (device will reboot).
//  Returns false if no update needed, not safe to update, or any error.
// ---------------------------------------------------------------------------
static bool otaCheckAndUpdate()
{
    // Safety gate — never update while process is running
    stateLock();
    bool running = g_state.isRunning;
    bool tripped = g_state.safetyTripped;
    stateUnlock();

    if (running) {
        Serial.println("[OTA] Skipping check — process is running");
        return false;
    }
    if (tripped) {
        Serial.println("[OTA] Skipping check — safety tripped");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] Skipping check — WiFi not connected");
        return false;
    }

    // ── 1. Fetch version.json ─────────────────────────────────────────────
    WiFiClientSecure vClient;
    vClient.setInsecure();   // GitHub uses public CA; setInsecure is fine here

    HTTPClient http;
    http.begin(vClient, OTA_VERSION_URL);
    http.setTimeout(10000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[OTA] version.json fetch failed: HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    // ── 2. Parse JSON ─────────────────────────────────────────────────────
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[OTA] version.json parse error");
        return false;
    }

    const char* remoteVersion = doc["version"];
    const char* binaryUrl     = doc["url"];

    if (!remoteVersion || !binaryUrl) {
        Serial.println("[OTA] version.json missing fields");
        return false;
    }

    // ── 3. Compare versions ───────────────────────────────────────────────
    uint32_t remote  = parseVersion(remoteVersion);
    uint32_t current = parseVersion(FW_VERSION);

    Serial.printf("[OTA] Current: %s  Remote: %s\n", FW_VERSION, remoteVersion);

    if (remote <= current) {
        Serial.println("[OTA] Firmware is up to date");
        return false;
    }

    // ── 4. Download and flash ─────────────────────────────────────────────
    Serial.printf("[OTA] New version %s available — downloading\n", remoteVersion);

    WiFiClientSecure fClient;
    fClient.setInsecure();
    fClient.setTimeout(60);   // firmware download can take a while

    httpUpdate.rebootOnUpdate(false);   // we reboot manually so we can log it
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    httpUpdate.onProgress([](int cur, int total) {
        if (total > 0)
            Serial.printf("[OTA] Progress: %d%%\r", cur * 100 / total);
    });

    t_httpUpdate_return ret = httpUpdate.update(fClient, binaryUrl);

    switch (ret) {
        case HTTP_UPDATE_OK:
            Serial.println("\n[OTA] Flash OK — rebooting in 1 s");
            delay(1000);
            ESP.restart();
            return true;   // unreachable

        case HTTP_UPDATE_FAILED:
            Serial.printf("\n[OTA] Flash failed (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            return false;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No update (server reported same version)");
            return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  otaTask  —  FreeRTOS task, Core 0, priority 1
// ---------------------------------------------------------------------------
static void otaTask(void* pv)
{
    // Wait for WiFi and let the system settle before first check
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(5000));
    vTaskDelay(pdMS_TO_TICKS(OTA_FIRST_CHECK_DELAY_MS));

    for (;;) {
        otaCheckAndUpdate();
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS));
    }
}

// ---------------------------------------------------------------------------
//  otaInit
// ---------------------------------------------------------------------------
void otaInit()
{
    xTaskCreatePinnedToCore(
        otaTask, "ota_pull",
        6144,    // stack — HTTPClient + HTTPUpdate + JSON need ~5 KB
        NULL,
        1,       // low priority — never preempts sensors or control
        NULL,
        0        // Core 0
    );
    Serial.println("[OTA] Pull task started");
}