/**
 * Sonos Controller - Optimized for ESP32-P4
 * Uses HTTPClient for better connection handling
 */

#include "sonos_controller.h"
#include "config.h"
#include "ui_network_guard.h"
#include <HTTPClient.h>
#include "lvgl.h"
#include "ui_common.h"
#include <new>  // placement new for PSRAM device array
#include "esp_memory_utils.h"  // esp_ptr_external_ram()
// Note: #include <lwip/sockets.h> removed — SO_LINGER (CONFIG_LWIP_SO_LINGER) is NOT
// compiled into the pioarduino pre-built ESP-IDF framework. sdkconfig.defaults has no
// effect on pre-compiled libs. lwip_setsockopt(fd, SO_LINGER) always returns -1.
// TCP teardowns use FIN → TIME_WAIT (120s, CONFIG_LWIP_TCP_MSL=60000ms, also fixed).
// The 16-slot PCB pool (CONFIG_LWIP_MAX_SOCKETS=16) is managed by tcp_kill_timewait()
// which auto-recycles oldest TIME_WAIT slot when pool is exhausted — no crash.

// Command debounce tracking
static uint32_t lastCommandTime = 0;

// Encode string for XML/SOAP transport
static void encodeXML(String& s) {
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
}

SonosController::SonosController() {
    // Keep constructor minimal - global objects are constructed before setup(),
    // before PSRAM is guaranteed initialized. Allocation happens in begin().
    devices = nullptr;
    deviceCount = 0;
    currentDeviceIndex = -1;
    deviceMutex = NULL;
    commandQueue = NULL;
    uiUpdateQueue = NULL;
    networkTaskHandle = NULL;
    pollingTaskHandle = NULL;
    networkTaskStack  = nullptr;
    pollingTaskStack  = nullptr;
}

SonosController::~SonosController() {
    if (networkTaskHandle) vTaskDelete(networkTaskHandle);
    if (pollingTaskHandle) vTaskDelete(pollingTaskHandle);
    if (deviceMutex) vSemaphoreDelete(deviceMutex);
    if (commandQueue) vQueueDelete(commandQueue);
    if (uiUpdateQueue) vQueueDelete(uiUpdateQueue);

    // Explicitly call destructors (frees String heap allocations) then free PSRAM block
    if (devices) {
        for (int i = 0; i < MAX_SONOS_DEVICES; i++) {
            devices[i].~SonosDevice();
        }
        heap_caps_free(devices);
        devices = nullptr;
    }
}

void SonosController::begin() {
    // Allocate devices array in PSRAM here (not in constructor) - PSRAM is
    // guaranteed initialized by the time begin() is called from setup().
    // Keeps ~112KB out of DMA-capable SRAM, preventing SDIO RX buffer exhaustion.
    devices = (SonosDevice*)heap_caps_malloc(
        MAX_SONOS_DEVICES * sizeof(SonosDevice), MALLOC_CAP_SPIRAM);
    if (!devices) {
        // Fallback: DRAM (should never happen with 32MB PSRAM)
        Serial.println("[SONOS] WARNING: PSRAM unavailable, falling back to DRAM for devices");
        devices = (SonosDevice*)heap_caps_malloc(
            MAX_SONOS_DEVICES * sizeof(SonosDevice), MALLOC_CAP_8BIT);
    }
    if (devices) {
        // Placement new: runs constructors on all String members (initialises to empty)
        for (int i = 0; i < MAX_SONOS_DEVICES; i++) {
            new (&devices[i]) SonosDevice();
        }
        Serial.printf("[SONOS] Devices array: %u bytes in %s\n",
            (unsigned)(MAX_SONOS_DEVICES * sizeof(SonosDevice)),
            esp_ptr_external_ram(devices) ? "PSRAM" : "DRAM");
    } else {
        Serial.println("[SONOS] FATAL: Could not allocate devices array - discovery disabled");
    }

    deviceMutex = xSemaphoreCreateMutex();
    commandQueue = xQueueCreate(SONOS_CMD_QUEUE_SIZE, sizeof(CommandRequest_t));
    uiUpdateQueue = xQueueCreate(SONOS_UI_QUEUE_SIZE, sizeof(UIUpdate_t));
    prefs.begin("sonos", false);
    Serial.println("[SONOS] SonosController initialized");
}

void SonosController::startTasks() {
    if (networkTaskHandle == NULL) {
        if (!networkTaskStack)
            networkTaskStack = (StackType_t*)heap_caps_malloc(SONOS_NET_TASK_STACK, MALLOC_CAP_SPIRAM);
        if (networkTaskStack) {
            networkTaskHandle = xTaskCreateStaticPinnedToCore(
                networkTaskFunction, "SonosNet", SONOS_NET_TASK_STACK / sizeof(StackType_t),
                this, SONOS_NET_TASK_PRIORITY, networkTaskStack, &networkTaskTCB, 1);
        } else {
            Serial.println("[SONOS] Net PSRAM stack alloc failed — using internal SRAM");
            xTaskCreatePinnedToCore(networkTaskFunction, "SonosNet", SONOS_NET_TASK_STACK,
                                    this, SONOS_NET_TASK_PRIORITY, &networkTaskHandle, 1);
        }
    }
    if (pollingTaskHandle == NULL) {
        if (!pollingTaskStack)
            pollingTaskStack = (StackType_t*)heap_caps_malloc(SONOS_POLL_TASK_STACK, MALLOC_CAP_SPIRAM);
        if (pollingTaskStack) {
            pollingTaskHandle = xTaskCreateStaticPinnedToCore(
                pollingTaskFunction, "SonosPoll", SONOS_POLL_TASK_STACK / sizeof(StackType_t),
                this, SONOS_POLL_TASK_PRIORITY, pollingTaskStack, &pollingTaskTCB, 1);
        } else {
            Serial.println("[SONOS] Poll PSRAM stack alloc failed — using internal SRAM");
            xTaskCreatePinnedToCore(pollingTaskFunction, "SonosPoll", SONOS_POLL_TASK_STACK,
                                    this, SONOS_POLL_TASK_PRIORITY, &pollingTaskHandle, 1);
        }
    }
    Serial.println("[SONOS] Background tasks started");
}

// ============================================================================
// Discovery - Implemented in sonos_discovery.cpp
// ============================================================================

SonosDevice* SonosController::getDevice(int index) {
    if (index >= 0 && index < deviceCount) return &devices[index];
    return nullptr;
}

SonosDevice* SonosController::getCurrentDevice() {
    // Use local copy to prevent TOCTOU (time-of-check-time-of-use) race
    // Reading int is atomic on 32-bit systems, so no mutex needed for performance
    int index = currentDeviceIndex;
    if (index >= 0 && index < deviceCount) {
        return &devices[index];
    }
    return nullptr;
}

void SonosController::selectDevice(int index) {
    if (index >= 0 && index < deviceCount) {
        currentDeviceIndex = index;
        devices[index].connected = true;
        Serial.printf("[SONOS] Selected: %s\n", devices[index].ip.toString().c_str());

        // Cache the selected device for fast boot next time
        cacheSelectedDevice();
    }
}

// ============================================================================
// SOAP Request - Faster timeout
// ============================================================================
String SonosController::sendSOAP(const char* service, const char* action, const char* args) {
    return sendSOAP(getCurrentDevice(), service, action, args);
}

// H-7: explicit-target overload. Group ops (joinGroup/leaveGroup/updateGroupInfo) call
// this to address a specific speaker WITHOUT mutating the shared currentDeviceIndex —
// the polling task reads that index concurrently, so a transient swap made it fire SOAPs
// at the wrong speaker. dev->ip and the dev->errorCount/connected bookkeeping below all
// correctly apply to whichever device is targeted.
String SonosController::sendSOAP(SonosDevice* dev, const char* service, const char* action, const char* args) {
    if (!dev) return "";

    // Validate args size to prevent buffer overflow
    // SOAP wrapper adds ~400 bytes, so args must stay under 1600 bytes
    size_t args_len = strlen(args);
    if (args_len > 1600) {
        Serial.printf("[SONOS] ERROR: SOAP args too large (%d bytes, max 1600)\n", args_len);
        return "";
    }

    // PRE-WAIT: general 200ms cooldown only. SOAP is plain HTTP — it does NOT need the
    // HTTPS cooldown (SDIO_HTTPS_COOLDOWN_MS = 3s). That cooldown was preventing SOAPs
    // from firing for 3s after every lyrics fetch → SDIO DMA idled → pkt_rxbuff :928
    // overflow when the next art download started on a cold DMA.
    // mbedTLS DMA buffers from lyrics HTTPS are irrelevant to plain HTTP SOAP traffic.
    // TCP FIN-ACKs from a prior HTTPS session drain within the 200ms general cooldown.
    if (last_network_end_ms > 0) {
        unsigned long elapsed = millis() - last_network_end_ms;
        if (elapsed < SDIO_GENERAL_COOLDOWN_MS) {
            vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - elapsed));
        }
    }

    // Acquire network_mutex to serialize WiFi access
    if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
        Serial.println("[SOAP] Failed to acquire network mutex - request failed");
        return "";
    }

    // POST-MUTEX re-check: another task may have used the network while we waited.
    // General 200ms only — no HTTPS cooldown (see pre-wait comment above).
    {
        unsigned long elapsed = millis() - last_network_end_ms;
        if (last_network_end_ms > 0 && elapsed < SDIO_GENERAL_COOLDOWN_MS) {
            vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - elapsed));
        }
    }

    // ── Request construction — MUST stay inside the mutex ─────────────────────
    // These buffers are static to avoid ~2.9KB of stack on each of the three task
    // stacks that call sendSOAP(), which makes them SHARED state. sendSOAP() runs
    // concurrently on mainAppTask (browse/groups/play), pollingTask and networkTask,
    // so building them before the mutex allowed one task to overwrite another's body
    // during the cooldown/mutex wait — e.g. a user's Next command being POSTed with a
    // GetPositionInfo body under a Next SOAPAction (command silently lost). Everything
    // below reads these buffers, so it all belongs under the lock.
    static char url[256];
    static char body[2048];  // Large buffer for SOAP body
    static char soapAction[256];
    static char soapActionHeader[280];
    static char custom_endpoint[128];
    const char* endpoint;

    // Determine endpoint (no String allocation)
    if (strstr(service, "AVTransport")) {
        endpoint = "/MediaRenderer/AVTransport/Control";
    } else if (strstr(service, "RenderingControl")) {
        endpoint = "/MediaRenderer/RenderingControl/Control";
    } else if (strstr(service, "ContentDirectory")) {
        endpoint = "/MediaServer/ContentDirectory/Control";
    } else {
        // Fallback - build endpoint in buffer
        snprintf(custom_endpoint, sizeof(custom_endpoint), "/MediaRenderer/%s/Control", service);
        endpoint = custom_endpoint;
    }

    // Build URL without String concatenation
    snprintf(url, sizeof(url), "http://%s:1400%s", dev->ip.toString().c_str(), endpoint);

    // Build SOAP body without String concatenation
    snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%s xmlns:u=\"urn:schemas-upnp-org:service:%s:1\">%s</u:%s>"
        "</s:Body></s:Envelope>",
        action, service, args, action);

    // Build SOAPAction header (quoted form built straight after)
    snprintf(soapAction, sizeof(soapAction), "urn:schemas-upnp-org:service:%s:1#%s", service, action);
    snprintf(soapActionHeader, sizeof(soapActionHeader), "\"%s\"", soapAction);

    // Fresh HTTPClient per request — Sonos's embedded HTTP server does not support
    // HTTP/1.1 keep-alive (persistent connections cause -1 / connection-refused errors).
    HTTPClient http;
    http.begin(url);
    http.setTimeout(2000);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    // Tell Sonos to close the connection after the response. Sonos already does this
    // (no keep-alive per comment above), but making it explicit ensures the server
    // sends its FIN immediately after the response body → FIN-wait loop (below) reliably
    // detects CLOSE_WAIT → passive close → zero DMA cost per SOAP.
    http.addHeader("Connection", "close");
    http.addHeader("SOAPAction", soapActionHeader);

    int code = http.POST(body);
    String response = "";  // Keep String for return value (used by callers)

    if (code == 200) {
        response = http.getString();
        dev->errorCount = 0;
        dev->connected = true;
    } else if (code == 500) {
        // Sonos returns 500 during source transitions (e.g. radio switching)
        // This is transient - don't count as error.
        // Read and discard the 500 response body: this lets the server send its FIN
        // alongside/after the body → we reach CLOSE_WAIT → passive close → no TIME_WAIT.
        http.getString();
        last_transient_500_ms = millis();  // arm 3s storm gate in art task pre-wait (unthrottled)
        // Throttle logging to avoid spam (only log first 500 in a burst)
        static unsigned long last_500_log = 0;
        if (millis() - last_500_log > 2000) {  // 2s throttle
            Serial.printf("[SOAP] Transient 500 for %s.%s | adlp=%d heap=%u dma=%u\n",
                service, action,
                (int)art_download_in_progress,
                heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                heap_caps_get_free_size(MALLOC_CAP_DMA));
            last_500_log = millis();
        }
    } else {
        Serial.printf("[SOAP] HTTP error %d for %s.%s\n", code, service, action);
        dev->errorCount++;
        // Only disconnect after multiple consecutive errors
        // This handles temporary network issues during source changes
        if (code == -1) {
            // Connection refused - immediate disconnect
            if (dev->connected) {
                Serial.printf("[SONOS] Device disconnected (connection refused)\n");
            }
            dev->connected = false;
            dev->errorCount = 10;
        } else if (code == -11) {
            // Timeout - might be temporary (source change), need 3 consecutive timeouts
            if (dev->errorCount >= 3) {
                if (dev->connected) {
                    Serial.printf("[SONOS] Device disconnected (repeated timeouts)\n");
                }
                dev->connected = false;
            }
        } else if (dev->errorCount > 5) {
            if (dev->connected) {
                Serial.println("[SONOS] Device disconnected (too many errors)");
            }
            dev->connected = false;
        }
    }

    // Passive close: wait up to 100ms for server's FIN to arrive.
    // With Connection:close header, Sonos sends FIN immediately after response body.
    // stream->connected() = false once lwIP receives FIN → we're in CLOSE_WAIT.
    // http.end() from CLOSE_WAIT → LAST_ACK → CLOSED: server enters TIME_WAIT, not us.
    // Result: zero DMA cost per SOAP. 100ms (was 20ms) gives more margin for Sonos FINs.
    // Fallback: if no FIN in 100ms → active close (TIME_WAIT on our side, ~400B PCB DMA).
    // DMA savings: even 50% passive-close success at 1 SOAP/300ms = ~3KB DMA saved per 300ms.
    if (code == 200 || code == 500) {
        if (WiFiClient* s = http.getStreamPtr()) {
            for (int i = 0; i < 100 && s->connected(); i++)
                vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // Measure DMA delta across http.end() to track per-SOAP PCB cost.
    // Passive close (FIN already received → CLOSE_WAIT): delta ≈ 0, no TIME_WAIT.
    // Active close fallback (no FIN in 5ms): delta ~6KB, TIME_WAIT on our side.
    size_t dma_pre_end = heap_caps_get_free_size(MALLOC_CAP_DMA);
    http.end();
    vTaskDelay(pdMS_TO_TICKS(1));  // allow lwIP to process RST/FIN synchronously
    size_t dma_post_end = heap_caps_get_free_size(MALLOC_CAP_DMA);

    // Per-SOAP DMA tracking: detects gradual DMA depletion from TIME_WAIT PCBs.
    // Logs: every SOAP when DMA < 50KB (danger zone), else every 50 SOAPs.
    {
        static size_t session_start_dma = 0;
        static int soap_count = 0;
        if (session_start_dma == 0) session_start_dma = dma_post_end;
        soap_count++;
        int delta_end  = (int)((long)dma_post_end  - (long)dma_pre_end);
        int delta_sess = (int)((long)dma_post_end  - (long)session_start_dma);
        if (dma_post_end < 50000 || delta_end < -2048 || soap_count % 10 == 0) {
            Serial.printf("[SOAP/DMA] #%d: pre=%uKB post=%uKB delta=%+dB session=%+dKB\n",
                          soap_count,
                          (unsigned)dma_pre_end/1024, (unsigned)dma_post_end/1024,
                          delta_end, delta_sess/1024);
        }
    }

    // Update timestamp before releasing mutex (for SDIO cooldown tracking)
    last_network_end_ms = millis();

    // Release network mutex after HTTP operation completes
    xSemaphoreGive(network_mutex);

    return response;
}

// ============================================================================
// Helpers
// ============================================================================
int SonosController::timeToSeconds(const String& time) {
    if (time.length() == 0) return 0;

    int h = 0, m = 0, s = 0;
    int c1 = time.indexOf(':');
    int c2 = (c1 > 0) ? time.indexOf(':', c1 + 1) : -1;

    if (c1 > 0 && c2 > c1) {
        // Format: H:MM:SS or HH:MM:SS
        h = constrain(time.substring(0, c1).toInt(), 0, 99);
        m = constrain(time.substring(c1 + 1, c2).toInt(), 0, 59);
        s = constrain(time.substring(c2 + 1).toInt(), 0, 59);
    } else if (c1 > 0) {
        // Format: M:SS or MM:SS
        m = constrain(time.substring(0, c1).toInt(), 0, 59);
        s = constrain(time.substring(c1 + 1).toInt(), 0, 59);
    }
    return h * 3600 + m * 60 + s;
}

// Extract XML tag value - searches entire string
String SonosController::extractXML(const String& xml, const char* tag) {
    return extractXMLRange(xml, tag, 0, xml.length());
}

// Extract XML tag value within a range - avoids substring copy for nested searches
String SonosController::extractXMLRange(const String& xml, const char* tag, int rangeStart, int rangeEnd) {
    // Build tags on stack to avoid heap allocations
    char startTag[64], endTag[64], attrTag[64];
    snprintf(startTag, sizeof(startTag), "<%s>", tag);
    snprintf(endTag, sizeof(endTag), "</%s>", tag);
    snprintf(attrTag, sizeof(attrTag), "<%s ", tag);

    // Search only within the specified range
    int start = xml.indexOf(startTag, rangeStart);
    if (start < 0 || start >= rangeEnd) {
        // Try with attributes
        start = xml.indexOf(attrTag, rangeStart);
        if (start < 0 || start >= rangeEnd) return "";
        start = xml.indexOf(">", start);
        if (start < 0 || start >= rangeEnd) return "";
        start++;
    } else {
        start += strlen(startTag);
    }

    int end = xml.indexOf(endTag, start);
    if (end < 0 || end > rangeEnd) return "";

    return xml.substring(start, end);
}

String SonosController::decodeHTML(String text) {
    // Optimized single-pass replacement using lookup table
    // Reserve space to avoid multiple reallocations
    text.reserve(text.length() + 10);

    // Lookup table for replacements (pattern -> replacement)
    static const struct { const char* from; const char* to; } replacements[] = {
        // HTML entities (most common first)
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"},

        // URL-encoded
        {"%3a", ":"}, {"%3A", ":"}, {"%2f", "/"}, {"%2F", "/"}, {"%3f", "?"}, {"%3F", "?"},
        {"%3d", "="}, {"%3D", "="}, {"%26", "&"},

        // Numeric HTML entities -> the real UTF-8 character.
        // These used to transliterate accents away (é -> e) because LVGL's built-in
        // Montserrat is ASCII-only and anything else drew a tofu box. The text fonts
        // now carry a Latin-1 / Latin-Ext-A fallback (see ui_fonts.h), so decode
        // properly instead of destroying the spelling — "Beyoncé" not "Beyonce".
        {"&#xe9;", "\xC3\xA9"}, {"&#xE9;", "\xC3\xA9"}, {"&#xe8;", "\xC3\xA8"}, {"&#xE8;", "\xC3\xA8"},
        {"&#xea;", "\xC3\xAA"}, {"&#xEA;", "\xC3\xAA"}, {"&#xe0;", "\xC3\xA0"}, {"&#xE0;", "\xC3\xA0"},
        {"&#xe2;", "\xC3\xA2"}, {"&#xE2;", "\xC3\xA2"}, {"&#xf4;", "\xC3\xB4"}, {"&#xF4;", "\xC3\xB4"},
        {"&#xf9;", "\xC3\xB9"}, {"&#xF9;", "\xC3\xB9"}, {"&#xfb;", "\xC3\xBB"}, {"&#xFB;", "\xC3\xBB"},
        {"&#xee;", "\xC3\xAE"}, {"&#xEE;", "\xC3\xAE"}, {"&#xe7;", "\xC3\xA7"}, {"&#xE7;", "\xC3\xA7"},
        {"&#xf1;", "\xC3\xB1"}, {"&#xF1;", "\xC3\xB1"},

        // Numeric HTML entities (decimal)
        {"&#233;", "\xC3\xA9"}, {"&#232;", "\xC3\xA8"}, {"&#234;", "\xC3\xAA"}, {"&#224;", "\xC3\xA0"},
        {"&#226;", "\xC3\xA2"}, {"&#244;", "\xC3\xB4"}, {"&#249;", "\xC3\xB9"}, {"&#251;", "\xC3\xBB"},
        {"&#238;", "\xC3\xAE"}, {"&#231;", "\xC3\xA7"}, {"&#241;", "\xC3\xB1"},

        // NOTE: raw 2-byte UTF-8 accented characters are deliberately NOT rewritten
        // any more — they pass straight through to the font, which can now draw them.

        // UTF-8 smart punctuation (3-byte). Still folded to ASCII: these live in the
        // General Punctuation block (U+2000-206F), outside the fallback font's range.
        {"\xE2\x80\x98", "'"}, {"\xE2\x80\x99", "'"}, {"\xE2\x80\x9C", "\""},
        {"\xE2\x80\x9D", "\""}, {"\xE2\x80\x93", "-"}, {"\xE2\x80\x94", "--"},
        {"\xE2\x80\xA6", "..."},
        {"\xE2\x80\x9A", "'"},  {"\xE2\x80\x9B", "'"},   // low / reversed single quote
        {"\xE2\x80\x9E", "\""}, {"\xE2\x80\x9F", "\""},  // low / reversed double quote
        {"\xE2\x80\xB2", "'"},  {"\xE2\x80\xB3", "\""},  // prime, double prime
        {"\xE2\x80\x90", "-"},  {"\xE2\x80\x91", "-"},   // hyphen, non-breaking hyphen
        {"\xE2\x80\x92", "-"},  {"\xE2\x80\x95", "-"},   // figure dash, horizontal bar
        {"\xE2\x88\x92", "-"},                            // U+2212 minus sign

        // Special spaces and separators (Apple Music uses these in station names)
        {"\xC2\xA0", " "},       // Non-breaking space (U+00A0)
        {"\xE2\x80\x82", " "},   // En space (U+2002)
        {"\xE2\x80\x83", " "},   // Em space (U+2003)
        {"\xE2\x80\x89", " "},   // Thin space (U+2009)
        {"\xE2\x80\x8B", ""},    // Zero-width space (U+200B) - remove
        {"\xE2\x80\x8C", ""},    // Zero-width non-joiner (U+200C) - remove
        {"\xE2\x80\x8D", ""},    // Zero-width joiner (U+200D) - remove
        {"\xEF\xBB\xBF", ""}     // BOM (U+FEFF) - remove
    };

    // Single pass through replacements
    for (const auto& r : replacements) {
        text.replace(r.from, r.to);
    }

    return text;
}

// ============================================================================
// Playback Commands - With debounce
// ============================================================================
void SonosController::play() {
    CommandRequest_t cmd = { CMD_PLAY, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::pause() {
    CommandRequest_t cmd = { CMD_PAUSE, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::next() {
    uint32_t now = millis();
    if (now - lastCommandTime < SONOS_DEBOUNCE_MS) return;
    lastCommandTime = now;
    
    CommandRequest_t cmd = { CMD_NEXT, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::previous() {
    uint32_t now = millis();
    if (now - lastCommandTime < SONOS_DEBOUNCE_MS) return;
    lastCommandTime = now;
    
    CommandRequest_t cmd = { CMD_PREV, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::seek(int seconds) {
    CommandRequest_t cmd = { CMD_SEEK, seconds };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::setVolume(int vol) {
    vol = constrain(vol, 0, 100);
    CommandRequest_t cmd = { CMD_SET_VOLUME, vol };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::volumeUp(int step) {
    SonosDevice* d = getCurrentDevice();
    if (d) setVolume(d->volume + step);
}

void SonosController::volumeDown(int step) {
    SonosDevice* d = getCurrentDevice();
    if (d) setVolume(d->volume - step);
}

void SonosController::setMute(bool mute) {
    CommandRequest_t cmd = { CMD_SET_MUTE, mute ? 1 : 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::setShuffle(bool enable) {
    CommandRequest_t cmd = { CMD_SET_SHUFFLE, enable ? 1 : 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::setRepeat(const char* mode) {
    int v = 0;
    if (strcmp(mode, "ONE") == 0) v = 1;
    else if (strcmp(mode, "ALL") == 0) v = 2;
    CommandRequest_t cmd = { CMD_SET_REPEAT, v };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::playQueueItem(int index) {
    // index is 1-based queue position
    CommandRequest_t cmd = { CMD_PLAY_QUEUE_ITEM, index };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::requestQueueUpdate() {
    // Enqueue an async queue refresh — runs in network task with proper SDIO cooldowns.
    // Safe to call from UI thread (mainAppTask); updateQueue() must NOT be called directly
    // from the UI thread as it fires a 20KB SOAP response without mutex/cooldown protection.
    CommandRequest_t cmd = { CMD_UPDATE_QUEUE, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

bool SonosController::saveCurrentTrack(const char* playlistName) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[FAV] Device not available or not connected");
        return false;
    }

    Serial.printf("[FAV] Adding current track to: %s\n", playlistName);

    // Get current track number in queue
    int currentTrackNum = dev->currentTrackNumber;
    if (currentTrackNum <= 0) {
        Serial.println("[FAV] No valid track number");
        return false;
    }

    Serial.printf("[FAV] Current track number in queue: %d\n", currentTrackNum);

    // Browse the queue to get the current track WITH metadata
    String browseQueue = sendSOAP("ContentDirectory", "Browse",
        "<ObjectID>Q:0</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>1000</RequestedCount>"
        "<SortCriteria></SortCriteria>");

    // Extract DIDL from Result
    String queueDIDL = extractXML(browseQueue, "Result");

    // Decode HTML entities
    queueDIDL = decodeHTMLEntities(queueDIDL);

    // Find the item for current track number
    String trackMetadata = "";
    String trackURI = "";

    int pos = 0;
    int itemCount = 0;
    while ((pos = queueDIDL.indexOf("<item", pos)) >= 0) {
        itemCount++;

        if (itemCount == currentTrackNum) {
            int endPos = queueDIDL.indexOf("</item>", pos) + 7;
            String itemXML = queueDIDL.substring(pos, endPos);

            // Extract URI before encoding
            int resStart = itemXML.indexOf("<res");
            if (resStart >= 0) {
                int resEnd = itemXML.indexOf("</res>", resStart);
                int resContentStart = itemXML.indexOf(">", resStart) + 1;
                trackURI = itemXML.substring(resContentStart, resEnd);
            }

            // Re-encode for SOAP
            encodeXML(itemXML);

            trackMetadata = itemXML;

            Serial.printf("[FAV] Found track metadata, length: %d\n", trackMetadata.length());
            Serial.printf("[FAV] Track URI: %s\n", trackURI.c_str());
            break;
        }

        pos = queueDIDL.indexOf("</item>", pos) + 7;
    }

    if (trackMetadata.length() == 0 || trackURI.length() == 0) {
        Serial.println("[FAV] Could not find track in queue");
        return false;
    }

    // Check if playlist exists
    String browseResp = sendSOAP("ContentDirectory", "Browse",
        "<ObjectID>SQ:</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>100</RequestedCount>"
        "<SortCriteria></SortCriteria>");

    String didlContent = extractXML(browseResp, "Result");
    didlContent = decodeHTMLEntities(didlContent);

    String playlistID = "";
    pos = 0;
    while ((pos = didlContent.indexOf("<container", pos)) >= 0) {
        int endPos = didlContent.indexOf("</container>", pos);
        if (endPos < 0) break;

        String container = didlContent.substring(pos, endPos);

        int idPos = container.indexOf("id=\"");
        if (idPos < 0) { pos = endPos; continue; }  // malformed container — skip (M-5)
        int idStart = idPos + 4;
        int idEnd = container.indexOf("\"", idStart);
        if (idEnd < 0) { pos = endPos; continue; }
        String id = container.substring(idStart, idEnd);

        int titleStart = container.indexOf("<dc:title>") + 10;
        int titleEnd = container.indexOf("</dc:title>", titleStart);
        if (titleStart < 10 || titleEnd < 0) { pos = endPos; continue; }
        String title = container.substring(titleStart, titleEnd);

        if (title == playlistName) {
            playlistID = id;
            Serial.printf("[FAV] Found existing playlist: %s\n", playlistID.c_str());
            break;
        }

        pos = endPos;
    }

    // Create playlist if doesn't exist
    if (playlistID.length() == 0) {
        Serial.printf("[FAV] Creating playlist: %s\n", playlistName);

        String createArgs = "<InstanceID>0</InstanceID>"
                           "<Title>" + String(playlistName) + "</Title>"
                           "<EnqueuedURI></EnqueuedURI>"
                           "<EnqueuedURIMetaData></EnqueuedURIMetaData>";

        String createResp = sendSOAP("AVTransport", "CreateSavedQueue", createArgs.c_str());
        playlistID = extractXML(createResp, "AssignedObjectID");

        if (playlistID.length() == 0) {
            Serial.println("[FAV] Failed to create playlist");
            return false;
        }
    }

    // Get UpdateID
    String browsePlaylist = sendSOAP("ContentDirectory", "Browse",
        ("<ObjectID>" + playlistID + "</ObjectID>"
        "<BrowseFlag>BrowseMetadata</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>1</RequestedCount>"
        "<SortCriteria></SortCriteria>").c_str());

    String updateID = extractXML(browsePlaylist, "UpdateID");
    if (updateID.length() == 0) updateID = "0";

    // Both of these came out of a DIDL that decodeHTMLEntities() already decoded
    // above, so trackURI holds literal & (service URIs carry "?sid=...&flags=...")
    // and trackMetadata holds literal < and >. Dropped into the SOAP body raw
    // they produce malformed XML — the same defect that stopped containers and
    // service tracks playing, in a third place.
    String trackURIEnc = trackURI;
    encodeXML(trackURIEnc);
    String trackMetaEnc = trackMetadata;
    encodeXML(trackMetaEnc);

    // Add track with proper metadata
    String addArgs = "<InstanceID>0</InstanceID>"
                    "<ObjectID>" + playlistID + "</ObjectID>"
                    "<UpdateID>" + updateID + "</UpdateID>"
                    "<EnqueuedURI>" + trackURIEnc + "</EnqueuedURI>"
                    "<EnqueuedURIMetaData>" + trackMetaEnc + "</EnqueuedURIMetaData>"
                    "<AddAtIndex>4294967295</AddAtIndex>";

    String addResp = sendSOAP("AVTransport", "AddURIToSavedQueue", addArgs.c_str());

    if (addResp.length() > 0 && addResp.indexOf("Fault") < 0) {
        Serial.println("[FAV] Track added to playlist successfully!");
        return true;
    } else {
        Serial.println("[FAV] Failed to add track");
        return false;
    }
}

String SonosController::browseContent(const char* objectID, int startIndex, int count) {
    // Use static buffer to avoid String concatenation
    static char args[512];
    snprintf(args, sizeof(args),
        "<ObjectID>%s</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>%d</StartingIndex>"
        "<RequestedCount>%d</RequestedCount>"
        "<SortCriteria></SortCriteria>",
        objectID, startIndex, count);

    String resp = sendSOAP("ContentDirectory", "Browse", args);

    // Extract and decode DIDL. Auto-retry once on empty response (HTTP 500 transient).
    String didl = extractXML(resp, "Result");
    if (didl.length() == 0 && resp.length() == 0) {
        Serial.printf("[BROWSE] Empty response for %s — retrying\n", objectID);
        vTaskDelay(pdMS_TO_TICKS(500));
        resp = sendSOAP("ContentDirectory", "Browse", args);
        didl = extractXML(resp, "Result");
    }
    didl = decodeHTMLEntities(didl);

    return didl;
}

bool SonosController::playURI(const char* uri, const char* metadata) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[PLAY] Device not available");
        return false;
    }

    String metaEncoded = String(metadata);
    encodeXML(metaEncoded);

    // The URI needs escaping as much as the metadata does: service track URIs
    // carry query strings ("...?sid=284&flags=8&sn=2") and a literal & inside
    // <CurrentURI> breaks the SOAP envelope, which the player rejects with
    // UPnP 402. Same bug that stopped containers playing — it reaches any track
    // opened from a music service rather than the local library.
    String uriEncoded = String(uri);
    encodeXML(uriEncoded);

    // String rather than a fixed buffer: URI and metadata together are unbounded,
    // and a truncated SOAP body fails as malformed XML instead of as an error.
    String args = String("<InstanceID>0</InstanceID>")
                + "<CurrentURI>" + uriEncoded + "</CurrentURI>"
                + "<CurrentURIMetaData>" + metaEncoded + "</CurrentURIMetaData>";

    String resp = sendSOAP("AVTransport", "SetAVTransportURI", args.c_str());

    if (resp.length() > 0 && resp.indexOf("Fault") < 0) {
        // Auto-play after setting URI
        vTaskDelay(pdMS_TO_TICKS(200));
        play();
        return true;
    }

    return false;
}

bool SonosController::playPlaylist(const char* playlistID, const char* title) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[PLAYLIST] Device not available");
        return false;
    }

    Serial.printf("[PLAYLIST] Loading playlist: %s (%s)\n", playlistID, title);

    // Switch transport to queue mode BEFORE clearing/loading.
    // When a radio station is the current transport (x-rincon-mp3radio:// etc),
    // SetAVTransportURI(x-rincon-queue:...) returns HTTP 500 later in the flow
    // because Sonos hasn't released the radio transport yet. Switching to queue
    // mode first makes the subsequent SetAVTransportURI a no-op (already queue)
    // so it always succeeds.
    // Skip pre-switch entirely if already in queue mode — avoids a redundant SOAP
    // that could reset the transport position on devices that are already playing queue.
    {
        bool alreadyQueue = dev->currentURI.indexOf("rincon-queue") >= 0 ||
                            dev->currentURI.indexOf("x-rincon-queue") >= 0;
        if (alreadyQueue) {
            Serial.println("[PLAYLIST] Already in queue transport mode — skipping pre-switch");
        } else {
            static char switchArgs[256];
            static char switchURI[128];
            snprintf(switchURI, sizeof(switchURI), "x-rincon-queue:%s#0", dev->rinconID.c_str());
            snprintf(switchArgs, sizeof(switchArgs),
                "<InstanceID>0</InstanceID>"
                "<CurrentURI>%s</CurrentURI>"
                "<CurrentURIMetaData></CurrentURIMetaData>",
                switchURI);
            // 3-attempt retry with increasing delays. Sonos may need up to 2 attempts
            // to release the radio transport before accepting the queue switch.
            // Delays: 200ms, 350ms, 500ms — covers slow/overloaded speakers.
            static const int preDelays[] = {200, 350, 500};
            String preResp;
            for (int a = 0; a < 3; a++) {
                preResp = sendSOAP("AVTransport", "SetAVTransportURI", switchArgs);
                if (preResp.length() > 0 && preResp.indexOf("Fault") < 0) break;
                Serial.printf("[PLAYLIST] Pre-switch attempt %d failed — retrying\n", a + 1);
                vTaskDelay(pdMS_TO_TICKS(preDelays[a]));
            }
            if (preResp.length() == 0 || preResp.indexOf("Fault") >= 0) {
                Serial.println("[PLAYLIST] Pre-switch failed after 3 attempts — proceeding anyway");
            }
            // 300ms: Sonos may confirm transport switch (HTTP 200) before fully committing
            // queue mode internally. RemoveAllTracksFromQueue fired too soon can land in
            // the transition window and return 500. 300ms > typical Sonos async settle.
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    sendSOAP("AVTransport", "RemoveAllTracksFromQueue", "<InstanceID>0</InstanceID>");
    // 500ms: Sonos enters a brief transient state after RemoveAllTracksFromQueue
    // and returns HTTP 500 for subsequent AddURIToQueue if we fire too quickly.
    vTaskDelay(pdMS_TO_TICKS(500));

    String playlistNum = String(playlistID);
    playlistNum.replace("SQ:", "");

    static char playlistURI[128];
    snprintf(playlistURI, sizeof(playlistURI),
             "file:///jffs/settings/savedqueues.rsq#%s", playlistNum.c_str());

    // Sonos requires DIDL-Lite metadata in EnqueuedURIMetaData for playlist URIs.
    // Without it, AddURIToQueue returns a SOAP Fault and the playlist never loads.
    static char rawMeta[512];
    snprintf(rawMeta, sizeof(rawMeta),
        "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\""
        " xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\">"
        "<container id=\"%s\" parentID=\"SQ:\" restricted=\"false\">"
        "<dc:title>%s</dc:title>"
        "<upnp:class>object.container.playlistContainer</upnp:class>"
        "<res protocolInfo=\"x-rincon-playlist:*:*:*\">%s</res>"
        "</container>"
        "</DIDL-Lite>",
        playlistID, title, playlistURI);

    // encodeXML converts < > " & to &lt; &gt; &quot; &amp; so the DIDL
    // can be safely embedded as a SOAP field value.
    String metaEncoded = String(rawMeta);
    encodeXML(metaEncoded);

    static char addArgs[1024];
    snprintf(addArgs, sizeof(addArgs),
        "<InstanceID>0</InstanceID>"
        "<EnqueuedURI>%s</EnqueuedURI>"
        "<EnqueuedURIMetaData>%s</EnqueuedURIMetaData>"
        "<DesiredFirstTrackNumberEnqueued>1</DesiredFirstTrackNumberEnqueued>"
        "<EnqueueAsNext>0</EnqueueAsNext>",
        playlistURI, metaEncoded.c_str());

    Serial.printf("[PLAYLIST] Adding to queue: %s\n", playlistURI);

    // 3-retry loop: Sonos may return HTTP 500 (transient) briefly after
    // RemoveAllTracksFromQueue even with the 500ms delay on slow devices.
    String resp;
    for (int attempt = 0; attempt < 3; attempt++) {
        resp = sendSOAP("AVTransport", "AddURIToQueue", addArgs);
        if (resp.length() > 0 && resp.indexOf("Fault") < 0) {
            break;
        }
        Serial.printf("[PLAYLIST] AddURIToQueue attempt %d failed, retrying\n", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(400));
    }

    if (resp.length() > 0 && resp.indexOf("Fault") < 0) {
        vTaskDelay(pdMS_TO_TICKS(200));

        static char queueURI[128];
        static char setArgs[256];
        snprintf(queueURI, sizeof(queueURI), "x-rincon-queue:%s#0", dev->rinconID.c_str());
        snprintf(setArgs, sizeof(setArgs),
            "<InstanceID>0</InstanceID>"
            "<CurrentURI>%s</CurrentURI>"
            "<CurrentURIMetaData></CurrentURIMetaData>",
            queueURI);

        // 3-retry loop: SetAVTransportURI can return HTTP 500 when Sonos is still
        // processing the AddURIToQueue expansion internally (radio → queue transition).
        // Identical pattern to AddURIToQueue retry above.
        String setResp;
        for (int attempt = 0; attempt < 3; attempt++) {
            setResp = sendSOAP("AVTransport", "SetAVTransportURI", setArgs);
            if (setResp.length() > 0 && setResp.indexOf("Fault") < 0) {
                break;
            }
            Serial.printf("[PLAYLIST] SetAVTransportURI attempt %d failed, retrying\n", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(400));
        }
        if (setResp.length() == 0 || setResp.indexOf("Fault") >= 0) {
            Serial.println("[PLAYLIST] Failed to set transport URI");
            return false;
        }

        Serial.println("[PLAYLIST] Playlist loaded and playing");
        vTaskDelay(pdMS_TO_TICKS(100));
        // Seek to track 1 before Play — queue was just rebuilt from scratch so position
        // may be at 0/undefined; without Seek, Sonos may start from a stale position.
        sendSOAP("AVTransport", "Seek",
            "<InstanceID>0</InstanceID><Unit>TRACK_NR</Unit><Target>1</Target>");
        vTaskDelay(pdMS_TO_TICKS(100));
        sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
        vTaskDelay(pdMS_TO_TICKS(300));
        updateTrackInfo();
        updateQueue();
        return true;
    }

    Serial.println("[PLAYLIST] Failed to add playlist to queue");
    return false;
}

bool SonosController::playContainer(const char* containerURI, const char* metadata) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[CONTAINER] Device not available");
        return false;
    }

    Serial.printf("[CONTAINER] Loading container: %s\n", containerURI);

    String metaDecoded = decodeHTMLEntities(String(metadata));

    String metaEncoded = metaDecoded;
    encodeXML(metaEncoded);

    Serial.printf("[CONTAINER] Metadata: %s\n", metaDecoded.c_str());

    // A container CANNOT be the transport. Setting one directly was tried first
    // here and Sonos rejects it outright — measured against a real player:
    //
    //     SetAVTransportURI(x-rincon-cpcontainer:...) -> HTTP 500, UPnP 714
    //                                                   "Illegal MIME-type"
    //
    // and the rejection left the transport unsettled, so the AddURIToQueue that
    // followed it — single-shot, no retry — hit the settling window and failed
    // too, reporting "both methods failed" for what is really one wrong call.
    // The same AddURIToQueue succeeds first time when it is not preceded by the
    // doomed one.
    //
    // So: no direct-transport attempt. Load the container into the queue and
    // point the transport at the queue, which is exactly what playPlaylist()
    // does and has always worked. The retry/delay constants below are that
    // function's, for the same reason: Sonos returns a transient 500 for a
    // while after each transport operation.

    // Pre-switch to queue mode if something else (radio, line-in) holds the
    // transport, or RemoveAllTracksFromQueue below can land mid-transition.
    {
        bool alreadyQueue = dev->currentURI.indexOf("rincon-queue") >= 0;
        if (!alreadyQueue) {
            String pre = "<InstanceID>0</InstanceID><CurrentURI>x-rincon-queue:"
                       + dev->rinconID + "#0</CurrentURI><CurrentURIMetaData></CurrentURIMetaData>";
            static const int preDelays[] = {200, 350, 500};
            for (int a = 0; a < 3; a++) {
                String r = sendSOAP("AVTransport", "SetAVTransportURI", pre.c_str());
                if (r.length() > 0 && r.indexOf("Fault") < 0) break;
                vTaskDelay(pdMS_TO_TICKS(preDelays[a]));
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    // Replace the queue rather than appending: tapping a favourite means "play
    // this now", not "add it after the 500 tracks already queued".
    sendSOAP("AVTransport", "RemoveAllTracksFromQueue", "<InstanceID>0</InstanceID>");
    vTaskDelay(pdMS_TO_TICKS(500));

    // String, not a fixed char[]: favourite metadata is unbounded — this one
    // encodes to ~700 bytes and a larger one would have silently truncated
    // against the 1024-byte buffer that used to be here, producing malformed
    // XML rather than an error.
    // The URI must be XML-escaped too, not just the metadata. Service URIs carry
    // query strings — this one is "...?sid=284&flags=72&sn=2" — and a literal &
    // inside <EnqueuedURI> breaks the SOAP envelope. Measured on a real player:
    //     URI raw      -> HTTP 500, UPnP 402 (Invalid Args)
    //     URI escaped  -> HTTP 200
    // Only the metadata was ever escaped here, which is why no container with a
    // query string could be played.
    String uriEncoded = String(containerURI);
    encodeXML(uriEncoded);

    String addArgs = String("<InstanceID>0</InstanceID>")
                   + "<EnqueuedURI>" + uriEncoded + "</EnqueuedURI>"
                   + "<EnqueuedURIMetaData>" + metaEncoded + "</EnqueuedURIMetaData>"
                   + "<DesiredFirstTrackNumberEnqueued>1</DesiredFirstTrackNumberEnqueued>"
                   + "<EnqueueAsNext>0</EnqueueAsNext>";

    String resp;
    for (int attempt = 0; attempt < 3; attempt++) {
        resp = sendSOAP("AVTransport", "AddURIToQueue", addArgs.c_str());
        if (resp.length() > 0 && resp.indexOf("Fault") < 0) break;
        Serial.printf("[CONTAINER] AddURIToQueue attempt %d failed, retrying\n", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(400));
    }

    if (resp.length() == 0 || resp.indexOf("Fault") >= 0) {
        Serial.println("[CONTAINER] Failed to add container to queue");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    String queueArgs = String("<InstanceID>0</InstanceID><CurrentURI>x-rincon-queue:")
                     + dev->rinconID + "#0</CurrentURI><CurrentURIMetaData></CurrentURIMetaData>";
    String setResp;
    for (int attempt = 0; attempt < 3; attempt++) {
        setResp = sendSOAP("AVTransport", "SetAVTransportURI", queueArgs.c_str());
        if (setResp.length() > 0 && setResp.indexOf("Fault") < 0) break;
        Serial.printf("[CONTAINER] SetAVTransportURI attempt %d failed, retrying\n", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    if (setResp.length() == 0 || setResp.indexOf("Fault") >= 0) {
        Serial.println("[CONTAINER] Failed to point transport at the queue");
        return false;
    }

    // Seek before Play: the queue was just rebuilt, so the position is stale.
    vTaskDelay(pdMS_TO_TICKS(100));
    sendSOAP("AVTransport", "Seek",
             "<InstanceID>0</InstanceID><Unit>TRACK_NR</Unit><Target>1</Target>");
    vTaskDelay(pdMS_TO_TICKS(100));
    sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
    vTaskDelay(pdMS_TO_TICKS(300));
    updateTrackInfo();
    updateQueue();
    Serial.println("[CONTAINER] Container loaded and playing");
    return true;
}

String SonosController::listMusicServices() {
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return "";

    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400/MusicServices/Control", dev->ip.toString().c_str());

    const char* body = "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:ListAvailableServices xmlns:u=\"urn:schemas-upnp-org:service:MusicServices:1\">"
        "</u:ListAvailableServices></s:Body></s:Envelope>";

    const char* soapAction = "\"urn:schemas-upnp-org:service:MusicServices:1#ListAvailableServices\"";

    HTTPClient http;
    http.begin(url);
    http.setTimeout(3000);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", soapAction);

    int code = http.POST(body);
    String resp = "";

    if (code == 200) {
        resp = http.getString();
    } else {
        Serial.printf("[SERVICES] HTTP error %d\n", code);
    }

    http.end();
    return resp;
}

String SonosController::getCurrentTrackInfo() {
    String resp = sendSOAP("AVTransport", "GetPositionInfo", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return "";

    // Extract track URI
    String uri = extractXML(resp, "TrackURI");

    // Extract metadata
    String metadata = extractXML(resp, "TrackMetaData");

    // Decode HTML entities in metadata
    metadata = decodeHTMLEntities(metadata);

    // Format output for serial monitor
    String result = "===== TRACK URI =====\n" + uri +
                    "\n\n===== TRACK METADATA =====\n" + metadata +
                    "\n=====================";

    Serial.println("[CAPTURE] " + result);
    return result;
}

int SonosController::getVolume() {
    SonosDevice* d = getCurrentDevice();
    return d ? d->volume : 0;
}

bool SonosController::getMute() {
    SonosDevice* d = getCurrentDevice();
    return d ? d->isMuted : false;
}

// ============================================================================
// State Updates
// ============================================================================
void SonosController::notifyUI(UIUpdateType_e type) {
    UIUpdate_t upd = { type, "" };
    xQueueSend(uiUpdateQueue, &upd, 0);
}

// Helper: Detect if URI is a radio station
// Based on research: x-sonosapi-stream:, x-rincon-mp3radio:, x-sonosapi-radio:, hls-radio:
// x-sonosapi-hls: = BBC Sounds live radio (NOT x-sonosapi-hls-static: which is on-demand podcasts)
// NOTE: aac:// is intentionally NOT here — Apple Music tracks also use aac:// URIs when played
// from a queue. Radio detection for aac:// is handled at the call site using queueSize.
static bool isRadioURI(const String& uri) {
    return uri.startsWith("x-sonosapi-stream:") ||
           uri.startsWith("x-rincon-mp3radio:") ||
           uri.startsWith("x-sonosapi-radio:") ||
           uri.startsWith("x-sonosapi-hls:") ||
           uri.startsWith("hls-radio:");
}

// Helper: Parse r:streamContent for current song info
// Formats: "Artist - Title" OR "TYPE=SNG|TITLE xxx|ARTIST xxx|ALBUM xxx"
// Based on research from node-sonos Issue #106 and OpenHAB Issue #13208
static void parseStreamContent(const String& content, String& outArtist, String& outTitle) {
    if (content.length() == 0) return;

    // Format 1: Pipe-delimited (SiriusXM, Apple Music)
    // Example: "BR P|TYPE=SNG|TITLE Talk To Me|ARTIST Kopecky"
    if (content.indexOf("TYPE=") >= 0 && content.indexOf("|") > 0) {
        // Extract TITLE
        int titleIdx = content.indexOf("TITLE ");
        if (titleIdx >= 0) {
            titleIdx += 6; // Skip "TITLE "
            int titleEnd = content.indexOf("|", titleIdx);
            if (titleEnd < 0) titleEnd = content.length();
            outTitle = content.substring(titleIdx, titleEnd);
            outTitle.trim();
        }

        // Extract ARTIST
        int artistIdx = content.indexOf("ARTIST ");
        if (artistIdx >= 0) {
            artistIdx += 7; // Skip "ARTIST "
            int artistEnd = content.indexOf("|", artistIdx);
            if (artistEnd < 0) artistEnd = content.length();
            outArtist = content.substring(artistIdx, artistEnd);
            outArtist.trim();
        }
    }
    // Format 2: Simple "Artist - Title" (TuneIn)
    else if (content.indexOf(" - ") > 0) {
        int sepIdx = content.indexOf(" - ");
        outArtist = content.substring(0, sepIdx);
        outTitle = content.substring(sepIdx + 3);
        outArtist.trim();
        outTitle.trim();
    }
    // Format 3: Plain text - use as title
    else {
        outTitle = content;
        outTitle.trim();
    }
}

bool SonosController::updateTrackInfo() {
    String resp = sendSOAP("AVTransport", "GetPositionInfo", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        // Get current track number
        String trackNum = extractXML(resp, "Track");
        if (trackNum.length() > 0) {
            dev->currentTrackNumber = trackNum.toInt();
        }

        dev->relTime = extractXML(resp, "RelTime");
        dev->relTimeSeconds = timeToSeconds(dev->relTime);

        dev->trackDuration = extractXML(resp, "TrackDuration");
        dev->durationSeconds = timeToSeconds(dev->trackDuration);

        // Extract TrackURI and detect radio
        String trackURI = extractXML(resp, "TrackURI");
        dev->currentURI = trackURI;
        // aac:// is used by both live AAC radio streams AND Apple Music/streaming service
        // tracks played from a queue. Treat as radio only when the queue is empty.
        dev->isLineIn       = trackURI.startsWith("x-rincon-stream:");
        dev->isTvAudio      = trackURI.startsWith("x-sonos-htastream:");
        dev->isRadioStation = !dev->isLineIn && !dev->isTvAudio && (isRadioURI(trackURI) ||
                              (trackURI.startsWith("aac://") && dev->queueSize == 0));

        // Get metadata and decode HTML entities
        String meta = extractXML(resp, "TrackMetaData");
        meta = decodeHTML(meta);

        // Extract r:streamContent for radio (contains current song info)
        String streamContent = extractXML(meta, "r:streamContent");
        streamContent = decodeHTML(streamContent);
        dev->streamContent = streamContent;

        String newTrack = decodeHTML(extractXML(meta, "dc:title"));
        String newArtist = decodeHTML(extractXML(meta, "dc:creator"));
        String newAlbum = decodeHTML(extractXML(meta, "upnp:album"));

        // For radio: parse streamContent for current song info
        // streamContent overrides dc:title/dc:creator when available
        if (dev->isRadioStation && streamContent.length() > 0) {
            String parsedArtist = "";
            String parsedTitle = "";
            parseStreamContent(streamContent, parsedArtist, parsedTitle);

            // Use parsed info if we got something useful
            if (parsedTitle.length() > 0) {
                newTrack = parsedTitle;
            }
            if (parsedArtist.length() > 0) {
                newArtist = parsedArtist;
            }
        }

        // Extract album art URL
        String art = extractXML(meta, "upnp:albumArtURI");
        art = decodeHTML(art);

        String newArtURL = "";
        if (art.length() > 0) {
            if (art.startsWith("/")) {
                // Local Sonos path - convert to full URL
                newArtURL = "http://" + dev->ip.toString() + ":1400" + art;
            } else {
                newArtURL = art;
            }
        }

        // Check if anything actually changed
        bool changed = (newTrack != dev->currentTrack) ||
                       (newArtist != dev->currentArtist) ||
                       (newAlbum != dev->currentAlbum) ||
                       (newArtURL != dev->albumArtURL);

        // Update values
        dev->currentTrack = newTrack;
        dev->currentArtist = newArtist;
        dev->currentAlbum = newAlbum;
        dev->albumArtURL = newArtURL;

        xSemaphoreGive(deviceMutex);

        // Only notify UI if something changed
        if (changed) {
            notifyUI(UPDATE_TRACK_INFO);
        }

        return true;
    }
    return false;
}

// Get station name for radio from GetMediaInfo
// For radio: CurrentURIMetaData contains the actual station name
// For music: This returns queue/playlist info (less useful)
bool SonosController::updateMediaInfo() {
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;

    // Only fetch media info for radio stations
    if (!dev->isRadioStation) {
        // Clear radio station info when not playing radio
        if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
            dev->radioStationName = "";
            dev->radioStationArtURL = "";
            xSemaphoreGive(deviceMutex);
        }
        return true;
    }

    String resp = sendSOAP("AVTransport", "GetMediaInfo", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return false;

    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        // Extract CurrentURIMetaData
        String meta = extractXML(resp, "CurrentURIMetaData");
        meta = decodeHTML(meta);

        // Extract station name from dc:title
        String stationName = extractXML(meta, "dc:title");
        stationName = decodeHTML(stationName);

        // Extract station logo from upnp:albumArtURI
        String stationArt = extractXML(meta, "upnp:albumArtURI");
        stationArt = decodeHTML(stationArt);
        Serial.printf("[RADIO] Extracted albumArtURI: '%s'\n", stationArt.c_str());

        // Store station name if valid (not URL junk)
        if (stationName.length() > 0) {
            // Filter out URL junk
            bool isJunk = (stationName.indexOf("?") > 0 ||
                          stationName.indexOf(".mp3") > 0 ||
                          stationName.indexOf(".m3u8") > 0 ||
                          stationName.indexOf("accessKey=") > 0);

            if (!isJunk) {
                dev->radioStationName = stationName;
            }
        }

        // Store station logo URL
        if (stationArt.length() > 0) {
            if (stationArt.startsWith("/")) {
                // Local Sonos path - convert to full URL
                dev->radioStationArtURL = "http://" + dev->ip.toString() + ":1400" + stationArt;
            } else {
                dev->radioStationArtURL = stationArt;
            }
            Serial.printf("[RADIO] Set radioStationArtURL: '%s'\n", dev->radioStationArtURL.c_str());
        } else {
            Serial.println("[RADIO] No station art found in metadata");
        }

        xSemaphoreGive(deviceMutex);
        return true;
    }
    return false;
}

bool SonosController::updatePlaybackState() {
    String resp = sendSOAP("AVTransport", "GetTransportInfo", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        dev->isPlaying = (resp.indexOf("PLAYING") > 0);
        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_PLAYBACK_STATE);
        return true;
    }
    return false;
}

bool SonosController::updateVolume() {
    String resp = sendSOAP("RenderingControl", "GetVolume", 
        "<InstanceID>0</InstanceID><Channel>Master</Channel>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        String vol = extractXML(resp, "CurrentVolume");
        if (vol.length() > 0) dev->volume = vol.toInt();
        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_VOLUME);
        return true;
    }
    return false;
}

bool SonosController::updateTransportSettings() {
    String resp = sendSOAP("AVTransport", "GetTransportSettings", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        String mode = extractXML(resp, "PlayMode");
        dev->shuffleMode = (mode.indexOf("SHUFFLE") >= 0);
        
        if (mode.indexOf("REPEAT_ONE") >= 0) dev->repeatMode = "ONE";
        else if (mode.indexOf("REPEAT") >= 0) dev->repeatMode = "ALL";
        else dev->repeatMode = "NONE";
        
        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_TRANSPORT);
        return true;
    }
    return false;
}

bool SonosController::updateQueue(int startIndex) {
    // SONOS_QUEUE_BATCH_SIZE=10 → ~4KB response, ~3 WiFi RX buffers.
    // Was 50 items → ~20KB, 14 TCP segs, all 32 WiFi RX buffers (~51KB DMA) — never released.
    // startIndex: 0-based offset into queue. For a window centred on currentTrackNumber:
    //   startIndex = max(0, currentTrackNumber - SONOS_QUEUE_BATCH_SIZE/2)
    // so the view shows ~5 tracks before and ~5 after the currently playing track.
    if (startIndex < 0) startIndex = 0;

    String queueArgs =
        "<ObjectID>Q:0</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>" + String(startIndex) + "</StartingIndex>"
        "<RequestedCount>" + String(SONOS_QUEUE_BATCH_SIZE) + "</RequestedCount>"
        "<SortCriteria></SortCriteria>";

    size_t dma_pre_q = heap_caps_get_free_size(MALLOC_CAP_DMA);
    String resp = sendSOAP("ContentDirectory", "Browse", queueArgs.c_str());
    size_t dma_post_q = heap_caps_get_free_size(MALLOC_CAP_DMA);

    // Stamp IMMEDIATELY after sendSOAP returns, before any XML parsing.
    // The art task reads last_queue_fetch_time in sdioPreWait and may be scheduled
    // between the sendSOAP return and the stamp — leaving a window where art sees
    // a stale timestamp and fires a download concurrent with the SOAP residue.
    // Stamping here closes that window to the minimum possible (a few instructions).
    // Also stamps on empty response: even a failed SOAP leaves TCP residue in SDIO.
    last_queue_fetch_time = millis();

    Serial.printf("[QUEUE/DMA] pre=%uKB post=%uKB delta=%+dB start=%d batch=%d\n",
                  (unsigned)(dma_pre_q / 1024), (unsigned)(dma_post_q / 1024),
                  (int)((long)dma_post_q - (long)dma_pre_q), startIndex, SONOS_QUEUE_BATCH_SIZE);

    if (resp.length() == 0) {
        Serial.printf("[SONOS] Queue response empty\n");
        return false;
    }

    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
        String total = extractXML(resp, "TotalMatches");
        if (total.length() > 0) {
            dev->totalTracks = total.toInt();
        }
        
        String numReturned = extractXML(resp, "NumberReturned");
        Serial.printf("[SONOS] Queue: total=%d, returned=%s\n", dev->totalTracks, numReturned.c_str());
        
        // Get the Result which contains DIDL-Lite
        String result = extractXML(resp, "Result");
        result = decodeHTML(result);
        
        dev->queueSize = 0;
        int pos = 0;
        
        while (dev->queueSize < QUEUE_ITEMS_MAX && pos < (int)result.length()) {
            int itemStart = result.indexOf("<item", pos);
            if (itemStart < 0) break;

            int itemEnd = result.indexOf("</item>", itemStart);
            if (itemEnd < 0) break;

            // Use range-based extraction to avoid creating substring copy
            dev->queue[dev->queueSize].title = decodeHTML(extractXMLRange(result, "dc:title", itemStart, itemEnd));
            dev->queue[dev->queueSize].artist = decodeHTML(extractXMLRange(result, "dc:creator", itemStart, itemEnd));
            dev->queue[dev->queueSize].album = decodeHTML(extractXMLRange(result, "upnp:album", itemStart, itemEnd));
            dev->queue[dev->queueSize].albumArtURL = decodeHTML(extractXMLRange(result, "upnp:albumArtURI", itemStart, itemEnd));
            dev->queue[dev->queueSize].trackNumber = startIndex + dev->queueSize + 1;  // 1-based absolute position
            dev->queueSize++;

            pos = itemEnd + 7;
        }
        
        Serial.printf("[SONOS] Parsed %d queue items\n", dev->queueSize);

        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_QUEUE);
        return true;
    }
    return false;
}

// ============================================================================
// Command Processing
// ============================================================================
void SonosController::processCommand(CommandRequest_t* cmd) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return;

    // Static buffer to avoid heap allocation for each command
    static char args[256];

    switch (cmd->type) {
        case CMD_PLAY:
            sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isPlaying = true;
                xSemaphoreGive(deviceMutex);
            }
            notifyUI(UPDATE_PLAYBACK_STATE);
            break;

        case CMD_PAUSE:
            sendSOAP("AVTransport", "Pause", "<InstanceID>0</InstanceID>");
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isPlaying = false;
                xSemaphoreGive(deviceMutex);
            }
            notifyUI(UPDATE_PLAYBACK_STATE);
            break;

        case CMD_NEXT:
            // Suppress pollingTask before the SOAP fires: art_download_in_progress=true blocks
            // the early-exit guard. Without this, pollingTask races through during the 200ms
            // vTaskDelay and fires GetPositionInfo concurrently with the Next SOAP + subsequent
            // updateTrackInfo SOAP — 3 simultaneous TCP teardowns overflow C6 pkt_rxbuff → :928.
            art_download_in_progress = true;
            sendSOAP("AVTransport", "Next", "<InstanceID>0</InstanceID>");
            vTaskDelay(pdMS_TO_TICKS(200));
            updateTrackInfo();  // sets pending_art_url → requestAlbumArt() keeps flag true
            break;

        case CMD_PREV:
            art_download_in_progress = true;  // same race fix as CMD_NEXT
            sendSOAP("AVTransport", "Previous", "<InstanceID>0</InstanceID>");
            vTaskDelay(pdMS_TO_TICKS(200));
            updateTrackInfo();
            break;

        case CMD_SET_VOLUME:
            snprintf(args, sizeof(args),
                "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>%d</DesiredVolume>",
                cmd->value);
            sendSOAP("RenderingControl", "SetVolume", args);
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->volume = cmd->value;
                xSemaphoreGive(deviceMutex);
            }
            break;

        case CMD_SET_MUTE:
            snprintf(args, sizeof(args),
                "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredMute>%d</DesiredMute>",
                cmd->value);
            sendSOAP("RenderingControl", "SetMute", args);
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isMuted = (cmd->value == 1);
                xSemaphoreGive(deviceMutex);
            }
            break;

        case CMD_SET_SHUFFLE: {
            const char* mode = (cmd->value == 1) ? "SHUFFLE" : "NORMAL";
            snprintf(args, sizeof(args),
                "<InstanceID>0</InstanceID><NewPlayMode>%s</NewPlayMode>", mode);
            sendSOAP("AVTransport", "SetPlayMode", args);
            updateTransportSettings();
            break;
        }

        case CMD_SET_REPEAT: {
            const char* mode = "NORMAL";
            if (cmd->value == 1) mode = "REPEAT_ONE";
            else if (cmd->value == 2) mode = "REPEAT_ALL";
            snprintf(args, sizeof(args),
                "<InstanceID>0</InstanceID><NewPlayMode>%s</NewPlayMode>", mode);
            sendSOAP("AVTransport", "SetPlayMode", args);
            updateTransportSettings();
            break;
        }

        case CMD_SEEK: {
            int h = cmd->value / 3600;
            int m = (cmd->value % 3600) / 60;
            int s = cmd->value % 60;
            snprintf(args, sizeof(args),
                "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>%02d:%02d:%02d</Target>",
                h, m, s);
            sendSOAP("AVTransport", "Seek", args);
            break;
        }

        case CMD_PLAY_QUEUE_ITEM: {
            Serial.printf("[CMD] PLAY_QUEUE_ITEM: track=%d | heap=%u dma=%u\n",
                cmd->value,
                heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                heap_caps_get_free_size(MALLOC_CAP_DMA));
            art_download_in_progress = true;  // same race fix: suppress polling during Seek+Play+settle
            snprintf(args, sizeof(args),
                "<InstanceID>0</InstanceID><Unit>TRACK_NR</Unit><Target>%d</Target>",
                cmd->value);
            sendSOAP("AVTransport", "Seek", args);
            vTaskDelay(pdMS_TO_TICKS(100));
            sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isPlaying = true;
                xSemaphoreGive(deviceMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            updateTrackInfo();
            break;
        }

        case CMD_UPDATE_QUEUE: {
            // Triggered by the queue screen refresh button — runs here in the network task,
            // NOT on the UI/mainAppTask thread, so SDIO cooldowns and mutex are handled properly.
            updateQueue();
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// Background Tasks - Faster polling
// ============================================================================
void SonosController::networkTaskFunction(void* param) {
    SonosController* ctrl = (SonosController*)param;
    CommandRequest_t cmd;

    Serial.printf("[SONOS] Network task started\n");

    while (1) {
        // Check if shutdown requested (for OTA update)
        if (sonos_tasks_shutdown_requested) {
            Serial.println("[SONOS] Network task shutdown requested - exiting");
            ctrl->networkTaskHandle = NULL;
            vTaskDelete(NULL);
            return;
        }

        if (xQueueReceive(ctrl->commandQueue, &cmd, pdMS_TO_TICKS(20))) {
            ctrl->processCommand(&cmd);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void SonosController::pollingTaskFunction(void* param) {
    SonosController* ctrl = (SonosController*)param;
    uint32_t tick = 0;
    uint32_t reconnectTick = 0;

    Serial.printf("[SONOS] Polling task started\n");

    // Initial queue load
    vTaskDelay(pdMS_TO_TICKS(1000));
    ctrl->updateQueue();

    // Track previous URI to detect station changes
    static String previousURI = "";

    while (1) {
        // Check if shutdown requested (for OTA update)
        if (sonos_tasks_shutdown_requested) {
            Serial.println("[SONOS] Polling task shutdown requested - exiting");
            ctrl->pollingTaskHandle = NULL;
            vTaskDelete(NULL);
            return;
        }

        SonosDevice* dev = ctrl->getCurrentDevice();

        // Auto-reconnect when disconnected
        if (dev && !dev->connected) {
            reconnectTick++;
            // Try to reconnect every 2 seconds (7 * 300ms)
            if (reconnectTick % 7 == 0) {
                Serial.println("[SONOS] Attempting to reconnect...");
                dev->errorCount = 0;  // Reset error count
                ctrl->updateTrackInfo();  // Try to fetch data
                if (dev->connected) {
                    Serial.println("[SONOS] Reconnected successfully!");
                    ctrl->updateQueue();  // Refresh queue on reconnect
                }
            }
        } else {
            reconnectTick = 0;
        }

        if (dev && dev->connected) {
            // ── Per-cycle DMA snapshot ────────────────────────────────────────────
            // Log DMA at the START of every poll cycle (before any SOAP fires).
            // This pinpoints exactly when/where the mystery 68KB drop occurs.
            {
                static size_t cycle_session_start = 0;
                static uint32_t cycle_count = 0;
                size_t dma_cycle = heap_caps_get_free_size(MALLOC_CAP_DMA);
                if (cycle_session_start == 0) cycle_session_start = dma_cycle;
                cycle_count++;
                int cycle_delta = (int)((long)dma_cycle - (long)cycle_session_start);
                // Log: every cycle when DMA < 60KB (danger zone), else every 20 cycles
                if (dma_cycle < 60000 || cycle_count % 20 == 0) {
                    Serial.printf("[POLL/DMA] cycle=%u dma=%uKB session=%+dKB art_dl=%d\n",
                                  (unsigned)cycle_count,
                                  (unsigned)(dma_cycle / 1024),
                                  cycle_delta / 1024,
                                  (int)art_download_in_progress);
                }
            }
            // ── SDIO early-exit guard ─────────────────────────────────────────────
            // 500-storm early-exit was removed (caused SDIO idle → DMA clock-gate);
            // 500 protection is handled by the inside-mutex 200ms post-500 drain in art,
            // art_download_in_progress suppression during downloads, and the post-SOAP-1
            // (in_500_now) guard skipping updatePlaybackState below.
            {
                bool post_download  = last_art_download_end_ms > 0 &&
                                      millis() - last_art_download_end_ms < SDIO_INTER_DOWNLOAD_MS;
                bool track_settling = last_track_change_ms > 0 &&
                                      millis() - last_track_change_ms < SDIO_TRACK_CHANGE_SETTLE_MS;

                // Art downloading (or post-download residue): skip ALL SOAPs, short sleep.
                if (art_download_in_progress || post_download || track_settling) {
                    static unsigned long last_poll_skip_log = 0;
                    if (millis() - last_poll_skip_log > 2000) {
                        Serial.printf("[POLL] Skip: art_dl=%d post_dl=%d settling=%d\n",
                            (int)art_download_in_progress, (int)post_download, (int)track_settling);
                        last_poll_skip_log = millis();
                    }
                    vTaskDelay(pdMS_TO_TICKS(track_settling ? 1000 : POLL_BASE_INTERVAL_MS));
                    continue;
                }
            }
            // ─────────────────────────────────────────────────────────────────────

            // Track info every cycle for instant updates when changing sources
            ctrl->updateTrackInfo();

            // ── Post-SOAP-1 guard ─────────────────────────────────────────────────
            // updateTrackInfo() calls onSonosUpdate() which may set pending_art_url
            // synchronously (same task). If a track just changed, skip GetTransportInfo
            // this cycle — its SOAP response + UPnP NOTIFY burst overlap in pkt_rxbuff.
            // Guard uses pending != last (not just isEmpty) so it clears once the art
            // task downloads and syncs them. isEmpty() never cleared → d->isPlaying stuck.
            // in_500_now: also skip GetTransportInfo when GetPositionInfo just returned
            // 500 — both SOAP responses + Sonos NOTIFYs in same cycle overflow pkt_rxbuff.
            // Timeout (10s): if art permanently fails (DMA floor), guard would fire forever
            // → updatePlaybackState() never runs → progress bar freezes. After 10s we give
            // up waiting and let GetTransportInfo through so playback state stays live.
            {
                bool in_500_now = last_transient_500_ms > 0 &&
                                  millis() - last_transient_500_ms < SDIO_STORM_COOLDOWN_MS;
                bool art_still_pending = isAlbumArtPending() &&
                                         last_track_change_ms > 0 &&
                                         millis() - last_track_change_ms < 10000;
                if (art_still_pending || in_500_now) {
                    tick++;
                    vTaskDelay(pdMS_TO_TICKS(POLL_BASE_INTERVAL_MS));
                    continue;
                }
            }

            ctrl->updatePlaybackState();

            // ── On-demand queue window fetch ──────────────────────────────────────
            // Placed here (after both mandatory SOAPs, BEFORE mid-cycle guard) so
            // it executes even during stable playback when pending==last_art_url.
            // Two triggers:
            //   (a) User opens queue screen / taps refresh → ev_queue() sets flag
            //   (b) Track number changed → auto-set below so Next Up stays current
            {
                static int lastQueuedTrackNum = -1;
                // Auto-trigger when track number changes (keeps Next Up populated)
                if (!dev->isRadioStation && dev->currentTrackNumber > 0 &&
                    dev->currentTrackNumber != lastQueuedTrackNum) {
                    int half  = SONOS_QUEUE_BATCH_SIZE / 2;
                    int start = dev->currentTrackNumber - half;
                    if (start < 0) start = 0;
                    if (dev->totalTracks > 0 && start + SONOS_QUEUE_BATCH_SIZE > dev->totalTracks)
                        start = dev->totalTracks - SONOS_QUEUE_BATCH_SIZE;
                    if (start < 0) start = 0;
                    queue_fetch_start_index = start;
                    queue_fetch_requested   = true;
                    lastQueuedTrackNum = dev->currentTrackNumber;
                }

                if (queue_fetch_requested) {
                    queue_fetch_requested = false;
                    size_t dma_now = heap_caps_get_free_size(MALLOC_CAP_DMA);
                    if (dma_now >= ART_MIN_DMA_PRE_BURST) {
                        ctrl->updateQueue(queue_fetch_start_index);
                        vTaskDelay(pdMS_TO_TICKS(SDIO_POST_QUEUE_DRAIN_MS));
                    } else {
                        Serial.printf("[POLL] Queue fetch deferred: DMA too low (%uKB)\n",
                                      (unsigned)(dma_now / 1024));
                        queue_fetch_requested = true;  // retry next cycle
                    }
                }
            }

            // ── Mid-cycle guard ───────────────────────────────────────────────────
            // Skip optional SOAPs while a new track's art is still pending download.
            // Guard clears once the art task downloads and syncs pending_art_url=last_art_url.
            // Uses != last_art_url (NOT !isEmpty()): isEmpty() is true for any stream with
            // art → would block volume/transport/queue polling forever during stable playback.
            // The vTaskDelay is OUTSIDE the connected-device block (bottom of the while
            // loop), so `continue` here skips it entirely — this guard must delay itself
            // or it spins the optional-SOAP section with zero inter-cycle pause (the exact
            // SOAP storm the rest of this architecture exists to prevent). The sibling
            // guards above both delay before continuing.
            // Bounded by the same 10s window as the guard above: art abort paths leave
            // art_download_in_progress set deliberately, and isAlbumArtPending() also
            // returns true on art_mutex timeout — without a bound, either one stalls
            // volume/transport/queue polling forever.
            if ((art_download_in_progress || isAlbumArtPending()) &&
                last_track_change_ms > 0 && millis() - last_track_change_ms < 10000) {
                tick++;
                vTaskDelay(pdMS_TO_TICKS(POLL_BASE_INTERVAL_MS));
                continue;
            }
            // ─────────────────────────────────────────────────────────────────────

            // Detect station change and fetch station name immediately
            if (dev->isRadioStation && dev->currentURI != previousURI) {
                Serial.printf("[RADIO] Station changed - fetching station name immediately\n");
                ctrl->updateMediaInfo();
                previousURI = dev->currentURI;
                vTaskDelay(pdMS_TO_TICKS(200));  // Allow network to recover after GetMediaInfo
            }

            // Media info for radio (station name) periodic refresh
            if (tick % POLL_MEDIA_INFO_MODULO == 0 && dev->isRadioStation) {
                ctrl->updateMediaInfo();
                vTaskDelay(pdMS_TO_TICKS(200));
            }

            // Clear previous URI when not on radio
            if (!dev->isRadioStation && previousURI.length() > 0) {
                previousURI = "";
            }

            // Volume polling
            if (tick % POLL_VOLUME_MODULO == 0) {
                ctrl->updateVolume();
            }

            // Transport settings polling
            if (tick % POLL_TRANSPORT_MODULO == 0) {
                ctrl->updateTransportSettings();
            }

            // Background queue refresh every POLL_QUEUE_MODULO cycles (60s).
            // Uses windowed fetch centred on currentTrackNumber — same window as on-demand.
            if (tick % POLL_QUEUE_MODULO == 0 && !dev->isRadioStation) {
                size_t dma_now = heap_caps_get_free_size(MALLOC_CAP_DMA);
                if (dma_now >= ART_MIN_DMA_PRE_BURST) {
                    int half  = SONOS_QUEUE_BATCH_SIZE / 2;
                    int start = (dev->currentTrackNumber > 0) ? (dev->currentTrackNumber - half) : 0;
                    if (start < 0) start = 0;
                    if (dev->totalTracks > 0 && start + SONOS_QUEUE_BATCH_SIZE > dev->totalTracks)
                        start = dev->totalTracks - SONOS_QUEUE_BATCH_SIZE;
                    if (start < 0) start = 0;
                    ctrl->updateQueue(start);
                    vTaskDelay(pdMS_TO_TICKS(SDIO_POST_QUEUE_DRAIN_MS));
                } else {
                    Serial.printf("[POLL] Queue poll skipped: DMA too low (%uKB < %uKB)\n",
                                  (unsigned)(dma_now / 1024), (unsigned)(ART_MIN_DMA_PRE_BURST / 1024));
                }
            }

            tick++;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_BASE_INTERVAL_MS));
    }
}

void SonosController::handleNetworkError(const char* msg) {
    Serial.printf("[SONOS] Error: %s\n", msg);
}

void SonosController::resetErrorCount() {
    SonosDevice* dev = getCurrentDevice();
    if (dev) dev->errorCount = 0;
}

void SonosController::suspendTasks() {
    // Request clean shutdown and WAIT for tasks to exit
    // This ensures HTTPClient destructors run and SDIO buffers are freed properly
    Serial.println("[SONOS] Requesting background tasks to stop...");
    sonos_tasks_shutdown_requested = true;

    // Wait up to 5 seconds for tasks to exit cleanly
    int wait_count = 0;
    while ((pollingTaskHandle != NULL || networkTaskHandle != NULL) && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }

    // Force-delete any tasks that didn't exit in time
    bool forced = false;
    if (pollingTaskHandle != NULL) {
        Serial.println("[SONOS] WARNING: Force-deleting polling task (didn't exit in time)");
        vTaskDelete(pollingTaskHandle);
        pollingTaskHandle = NULL;
        forced = true;
    }
    if (networkTaskHandle != NULL) {
        Serial.println("[SONOS] WARNING: Force-deleting network task (didn't exit in time)");
        vTaskDelete(networkTaskHandle);
        networkTaskHandle = NULL;
        forced = true;
    }

    // H-5: a force-deleted task may have died mid-sendSOAP/updateTrackInfo holding
    // deviceMutex, poisoning it (all SonosDevice access would deadlock after resumeTasks).
    // Both background tasks are now gone and no other task takes deviceMutex during the
    // OTA shutdown window, so recreate it from a clean state. (network_mutex is recreated
    // unconditionally by the OTA caller, which also covers the force-killed clock-bg task.)
    if (forced && deviceMutex != NULL) {
        vSemaphoreDelete(deviceMutex);
        deviceMutex = xSemaphoreCreateMutex();
        Serial.println("[SONOS] deviceMutex recreated after force-delete (clean state)");
    }

    Serial.println("[SONOS] ✓ Background tasks stopped, WiFi buffers freed");
}

void SonosController::resumeTasks() {
    // Recreate tasks after failed OTA (successful OTA reboots device)
    Serial.println("[SONOS] Recreating background tasks");
    sonos_tasks_shutdown_requested = false;  // Reset shutdown flag
    startTasks();  // This will recreate polling and network tasks
    Serial.println("[SONOS] ✓ Background tasks recreated");
}

// ============================================================================
// Group Management
// ============================================================================

bool SonosController::joinGroup(int deviceIndex, int coordinatorIndex) {
    if (deviceIndex < 0 || deviceIndex >= deviceCount) return false;
    if (coordinatorIndex < 0 || coordinatorIndex >= deviceCount) return false;
    if (deviceIndex == coordinatorIndex) return false;  // Can't join self

    SonosDevice* device = &devices[deviceIndex];
    SonosDevice* coordinator = &devices[coordinatorIndex];

    if (coordinator->rinconID.length() == 0) {
        Serial.println("[GROUP] Coordinator has no RINCON ID");
        return false;
    }

    // Build the x-rincon URI to join the coordinator
    char uri[128];
    snprintf(uri, sizeof(uri), "x-rincon:%s", coordinator->rinconID.c_str());

    // Send SetAVTransportURI to the device we want to join
    // This tells it to follow the coordinator's playback
    char args[256];
    snprintf(args, sizeof(args),
        "<InstanceID>0</InstanceID>"
        "<CurrentURI>%s</CurrentURI>"
        "<CurrentURIMetaData></CurrentURIMetaData>",
        uri);

    // Send to the device being joined, addressed explicitly (H-7 — no index swap).
    String resp = sendSOAP(device, "AVTransport", "SetAVTransportURI", args);

    bool success = (resp.length() > 0 && resp.indexOf("Fault") < 0);

    if (success) {
        Serial.printf("[GROUP] %s joined group with coordinator %s\n",
            device->roomName.c_str(), coordinator->roomName.c_str());

        // Update group info
        if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
            device->groupCoordinatorUUID = coordinator->rinconID;
            device->isGroupCoordinator = false;
            coordinator->isGroupCoordinator = true;
            xSemaphoreGive(deviceMutex);
        }

        notifyUI(UPDATE_GROUPS);
    } else {
        Serial.printf("[GROUP] Failed to join %s to group\n", device->roomName.c_str());
    }

    return success;
}

bool SonosController::leaveGroup(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= deviceCount) return false;

    SonosDevice* device = &devices[deviceIndex];

    // BecomeCoordinatorOfStandaloneGroup makes the device leave its group
    // and become a standalone player. Addressed explicitly (H-7 — no index swap).
    String resp = sendSOAP(device, "AVTransport", "BecomeCoordinatorOfStandaloneGroup",
        "<InstanceID>0</InstanceID>");

    bool success = (resp.length() > 0 && resp.indexOf("Fault") < 0);

    if (success) {
        Serial.printf("[GROUP] %s left group (now standalone)\n", device->roomName.c_str());

        if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
            device->groupCoordinatorUUID = "";
            device->isGroupCoordinator = true;  // Standalone = coordinator of self
            device->groupMemberCount = 1;
            xSemaphoreGive(deviceMutex);
        }

        notifyUI(UPDATE_GROUPS);
    } else {
        Serial.printf("[GROUP] Failed to remove %s from group\n", device->roomName.c_str());
    }

    return success;
}

void SonosController::updateGroupInfo() {
    // Query each device for its group coordinator
    for (int i = 0; i < deviceCount; i++) {
        SonosDevice* dev = &devices[i];

        // GetMediaInfo contains the current transport URI which tells us about grouping.
        // Addressed explicitly (H-7 — no currentDeviceIndex swap that polling could read).
        String resp = sendSOAP(dev, "AVTransport", "GetMediaInfo", "<InstanceID>0</InstanceID>");

        if (resp.length() > 0) {
            String currentURI = extractXML(resp, "CurrentURI");

            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
                // Check if this device is following another (x-rincon:RINCON_xxx format)
                if (currentURI.startsWith("x-rincon:")) {
                    // Extract coordinator RINCON from URI
                    dev->groupCoordinatorUUID = currentURI.substring(9);  // Skip "x-rincon:"
                    dev->isGroupCoordinator = false;
                } else {
                    // Not following anyone - either standalone or is a coordinator
                    dev->groupCoordinatorUUID = dev->rinconID;  // Self
                    dev->isGroupCoordinator = true;
                }
                xSemaphoreGive(deviceMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // Small delay between queries
    }

    // Now count members for each coordinator
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].isGroupCoordinator) {
            int count = 1;  // Count self
            for (int j = 0; j < deviceCount; j++) {
                if (i != j && devices[j].groupCoordinatorUUID == devices[i].rinconID) {
                    count++;
                }
            }
            devices[i].groupMemberCount = count;
        } else {
            devices[i].groupMemberCount = 0;  // Non-coordinators don't have members
        }
    }

    notifyUI(UPDATE_GROUPS);
}

int SonosController::getGroupMemberCount(int coordinatorIndex) {
    if (coordinatorIndex < 0 || coordinatorIndex >= deviceCount) return 0;

    SonosDevice* coordinator = &devices[coordinatorIndex];
    if (!coordinator->isGroupCoordinator) return 0;

    int count = 1;  // Count coordinator itself
    for (int i = 0; i < deviceCount; i++) {
        if (i != coordinatorIndex &&
            devices[i].groupCoordinatorUUID == coordinator->rinconID) {
            count++;
        }
    }
    return count;
}

bool SonosController::isDeviceInGroup(int deviceIndex, int coordinatorIndex) {
    if (deviceIndex < 0 || deviceIndex >= deviceCount) return false;
    if (coordinatorIndex < 0 || coordinatorIndex >= deviceCount) return false;

    if (deviceIndex == coordinatorIndex) return true;  // Coordinator is in own group

    SonosDevice* device = &devices[deviceIndex];
    SonosDevice* coordinator = &devices[coordinatorIndex];

    return (device->groupCoordinatorUUID == coordinator->rinconID);
}
