/**
 * Anonymous install counter — see include/analytics.h for exactly what is sent
 * and what deliberately is not.
 */

#include "analytics.h"
#include "ui_common.h"
#include "config.h"
#include "ui_network_guard.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// One shot per boot. Set before the attempt, not after: a failure must not make
// this retry on the next tick and turn a dead endpoint into a ping loop.
static bool s_analytics_done = false;

void analyticsTick() {
    if (s_analytics_done) return;
    if (millis() < ANALYTICS_BOOT_DELAY_MS) return;
    if (WiFi.status() != WL_CONNECTED) return;   // try again once the link is up

    s_analytics_done = true;                     // exactly one attempt, ever

    if (!analytics_enabled) {
        Serial.println("[STATS] Disabled - nothing sent");
        return;
    }

    // DMA floor, checked before anything is allocated. A TLS session costs
    // ~32KB of DMA, and a usage counter is never worth an allocation the next
    // artwork download needs. Skipping is the correct outcome: the count is
    // approximate by design and one missing panel-boot changes nothing.
    if (heap_caps_get_free_size(MALLOC_CAP_DMA) < ART_MIN_DMA_PRE_BURST) {
        Serial.println("[STATS] Skipped - DMA too low");
        return;
    }

    // Same envelope as every other network path in this firmware: the general
    // cooldown plus the HTTPS/TLS-teardown gate, then network_mutex. Discovery
    // skipping this is what caused #182, so nothing new gets to skip it either.
    if (!sdioPreWait("STATS", SDIO_WAIT_HTTPS_COOLDOWN)) return;

    if (!network_mutex ||
        xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        Serial.println("[STATS] Skipped - network busy");
        return;
    }

    // Version and panel size, as a path. GoatCounter shows each as its own row,
    // so the dashboard gives the version spread and the 4"/7" split for free.
    char url[160];
    snprintf(url, sizeof(url),
             "https://%s/count?p=/boot/%s/%din&t=SonosESP+boot",
             ANALYTICS_HOST, FIRMWARE_VERSION, SCREEN_SIZE);

    WiFiClientSecure client;
    client.setInsecure();          // same posture as lyrics, weather, art and OTA
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(8000);

    if (http.begin(client, url)) {
        // Identifies the fleet in the dashboard's browser column, and keeps the
        // request from being filtered as an unknown bot. No device identifier.
        http.setUserAgent("SonosESP/" FIRMWARE_VERSION " (esp32-p4)");
        int code = http.GET();
        Serial.printf("[STATS] Ping %s (%d)\n", code == 200 ? "ok" : "failed", code);
        http.end();
    } else {
        Serial.println("[STATS] Ping failed - begin()");
    }
    client.stop();

    // Stamp both, exactly as the weather and lyrics paths do, so the next SOAP
    // and the next artwork download still see their drain window.
    last_network_end_ms = millis();
    last_https_end_ms   = millis();
    xSemaphoreGive(network_mutex);
}
