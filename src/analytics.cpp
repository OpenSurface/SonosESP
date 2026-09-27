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
#include <esp_task_wdt.h>   // the bracket below; not relied on transitively

// One shot per boot, but spent on a real attempt rather than on reaching this
// function. The first version set it at the top, which meant a panel that was
// merely busy at t=90s — artwork holding network_mutex, a cooldown still
// running, DMA momentarily under the gate — was never counted for that entire
// boot. Those are the conditions a device playing music is in most of the time,
// so it biased the count against exactly the panels that are being used.
//
// Transient skips now back off and retry instead. The budget is what stops a
// dead endpoint or a permanently busy radio becoming a ping loop: after
// ANALYTICS_MAX_TRIES the boot is written off, uncounted and quietly.
static bool     s_analytics_done = false;
static uint8_t  s_tries          = 0;
static uint32_t s_next_try_ms    = 0;

#define ANALYTICS_MAX_TRIES   5
#define ANALYTICS_RETRY_MS    60000UL

// Back off and come back, unless the budget is gone.
static void analyticsRetryLater(const char* why) {
    if (++s_tries >= ANALYTICS_MAX_TRIES) {
        s_analytics_done = true;
        Serial.printf("[STATS] Giving up after %d tries (%s)\n", s_tries, why);
        return;
    }
    s_next_try_ms = millis() + ANALYTICS_RETRY_MS;
    Serial.printf("[STATS] %s - retry %d/%d in 60s\n", why, s_tries, ANALYTICS_MAX_TRIES - 1);
}

void analyticsTick() {
    if (s_analytics_done) return;
    if (millis() < ANALYTICS_BOOT_DELAY_MS) return;
    if (s_next_try_ms && (int32_t)(millis() - s_next_try_ms) < 0) return;
    if (WiFi.status() != WL_CONNECTED) return;   // try again once the link is up

    if (!analytics_enabled) {
        s_analytics_done = true;                 // a setting, not a transient
        Serial.println("[STATS] Disabled - nothing sent");
        return;
    }

    // DMA floor, checked before anything is allocated. A TLS session costs
    // ~32KB of DMA, and a usage counter is never worth an allocation the next
    // artwork download needs. Skipping is the correct outcome: the count is
    // approximate by design and one missing panel-boot changes nothing.
    if (heap_caps_get_free_size(MALLOC_CAP_DMA) < ART_MIN_DMA_PRE_BURST) {
        analyticsRetryLater("DMA too low");
        return;
    }

    // Same envelope as every other network path in this firmware: the general
    // cooldown plus the HTTPS/TLS-teardown gate, then network_mutex. Discovery
    // skipping this is what caused #182, so nothing new gets to skip it either.
    if (!sdioPreWait("STATS", SDIO_WAIT_HTTPS_COOLDOWN)) {
        analyticsRetryLater("SDIO busy");
        return;
    }

    if (!network_mutex ||
        xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        analyticsRetryLater("network busy");
        return;
    }

    // Past every transient gate: this is the one real attempt. Spend the
    // one-shot here so a dead endpoint cannot turn into a ping loop.
    s_analytics_done = true;

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

    // Same watchdog bracket as the background update check, same reason.
    // Worst case here is sdioPreWait 6.2s + mutex 5s + connect 8s + read 8s,
    // which is ~27s against a 30s WATCHDOG_TIMEOUT_SEC — under it, but three
    // seconds of margin is not a margin. This runs once per boot on
    // mainAppTask, the only WDT-subscribed task, which feeds the dog once per
    // loop iteration and gets no chance to while this blocks.
    esp_task_wdt_delete(NULL);

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

    esp_task_wdt_add(NULL);   // re-subscribe; every path above reaches here
}
