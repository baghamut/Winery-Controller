// =============================================================================
//  ddns.cpp  –  GoDaddy DDNS updater
//  Runs as a low-priority FreeRTOS task on Core 0.
//  Updates the A record only when the IP actually changes.
// =============================================================================
#include "ddns.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static String s_lastIp = "";

static bool updateGoDaddy(const String& ip)
{
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = String("https://api.godaddy.com/v1/domains/")
                 + DDNS_DOMAIN + "/records/A/" + DDNS_HOSTNAME;

    http.begin(client, url);
    http.addHeader("Authorization",
                   String("sso-key ") + DDNS_API_KEY + ":" + DDNS_API_SECRET);
    http.addHeader("Content-Type", "application/json");

    String body = String("[{\"data\":\"") + ip + "\",\"ttl\":600}]";
    int code = http.PATCH(body);

    bool ok = (code == 200);
    Serial.printf("[DDNS] GoDaddy update %s → %s  HTTP %d\n",
                  DDNS_HOSTNAME "." DDNS_DOMAIN, ip.c_str(), code);
    http.end();
    return ok;
}

void ddnsTask(void* pv)
{
    // Wait for WiFi association
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(1000));

    // Wait for IP to settle — DHCP sometimes restores a stale lease first,
    // then corrects it within ~10 s. 15 s covers this reliably.
    vTaskDelay(pdMS_TO_TICKS(15000));

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            String ip = WiFi.localIP().toString();
            if (ip != s_lastIp) {
                if (updateGoDaddy(ip)) s_lastIp = ip;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(DDNS_UPDATE_INTERVAL_MS));
    }
}

void ddnsInit()
{
    xTaskCreatePinnedToCore(
        ddnsTask, "ddns",
        4096,
        NULL,
        1,
        NULL,
        0
    );
    Serial.println("[DDNS] Task started");
}