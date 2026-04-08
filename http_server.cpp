// =============================================================================
//  http_server.cpp  –  ESP-IDF esp_https_server handlers
//  ArduinoJson v7 API (JsonDocument, obj[key].to<T>(), arr.add<T>())
//
//  CERT LIFECYCLE
//  ──────────────
//  Certs are stored in NVS namespace "certs" (keys: "cert", "key").
//  On first boot (NVS empty) the compiled-in certs.h values are used as
//  fallback and the server starts normally.
//
//  POST /api/update_cert  accepts JSON {"cert":"<PEM>","key":"<PEM>"},
//  writes both to NVS, and reboots. After reboot the new cert is active.
//  No firmware rebuild or USB cable required for cert renewal.
//
//  MULTI-CLIENT SUPPORT
//  ────────────────────
//  GET /state?client=monitor  — treated as non-interactive (monitor page)
//  GET /state                 — treated as interactive (web_ui.html)
//  GET /api/ping              — interactive keepalive, updates s_lastInteractiveMs
//  interactiveActive field in /state JSON tells monitor page to back off polling.
// =============================================================================

#include "http_server.h"
#include "config.h"
#include "state.h"
#include "control.h"
#include "sensors.h"
#include "web_ui.h"
#include "ui_strings.h"
#include "certs.h"

#include <esp_https_server.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Preferences.h>
#include <mbedtls/x509_crt.h>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
//  Module-level state
// ---------------------------------------------------------------------------

static httpd_handle_t    s_server    = NULL;
static SemaphoreHandle_t s_jsonMutex = NULL;

static std::vector<uint8_t> s_cert;
static std::vector<uint8_t> s_key;

static const char* NVS_CERT_NS  = "certs";
static const char* NVS_KEY_CERT = "cert";
static const char* NVS_KEY_KEY  = "key";

// ---------------------------------------------------------------------------
//  Interactive client tracking
//  Updated by any /state fetch that is NOT ?client=monitor, and by /api/ping.
//  s_lastInteractiveMs == 0  means "no interactive client seen yet" (boot state).
//  All accesses are from the single httpd task, so no lock needed.
// ---------------------------------------------------------------------------
static uint32_t       s_lastInteractiveMs    = 0;
static const uint32_t INTERACTIVE_TIMEOUT_MS = 90000UL;   // 3× the 30 s ping interval

// ---------------------------------------------------------------------------
//  certLoad / certSave / certLogExpiry
// ---------------------------------------------------------------------------

static void certLoad()
{
    Preferences p;
    p.begin(NVS_CERT_NS, true);
    size_t certLen = p.getBytesLength(NVS_KEY_CERT);
    size_t keyLen  = p.getBytesLength(NVS_KEY_KEY);
    if (certLen > 0 && keyLen > 0) {
        s_cert.resize(certLen);
        s_key.resize(keyLen);
        p.getBytes(NVS_KEY_CERT, s_cert.data(), certLen);
        p.getBytes(NVS_KEY_KEY,  s_key.data(),  keyLen);
        Serial.printf("[HTTPS] Cert loaded from NVS (%u + %u bytes)\n",
                      (unsigned)certLen, (unsigned)keyLen);
    } else {
        s_cert = std::vector<uint8_t>(SERVER_CERT_PEM,
                                      SERVER_CERT_PEM + SERVER_CERT_PEM_LEN);
        s_key  = std::vector<uint8_t>(SERVER_KEY_PEM,
                                      SERVER_KEY_PEM  + SERVER_KEY_PEM_LEN);
        Serial.println("[HTTPS] Cert loaded from flash (compiled-in fallback)");
    }
    p.end();
}

static bool certSave(const char* certPem, size_t certLen,
                     const char* keyPem,  size_t keyLen)
{
    Preferences p;
    if (!p.begin(NVS_CERT_NS, false)) {
        Serial.println("[HTTPS] certSave: NVS open failed");
        return false;
    }
    bool ok = (p.putBytes(NVS_KEY_CERT, certPem, certLen) == certLen) &&
              (p.putBytes(NVS_KEY_KEY,  keyPem,  keyLen)  == keyLen);
    p.end();
    if (ok) Serial.printf("[HTTPS] New cert saved to NVS (%u + %u bytes)\n",
                           (unsigned)certLen, (unsigned)keyLen);
    else    Serial.println("[HTTPS] certSave: NVS write failed");
    return ok;
}

static void certLogExpiry()
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse(&crt, s_cert.data(), s_cert.size()) == 0) {
        Serial.printf("[HTTPS] Cert expires: %04d-%02d-%02d\n",
                      crt.valid_to.year, crt.valid_to.mon, crt.valid_to.day);
    }
    mbedtls_x509_crt_free(&crt);
}

// ---------------------------------------------------------------------------
//  recvFullBody
// ---------------------------------------------------------------------------

static int recvFullBody(httpd_req_t *req, char *buf, size_t buf_len)
{
    if (req->content_len == 0)       return 0;
    if (req->content_len >= buf_len) return -1;
    size_t remaining = req->content_len;
    size_t offset    = 0;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf + offset, remaining);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0)                      return -1;
        offset    += r;
        remaining -= r;
    }
    buf[offset] = '\0';
    return (int)offset;
}

// ---------------------------------------------------------------------------
//  threshToJson
// ---------------------------------------------------------------------------

static void threshToJson(JsonObject obj, const SensorThresholds& t)
{
    JsonArray tw = obj["tempWarn"].to<JsonArray>();
    JsonArray td = obj["tempDanger"].to<JsonArray>();
    for (int i = 0; i < 3; i++) {
        tw.add(t.tempWarn[i]);
        td.add(t.tempDanger[i]);
    }
    obj["pressWarn"]   = t.pressWarn;
    obj["pressDanger"] = t.pressDanger;
}

// ---------------------------------------------------------------------------
//  buildStateJson
//  Shared by GET /state.
//  Snapshots AppState, builds full JSON into static doc, serialises to out.
//  Acquires s_jsonMutex for the build duration only.
// ---------------------------------------------------------------------------

static void buildStateJson(String& out, bool isMonitorClient)
{
    stateLock();
    AppState snap = g_state;
    stateUnlock();

    xSemaphoreTake(s_jsonMutex, portMAX_DELAY);

    static JsonDocument doc;
    doc.clear();

    doc["fw"]          = snap.fw;
    doc["isRunning"]   = snap.isRunning;
    doc["processMode"] = snap.processMode;
    doc["masterPower"] = snap.masterPower;

    doc["roomTemp"]    = snap.roomTemp;
    doc["kettleTemp"]  = snap.kettleTemp;
    doc["pillar1Temp"] = snap.pillar1Temp;
    doc["pillar2Temp"] = snap.pillar2Temp;
    doc["pillar3Temp"] = snap.pillar3Temp;
    doc["dephlegmTemp"]= snap.dephlegmTemp;
    doc["refluxTemp"]  = snap.refluxTemp;
    doc["productTemp"] = snap.productTemp;

    doc["pressureBar"]       = snap.pressureBar;
    doc["levelHigh"]         = snap.levelHigh;
    doc["flowRateLPM"]       = snap.flowRateLPM;
    doc["totalVolumeLiters"] = snap.totalVolumeLiters;
    doc["waterDephlLpm"]     = snap.waterDephlLpm;
    doc["waterCondLpm"]      = snap.waterCondLpm;

    // Interactive client tracking — tells monitor page whether to back off polling.
    // interactiveActive is false only when no non-monitor client has been seen within
    // INTERACTIVE_TIMEOUT_MS, or on first boot before any client connects.
    {
        bool active = false;
        if (s_lastInteractiveMs != 0) {
            uint32_t age = (uint32_t)millis() - s_lastInteractiveMs;
            active = (age < INTERACTIVE_TIMEOUT_MS);
        }
        doc["interactiveActive"] = active;
    }

    JsonArray sensorRoms = doc["sensorRoms"].to<JsonArray>();
    for (int i = 0; i < MAX_SENSORS; i++) {
        char hex[17];
        bool assigned = false;
        for (int b = 0; b < 8; b++) if (snap.tempSensorRom[i][b]) { assigned = true; break; }
        if (assigned) { romToHex(snap.tempSensorRom[i], hex); sensorRoms.add(hex); }
        else          { sensorRoms.add(""); }
    }

    const char* slotNames[MAX_SENSORS] = {
        STR_SENSOR_NAME1, STR_SENSOR_NAME2, STR_SENSOR_NAME3, STR_SENSOR_NAME4,
        STR_SENSOR_NAME5, STR_SENSOR_NAME6, STR_SENSOR_NAME7, STR_SENSOR_NAME8
    };
    JsonArray slotNamesArr = doc["tempSensorSlotNames"].to<JsonArray>();
    for (int i = 0; i < MAX_SENSORS; i++) slotNamesArr.add(slotNames[i]);

    doc["ip"]   = snap.ip;
    doc["ssid"] = snap.ssid;

    doc["timerSetSeconds"] = snap.timerSetSeconds;
    doc["timerElapsedMs"]  = snap.timerElapsedMs;

    doc["safetyTempMaxC"]   = snap.safetyTempMaxC;
    doc["safetyPresMaxBar"] = snap.safetyPresMaxBar;
    doc["safetyTripped"]    = snap.safetyTripped;
    doc["safetyMessage"]    = snap.safetyMessage;

    threshToJson(doc["threshDist"].to<JsonObject>(), snap.threshDist);
    threshToJson(doc["threshRect"].to<JsonObject>(), snap.threshRect);

    doc["smaxLabel1"] = STR_SMAX1;
    doc["smaxLabel2"] = STR_SMAX2;
    doc["smaxLabel3"] = STR_SMAX3;

    doc["appTitle"]        = STR_APP_TITLE;
    doc["appSubtitle"]     = STR_APP_SUBTITLE;
    doc["procDist"]        = STR_PROC_DIST;
    doc["procRect"]        = STR_PROC_RECT;

    doc["statusRunning"]   = STR_STATUS_RUNNING;
    doc["statusStopped"]   = STR_STATUS_STOPPED;
    doc["statusSafety"]    = STR_STATUS_SAFETY;

    doc["sensorName1"]     = STR_SENSOR_NAME1;
    doc["sensorName2"]     = STR_SENSOR_NAME2;
    doc["sensorName3"]     = STR_SENSOR_NAME3;
    doc["sensorName4"]     = STR_SENSOR_NAME4;
    doc["sensorName5"]     = STR_SENSOR_NAME5;
    doc["sensorName6"]     = STR_SENSOR_NAME6;
    doc["sensorName7"]     = STR_SENSOR_NAME7;
    doc["sensorName8"]     = STR_SENSOR_NAME8;
    doc["sensorName9"]     = STR_SENSOR_NAME9;

    doc["hdrT1Fmt"]        = STR_HDR_T1_FMT;
    doc["unitDegC"]        = STR_UNIT_DEGC;
    doc["unitBar"]         = STR_UNIT_BAR;
    doc["unitLpm"]         = STR_UNIT_LPM;
    doc["unitLiters"]      = STR_UNIT_LITERS;
    doc["labelOffline"]    = STR_OFFLINE;
    doc["maxPrefix"]       = STR_MAX_PREFIX;

    doc["labelPressure"]   = STR_MON_PRESSURE;
    doc["labelLevel"]      = STR_MON_LEVEL;
    doc["labelFlow"]       = STR_MON_FLOW;
    doc["labelTotal"]      = STR_MON_TOTAL;
    doc["labelLoadStatus"] = STR_MON_LOAD_STATUS;
    doc["labelLevelOk"]    = STR_LEVEL_OK;
    doc["labelLevelLow"]   = STR_LEVEL_LOW;

    doc["titleCtrl"]       = STR_TITLE_CTRL;
    doc["titleCtrlDist"]   = STR_TITLE_CTRL_DIST;
    doc["titleCtrlRect"]   = STR_TITLE_CTRL_RECT;
    doc["titleMonitor"]    = STR_TITLE_MONITOR;
    doc["titleWifiSetup"]  = STR_TITLE_WIFI_SETUP;
    doc["titleValves"]     = STR_TITLE_VALVES;

    doc["btnStart"]        = STR_BTN_START;
    doc["btnStop"]         = STR_BTN_STOP;
    doc["btnBack"]         = STR_BTN_BACK;
    doc["btnSave"]         = STR_BTN_SAVE;
    doc["btnCancel"]       = STR_BTN_CANCEL;

    doc["wifiSsidLabel"]   = STR_WIFI_SSID_LABEL;
    doc["wifiPassLabel"]   = STR_WIFI_PASS_LABEL;
    doc["wifiEmptySsid"]   = STR_WIFI_EMPTY_SSID;
    doc["wifiSavedMsg"]    = STR_WIFI_SAVED_MSG;

    doc["powerLabel"]        = STR_MASTER_POWER_LABEL;
    doc["powerHelpText"]     = STR_POWER_HELP_TEXT;
    doc["limitsDangerTitle"] = STR_LIMITS_DANGER_TITLE;

    doc["valveColValve"]    = STR_VALVE_COL_VALVE;
    doc["valveColOpen"]     = STR_VALVE_COL_OPEN;
    doc["valveColClose"]    = STR_VALVE_COL_CLOSE;
    doc["valvePlaceholder"] = STR_VALVE_PLACEHOLDER;
    doc["valveProtoMsg"]    = STR_VALVE_PROTO_MSG;

    doc["promptNewDanger"]   = STR_PROMPT_NEW_DANGER;
    doc["promptEnter03Bar"]  = STR_PROMPT_ENTER_0_3_BAR;
    doc["promptEnter0200"]   = STR_PROMPT_ENTER_0_200;

    doc["tempOfflineThresh"]     = TEMP_OFFLINE_THRESH;
    doc["pressureOfflineThresh"] = SENSOR_OFFLINE;
    doc["flowOfflineThresh"]     = SENSOR_OFFLINE;

    JsonArray ruleSensors = doc["ruleSensors"].to<JsonArray>();
    for (size_t i = 0; i < RULE_SENSOR_COUNT; i++) {
        const RuleSensorDef& rs = g_ruleSensors[i];
        JsonObject o = ruleSensors.add<JsonObject>();
        o["id"]      = rs.id;
        o["kind"]    = rs.kind;
        o["label"]   = rs.label;
        o["unit"]    = rs.unit;
        o["enabled"] = rs.enabled;
    }

    const char* valveNameList[VALVE_COUNT] = {
        STR_VALVE_NAME_0, STR_VALVE_NAME_1, STR_VALVE_NAME_2,
        STR_VALVE_NAME_3, STR_VALVE_NAME_4
    };
    JsonArray valveNames = doc["valveNames"].to<JsonArray>();
    for (int i = 0; i < VALVE_COUNT; i++) valveNames.add(valveNameList[i]);

    JsonArray valveRulesArr = doc["valveRules"].to<JsonArray>();
    JsonArray valveOpenArr  = doc["valveOpen"].to<JsonArray>();
    for (int i = 0; i < VALVE_COUNT; i++) {
        JsonObject rule = valveRulesArr.add<JsonObject>();
        JsonObject ow   = rule["openWhen"].to<JsonObject>();
        ow["sensorId"]  = snap.valveRules[i].openWhen.sensorId;
        ow["op"]        = snap.valveRules[i].openWhen.op;
        ow["value"]     = snap.valveRules[i].openWhen.value;
        JsonObject cw   = rule["closeWhen"].to<JsonObject>();
        cw["sensorId"]  = snap.valveRules[i].closeWhen.sensorId;
        cw["op"]        = snap.valveRules[i].closeWhen.op;
        cw["value"]     = snap.valveRules[i].closeWhen.value;
        valveOpenArr.add(snap.valveOpen[i]);
    }

    doc["valveOpNone"] = STR_VALVE_OP_NONE;
    doc["valveOpGt"]   = STR_VALVE_OP_GT;
    doc["valveOpLt"]   = STR_VALVE_OP_LT;
    doc["valveOpGte"]  = STR_VALVE_OP_GTE;
    doc["valveOpLte"]  = STR_VALVE_OP_LTE;
    doc["valveOpEq"]   = STR_VALVE_OP_EQ;

    if (doc.overflowed()) {
        Serial.println("[HTTPS] WARNING: /state JSON doc overflowed");
    }

    serializeJson(doc, out);
    xSemaphoreGive(s_jsonMutex);
}

// ---------------------------------------------------------------------------
//  GET /state
//  Parses ?client= query parameter.
//  Any client that is NOT "monitor" counts as interactive and updates
//  s_lastInteractiveMs.  The monitor page identifies itself as "monitor"
//  so it does NOT update the timestamp and can receive interactiveActive=true.
// ---------------------------------------------------------------------------

static esp_err_t handle_get_state(httpd_req_t *req)
{
    // Parse ?client= query parameter
    char query[64]     = {};
    char clientVal[32] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "client", clientVal, sizeof(clientVal));
    }

    bool isMonitor = (strcmp(clientVal, "monitor") == 0);

    // Any non-monitor fetch counts as interactive activity.
    if (!isMonitor) {
        uint32_t now = (uint32_t)millis();
        s_lastInteractiveMs = (now == 0) ? 1 : now;   // avoid 0 sentinel
    }

    String output;
    buildStateJson(output, isMonitor);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, output.c_str());
    return ESP_OK;
}
static const httpd_uri_t uri_get_state = {
    .uri = "/state", .method = HTTP_GET, .handler = handle_get_state, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  GET /api/ping
//  Lightweight interactive keepalive.  The web UI sends this on load and
//  every 30 s so that interactiveActive stays true even when the tab is
//  backgrounded and JavaScript throttles the poll timer.
// ---------------------------------------------------------------------------

static esp_err_t handle_ping(httpd_req_t *req)
{
    uint32_t now = (uint32_t)millis();
    s_lastInteractiveMs = (now == 0) ? 1 : now;
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
static const httpd_uri_t uri_ping = {
    .uri = "/api/ping", .method = HTTP_GET, .handler = handle_ping, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  POST /
// ---------------------------------------------------------------------------

static esp_err_t handle_post_command(httpd_req_t *req)
{
    char buf[256];
    if (recvFullBody(req, buf, sizeof(buf)) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty or oversized command");
        return ESP_FAIL;
    }
    handleCommand(String(buf));
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
static const httpd_uri_t uri_post_command = {
    .uri = "/", .method = HTTP_POST, .handler = handle_post_command, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  POST /api/sensor_scan
// ---------------------------------------------------------------------------

static esp_err_t handle_sensor_scan(httpd_req_t *req)
{
    int count = sensorsScanBus();

    xSemaphoreTake(s_jsonMutex, portMAX_DELAY);
    static JsonDocument scanDoc;
    scanDoc.clear();
    scanDoc["count"] = count;
    JsonArray roms = scanDoc["roms"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
        uint8_t rom[8];
        sensorsGetScannedRom(i, rom);
        char hex[17];
        romToHex(rom, hex);
        roms.add(hex);
    }
    String out;
    serializeJson(scanDoc, out);
    xSemaphoreGive(s_jsonMutex);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, out.c_str());
    return ESP_OK;
}
static const httpd_uri_t uri_sensor_scan = {
    .uri = "/api/sensor_scan", .method = HTTP_POST,
    .handler = handle_sensor_scan, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  POST /api/flow_reset
// ---------------------------------------------------------------------------

static esp_err_t handle_flow_reset(httpd_req_t *req)
{
    flowResetTotal();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
static const httpd_uri_t uri_flow_reset = {
    .uri = "/api/flow_reset", .method = HTTP_POST,
    .handler = handle_flow_reset, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  POST /api/update_cert
// ---------------------------------------------------------------------------

static esp_err_t handle_update_cert(httpd_req_t *req)
{
    const size_t MAX_BODY = 8192;
    if (req->content_len == 0 || req->content_len >= MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or oversized body");
        return ESP_FAIL;
    }

    char* buf = (char*)malloc(MAX_BODY);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = recvFullBody(req, buf, MAX_BODY);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive error");
        return ESP_FAIL;
    }

    JsonDocument jdoc;
    DeserializationError err = deserializeJson(jdoc, buf, received);
    free(buf);

    if (err) {
        Serial.printf("[HTTPS] update_cert: JSON parse error: %s\n", err.c_str());
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON parse error");
        return ESP_FAIL;
    }

    const char* certPem = jdoc["cert"];
    const char* keyPem  = jdoc["key"];
    if (!certPem || !keyPem || strlen(certPem) < 64 || strlen(keyPem) < 64) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing cert or key field");
        return ESP_FAIL;
    }

    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int parseRet = mbedtls_x509_crt_parse(&crt,
                       (const uint8_t*)certPem, strlen(certPem) + 1);
    mbedtls_x509_crt_free(&crt);
    if (parseRet != 0) {
        Serial.printf("[HTTPS] update_cert: cert parse failed: -0x%04X\n", -parseRet);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid certificate PEM");
        return ESP_FAIL;
    }

    if (!certSave(certPem, strlen(certPem) + 1, keyPem, strlen(keyPem) + 1)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
        return ESP_FAIL;
    }

    Serial.println("[HTTPS] New cert saved — rebooting in 1 s");
    httpd_resp_sendstr(req, "Cert saved. Rebooting.");
    delay(1000);
    ESP.restart();
    return ESP_OK;
}
static const httpd_uri_t uri_update_cert = {
    .uri = "/api/update_cert", .method = HTTP_POST,
    .handler = handle_update_cert, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  POST /ota
// ---------------------------------------------------------------------------

static esp_err_t handle_ota(httpd_req_t *req)
{
    size_t total = req->content_len;
    if (total == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No firmware content");
        return ESP_FAIL;
    }
    Serial.printf("[OTA] Incoming firmware: %u bytes\n", total);
    if (!Update.begin(total)) {
        Serial.printf("[OTA] begin() failed: %s\n", Update.errorString());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
        return ESP_FAIL;
    }
    uint8_t buf[512];
    size_t  remaining = total;
    while (remaining > 0) {
        int recv = httpd_req_recv(req, (char*)buf, std::min(remaining, sizeof(buf)));
        if (recv == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (recv <= 0) {
            Update.abort();
            Serial.println("[OTA] receive error — aborted");
            return ESP_FAIL;
        }
        if (Update.write(buf, recv) != (size_t)recv) {
            Update.abort();
            Serial.printf("[OTA] write error: %s\n", Update.errorString());
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
            return ESP_FAIL;
        }
        remaining -= recv;
    }
    if (!Update.end(true)) {
        Serial.printf("[OTA] end() failed: %s\n", Update.errorString());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
        return ESP_FAIL;
    }
    Serial.println("[OTA] Success — rebooting in 500 ms");
    httpd_resp_sendstr(req, "OTA success. Device rebooting.");
    delay(500);
    ESP.restart();
    return ESP_OK;
}
static const httpd_uri_t uri_ota = {
    .uri = "/ota", .method = HTTP_POST, .handler = handle_ota, .user_ctx = NULL
};

// ---------------------------------------------------------------------------
//  httpServerInit
// ---------------------------------------------------------------------------

void httpServerInit()
{
    s_jsonMutex = xSemaphoreCreateMutex();
    configASSERT(s_jsonMutex);

    certLoad();
    certLogExpiry();

    httpd_ssl_config_t conf     = HTTPD_SSL_CONFIG_DEFAULT();
    conf.servercert             = s_cert.data();
    conf.servercert_len         = s_cert.size();
    conf.prvtkey_pem            = s_key.data();
    conf.prvtkey_len            = s_key.size();
    conf.httpd.stack_size       = 8192;
    conf.httpd.max_uri_handlers = 16;
    conf.port_secure            = 443;

    // 3 concurrent sockets: 1 active + 1 reconnecting + 1 for OTA/cert push.
    conf.httpd.max_open_sockets = 3;
    conf.httpd.lru_purge_enable = true;
    conf.httpd.max_req_hdr_len  = 2048;

    // Disable TLS session tickets — saves ~10 KB internal DRAM per session.
    conf.session_tickets = MBEDTLS_SSL_SESSION_TICKETS_DISABLED;

    esp_err_t ret = httpd_ssl_start(&s_server, &conf);
    if (ret != ESP_OK) {
        Serial.printf("[HTTPS] httpd_ssl_start failed: %s\n", esp_err_to_name(ret));
        return;
    }

    webUiRegisterHandlers(s_server);
    httpd_register_uri_handler(s_server, &uri_post_command);
    httpd_register_uri_handler(s_server, &uri_get_state);
    httpd_register_uri_handler(s_server, &uri_ping);
    httpd_register_uri_handler(s_server, &uri_sensor_scan);
    httpd_register_uri_handler(s_server, &uri_flow_reset);
    httpd_register_uri_handler(s_server, &uri_update_cert);
    httpd_register_uri_handler(s_server, &uri_ota);

    Serial.println("[HTTPS] Server started on port 443");
}

void httpServerHandle()
{
    // intentionally empty — esp_https_server runs on its own FreeRTOS task
}
