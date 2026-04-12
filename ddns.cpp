// =============================================================================
//  ddns.cpp  –  Cloudflare DDNS updater
//  Runs as a low-priority FreeRTOS task on Core 0.
//  Updates the A record only when the IP actually changes.
//
//  Credentials loaded from NVS namespace "distill":
//    cf_token    – Cloudflare API token (Zone:DNS:Edit)
//    cf_zone_id  – Zone ID for baghamut.com
//    cf_rec_id   – DNS record ID for winery.baghamut.com
// =============================================================================
#include "ddns.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

static String s_lastIp = "";

// -----------------------------------------------------------------------------
//  updateCloudflare()  –  PATCHes the A record with the given IP.
//  Loads credentials from NVS on every call so live updates take effect.
// -----------------------------------------------------------------------------
static bool updateCloudflare(const String& ip)
{
    Preferences prefs;
    prefs.begin("distill", true);
    String token  = prefs.getString("cf_token",   "");
    String zoneId = prefs.getString("cf_zone_id", "");
    String recId  = prefs.getString("cf_rec_id",  "");
    prefs.end();
    Serial.printf("[DDNS] token='%s' zone='%s' rec='%s'\n", token.c_str(), zoneId.c_str(), recId.c_str());

    if (token.isEmpty() || zoneId.isEmpty() || recId.isEmpty()) {
        Serial.println("[DDNS] Cloudflare credentials not set in NVS — skipping update.");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = String("https://api.cloudflare.com/client/v4/zones/")
                 + zoneId + "/dns_records/" + recId;

    http.begin(client, url);
    http.addHeader("Authorization", String("Bearer ") + token);
    http.addHeader("Content-Type",  "application/json");

    String fqdn = String(DDNS_HOSTNAME) + "." + String(DDNS_DOMAIN);
    String body = String("{\"type\":\"A\",\"name\":\"") + fqdn
                + "\",\"content\":\"" + ip
                + "\",\"ttl\":60,\"proxied\":false}";

    int    code     = http.PATCH(body);
    String response = http.getString();
    http.end();

    bool ok = (code == 200) && (response.indexOf("\"success\":true") >= 0);

    Serial.printf("[DDNS] Cloudflare update %s → %s  HTTP %d  %s\n",
                  fqdn.c_str(), ip.c_str(), code, ok ? "OK" : "FAILED");

    if (!ok) {
        Serial.printf("[DDNS] Response: %s\n", response.c_str());
    }

    return ok;
}

// -----------------------------------------------------------------------------
//  getPublicIp()  –  Fetches WAN IP from an external service.
//  Tries primary endpoint, falls back to secondary on failure.
// -----------------------------------------------------------------------------
static String getPublicIp()
{
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    const char* endpoints[] = {
        "https://api.ipify.org",
        "https://ifconfig.me/ip"
    };

    for (auto& url : endpoints) {
        http.begin(client, url);
        http.setTimeout(5000);
        int code = http.GET();
        if (code == 200) {
            String ip = http.getString();
            ip.trim();
            http.end();
            if (ip.length() >= 7 && ip.length() <= 15) return ip;
        }
        http.end();
    }

    Serial.println("[DDNS] Could not determine public IP.");
    return "";
}

// -----------------------------------------------------------------------------
//  ddnsTask()  –  FreeRTOS task body.
// -----------------------------------------------------------------------------
void ddnsTask(void* pv)
{
    // Wait for WiFi association
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(1000));

    // Wait for IP to settle — DHCP sometimes restores a stale lease first,
    // then corrects it within ~10 s. 15 s covers this reliably.
    vTaskDelay(pdMS_TO_TICKS(15000));

    // Force immediate update on boot — covers power failure / router reboot
    // scenarios where the public IP may have changed during the outage.
    {
        String ip = getPublicIp();
        if (!ip.isEmpty()) {
            if (updateCloudflare(ip)) s_lastIp = ip;
        }
    }

    // Polling loop — only updates Cloudflare when IP actually changes.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DDNS_UPDATE_INTERVAL_MS));

        if (WiFi.status() != WL_CONNECTED) continue;

        String ip = getPublicIp();
        if (ip.isEmpty()) continue;

        if (ip != s_lastIp) {
            if (updateCloudflare(ip)) s_lastIp = ip;
        }
    }
}

// -----------------------------------------------------------------------------
//  ddnsInit()  –  Spawns the DDNS task on Core 0.
// -----------------------------------------------------------------------------
void ddnsInit()
{
    xTaskCreatePinnedToCore(
        ddnsTask, "ddns",
        8192,
        NULL,
        1,
        NULL,
        0
    );
    Serial.println("[DDNS] Task started");
}