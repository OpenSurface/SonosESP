/**
 * Streaming-service browsing over SMAPI — see include/music_services.h for the
 * overall flow, and include/smapi.h for the protocol pieces (host-tested).
 *
 * This file is the part that cannot run on the host: the UPnP event capture,
 * the decryption, the HTTPS transport and the worker task.
 */

#include "music_services.h"
#include "smapi.h"
#include "ui_common.h"          // network_mutex, last_*_ms, art/clock/ota flags, sonos
#include "ui_network_guard.h"   // sdioPreWait
#include "config.h"
#include "clock_screen.h"       // CLOCK_ZONES / clock_tz_idx — IANA zone for <context>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <MD5Builder.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <new>

MusicServices musicServices;

// ============================================================================
// Shared state. Everything below is guarded by lock_, except where noted.
// ============================================================================

namespace {

struct Entry {
    smapi::Account acct;
    smapi::Service svc;
    std::string    label;
    bool           manifestTried = false;   // content endpoint looked up (see contentEndpoint)
    std::string    contentUri;              // the manifest's "browse" endpoint, if any
    // Search categories from the presentation map, fetched on first use.
    enum { CATS_UNTRIED, CATS_LOADING, CATS_READY, CATS_FAILED } catsState = CATS_UNTRIED;
    std::vector<smapi::SearchCategory> cats;
};

// Accounts and the identity the requests are made under. Rewritten by the
// worker on load, read by the UI through sources()/buildPlayback().
std::vector<Entry> g_entries;
std::string g_household;     // "Sonos_..."
std::string g_deviceId;      // the speaker's R_TrialZPSerial
std::string g_zonePlayer;    // the speaker's RINCON id
String      g_lastError;

// Request slots. A new browse request replaces one that has not started yet:
// the user only ever waits on the most recent tap.
bool        g_loadPending  = false;
bool        g_browsePending = false;
uint32_t    g_browseReqId  = 0;
int         g_browseSid    = 0, g_browseSerial = 0, g_browseIndex = 0, g_browseCount = 0;
String      g_browseObjId;
uint32_t    g_nextReqId    = 1;
bool        g_catsPending  = false;   // a presentation map to fetch
int         g_catsSid      = 0, g_catsSerial = 0;

// The finished page, waiting for the UI to take it.
SvcPage*    g_donePage     = nullptr;

// AES salt from the desktop controller (the "2:" account envelope).
const uint8_t kAccountSalt[16] = {0x1a, 0x01, 0xa7, 0x31, 0xc9, 0x6e, 0x9e, 0xbd,
                                  0xe8, 0x47, 0x51, 0x82, 0xb2, 0x74, 0xb7, 0x0e};

// The user agent the reference flow uses. Some providers check the controller
// identity string, so it is copied verbatim rather than tidied up.
const char* kUserAgent = "Linux UPnP/1.0 Sonos/90.0-77070 (WDCR:Microsoft Windows NT 10.0.19045 64-bit)";

void copyStr(char* dst, size_t n, const std::string& src) {
    size_t len = src.size() < n - 1 ? src.size() : n - 1;
    memcpy(dst, src.data(), len);
    dst[len] = '\0';
}

// Stable per-panel controller id in UUID form, derived from the MAC.
std::string controllerId() {
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    char buf[40];
    snprintf(buf, sizeof(buf), "53584553-5350-4000-8000-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

std::string timeZoneName() {
    if (clock_tz_idx >= 0 && clock_tz_idx < CLOCK_ZONES_COUNT) return CLOCK_ZONES[clock_tz_idx].name;
    return "UTC";
}

bool findEntry(int sid, int serial, Entry* out) {
    for (const auto& e : g_entries) {
        if (e.acct.serviceId == sid && e.acct.serial == serial) { *out = e; return true; }
    }
    return false;
}

}  // namespace

// ============================================================================
// Account capture — one UPnP subscription to ZoneGroupTopology
// ============================================================================
// The NOTIFY arrives as an inbound connection, which the rest of this firmware
// deliberately never accepts (config.h: SDIO crash defence — NOTIFY bursts on
// top of other traffic overflow the C6 rx buffer). So it is done once, briefly,
// with network_mutex held for the whole exchange: nothing else is on the radio
// while the NOTIFY body streams in, and the subscription is cancelled the
// moment the initial event has been read.

// Reads one HTTP request (NOTIFY) from `c` into *body. Handles Content-Length
// and chunked bodies; gives up at the deadline.
static bool readNotify(WiFiClient& c, String* body, uint32_t deadline) {
    String raw;
    int headerEnd = -1;
    long contentLength = -1;
    bool chunked = false;
    static char buf[1024];

    while ((int32_t)(deadline - millis()) > 0) {
        int avail = c.available();
        if (avail <= 0) {
            if (!c.connected()) break;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        int n = c.read((uint8_t*)buf, avail < (int)sizeof(buf) ? avail : (int)sizeof(buf));
        if (n <= 0) continue;
        if (raw.length() + n > SVC_EVENT_MAX_BYTES) {
            Serial.println("[SVC] NOTIFY too large - dropped");
            return false;
        }
        raw.concat(buf, n);

        if (headerEnd < 0) {
            headerEnd = raw.indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
                String headers = raw.substring(0, headerEnd);
                headers.toLowerCase();
                int cl = headers.indexOf("content-length:");
                if (cl >= 0) contentLength = headers.substring(cl + 15).toInt();
                chunked = headers.indexOf("transfer-encoding: chunked") >= 0 ||
                          headers.indexOf("transfer-encoding:chunked") >= 0;
            }
        }
        if (headerEnd >= 0 && contentLength >= 0 &&
            (long)raw.length() - (headerEnd + 4) >= contentLength) break;
        if (headerEnd >= 0 && chunked && raw.endsWith("0\r\n\r\n")) break;
    }
    if (headerEnd < 0) return false;

    String b = raw.substring(headerEnd + 4);
    if (chunked) {
        String out;
        int p = 0;
        while (p < (int)b.length()) {
            int le = b.indexOf("\r\n", p);
            if (le < 0) break;
            long sz = strtol(b.substring(p, le).c_str(), nullptr, 16);
            if (sz <= 0) break;
            out += b.substring(le + 2, le + 2 + sz);
            p = le + 2 + sz + 2;
        }
        b = out;
    }
    *body = b;
    return true;
}

static bool captureAccountsEvent(IPAddress speaker, String* encrypted, String* err) {
    sdioPreWait("SVC");
    if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
        *err = "Network busy";
        return false;
    }
    {
        unsigned long elapsed = millis() - last_network_end_ms;
        if (last_network_end_ms > 0 && elapsed < SDIO_GENERAL_COOLDOWN_MS)
            vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - elapsed));
    }

    WiFiServer server(SVC_EVENT_PORT);
    server.begin();

    char url[96];
    snprintf(url, sizeof(url), "http://%s:1400/ZoneGroupTopology/Event", speaker.toString().c_str());
    String callback = "<http://" + WiFi.localIP().toString() + ":" + String(SVC_EVENT_PORT) + "/svc>";

    String sid;
    {
        HTTPClient http;
        http.begin(url);
        http.setReuse(false);
        http.setTimeout(3000);
        http.addHeader("CALLBACK", callback);
        http.addHeader("NT", "upnp:event");
        http.addHeader("TIMEOUT", "Second-60");
        const char* keys[] = {"SID"};
        http.collectHeaders(keys, 1);
        int code = http.sendRequest("SUBSCRIBE");
        if (code == 200) sid = http.header("SID");
        else Serial.printf("[SVC] SUBSCRIBE failed: HTTP %d\n", code);
        http.end();
    }

    bool found = false;
    if (sid.length()) {
        uint32_t deadline = millis() + SVC_EVENT_WAIT_MS;
        while (!found && (int32_t)(deadline - millis()) > 0) {
            WiFiClient c = server.accept();
            if (!c) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
            String body;
            bool ok = readNotify(c, &body, deadline);
            c.print("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            vTaskDelay(pdMS_TO_TICKS(10));
            c.stop();
            if (!ok) continue;
            Serial.printf("[SVC] NOTIFY received (%u bytes)\n", (unsigned)body.length());
            int s = body.indexOf("<ThirdPartyMediaServersX>");
            int e = body.indexOf("</ThirdPartyMediaServersX>");
            if (s >= 0 && e > s) {
                *encrypted = body.substring(s + 25, e);
                found = true;
            }
        }

        HTTPClient http;
        http.begin(url);
        http.setReuse(false);
        http.setTimeout(2000);
        http.addHeader("SID", sid);
        int code = http.sendRequest("UNSUBSCRIBE");
        if (code != 200) Serial.printf("[SVC] UNSUBSCRIBE: HTTP %d (expires in 60s anyway)\n", code);
        http.end();
    }
    server.end();

    vTaskDelay(pdMS_TO_TICKS(SDIO_TCP_CLOSE_MS));
    last_network_end_ms = millis();
    xSemaphoreGive(network_mutex);

    if (!sid.length())  { *err = "Speaker refused the event subscription"; return false; }
    if (!found)         { *err = "No account data from the speaker"; return false; }
    return true;
}

// ============================================================================
// Decryption of the "2:" account envelope
// ============================================================================
// base64(iv[16] + AES-128-CBC(payload + md5(payload)[0:4], PKCS#7)), with
//     key = md5(iv + md5(householdId + salt))
// Same format the desktop controller uses; see credentials.py in the reference.

static void md5Of(const uint8_t* a, size_t na, const uint8_t* b, size_t nb, uint8_t out[16]) {
    MD5Builder md5;
    md5.begin();
    md5.add(a, na);
    if (b && nb) md5.add(b, nb);
    md5.calculate();
    md5.getBytes(out);
}

static bool decryptAccounts(const String& encoded, const std::string& household,
                            std::string* out, String* err) {
    std::string enc = smapi::xmlUnescape(encoded.c_str());
    std::string clean;
    clean.reserve(enc.size());
    for (char c : enc) if (c != ' ' && c != '\r' && c != '\n' && c != '\t') clean += c;
    if (clean.compare(0, 2, "2:") != 0) { *err = "Unknown account format"; return false; }

    size_t srcLen = clean.size() - 2;
    size_t cap = srcLen * 3 / 4 + 4;
    uint8_t* raw = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_8BIT);
    uint8_t* plain = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_8BIT);
    if (!raw || !plain) {
        free(raw); free(plain);
        *err = "Out of memory";
        return false;
    }

    bool ok = false;
    size_t rawLen = 0;
    do {
        if (mbedtls_base64_decode(raw, cap, &rawLen, (const uint8_t*)clean.data() + 2, srcLen) != 0) {
            *err = "Account data is not base64";
            break;
        }
        if (rawLen < 32 || (rawLen - 16) % 16) { *err = "Account data has a bad size"; break; }

        uint8_t globalKey[16], blobKey[16], iv[16];
        md5Of((const uint8_t*)household.data(), household.size(), kAccountSalt, sizeof(kAccountSalt),
              globalKey);
        md5Of(raw, 16, globalKey, 16, blobKey);
        memcpy(iv, raw, 16);

        const size_t ctLen = rawLen - 16;
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        int rc = mbedtls_aes_setkey_dec(&aes, blobKey, 128);
        if (rc == 0) rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ctLen, iv, raw + 16, plain);
        mbedtls_aes_free(&aes);
        if (rc != 0) { *err = "Account decryption failed"; break; }

        uint8_t pad = plain[ctLen - 1];
        if (pad < 1 || pad > 16) { *err = "Wrong household key"; break; }
        bool padOk = true;
        for (size_t i = ctLen - pad; i < ctLen; i++) if (plain[i] != pad) padOk = false;
        if (!padOk || ctLen - pad < 4) { *err = "Wrong household key"; break; }

        size_t payloadLen = ctLen - pad - 4;
        uint8_t check[16];
        md5Of(plain, payloadLen, nullptr, 0, check);
        if (memcmp(check, plain + payloadLen, 4) != 0) { *err = "Account integrity check failed"; break; }

        out->assign((const char*)plain, payloadLen);
        ok = true;
    } while (false);

    // The plaintext holds every account token in the household — wipe it.
    memset(plain, 0, cap);
    free(raw);
    free(plain);
    return ok;
}

// ============================================================================
// HTTPS transport — the lyrics.cpp envelope, applied to SMAPI
// ============================================================================

typedef std::vector<std::pair<std::string, std::string>> Headers;

// ── TLS keep-alive ───────────────────────────────────────────────────────────
// One TLS session kept open between requests to the same host. Measured on the
// bench, a tap spent ~3.4s in the HTTPS cooldown and ~0.25s in the handshake,
// against ~0.3s of server time. That cooldown exists for the residue of a TLS
// TEARDOWN (SDIO_HTTPS_COOLDOWN_MS); a reused session has had no teardown, so
// it is skipped. The price is ~32KB of DMA-capable RAM held while the session
// is open, so it is closed once idle, when free DMA runs low, or as soon as an
// art download, clock photo or OTA wants the radio.
//
// The HTTPClient is kept too: its destructor stop()s the client it was given.
// Worker task only - no lock beyond network_mutex.
static WiFiClientSecure* g_tls  = nullptr;
static HTTPClient*       g_http = nullptr;
static std::string       g_tlsHost;
static uint16_t          g_tlsPort    = 0;
static uint32_t          g_tlsLastUse = 0;

// Caller holds network_mutex. Same teardown envelope as a one-shot request.
static void tlsDropLocked(const char* why) {
    if (!g_tls) return;
    delete g_http;          // stop()s the client
    g_http = nullptr;
    g_tls->stop();          // frees the mbedTLS record buffers (~32KB DMA)
    delete g_tls;
    g_tls = nullptr;
    vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_TCP_CLOSE_MS));
    last_network_end_ms = millis();
    last_https_end_ms   = millis();
    Serial.printf("[SVC] TLS session closed (%s) | dma=%u\n", why,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
}

static bool tlsOpen() { return g_tls != nullptr; }

// Called by the worker between jobs, every SVC_KEEPALIVE_TICK_MS while open.
static void tlsIdleTick() {
    if (!g_tls) return;
    const bool idle    = millis() - g_tlsLastUse > SVC_KEEPALIVE_MS;
    const bool lowDma  = heap_caps_get_free_size(MALLOC_CAP_DMA) < SVC_KEEPALIVE_MIN_DMA;
    const bool wanted  = art_download_in_progress || clock_photo_in_progress || ota_in_progress;
    if (!idle && !lowDma && !wanted) return;
    if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(1000))) return;   // next tick
    tlsDropLocked(idle ? "idle" : lowDma ? "low DMA" : "radio wanted");
    xSemaphoreGive(network_mutex);
}

// One HTTPS exchange under the full SDIO envelope. `body` null means GET.
// `label` is only for the log.
static bool httpsRequest(const std::string& url, const char* label, const std::string* body,
                         const Headers& headers, int* code, std::string* resp, String* err) {
    if (ota_in_progress) { *err = "Busy updating"; return false; }

    std::string host;
    uint16_t port = 443;
    {
        size_t hs = url.find("://");
        hs = hs == std::string::npos ? 0 : hs + 3;
        size_t he = url.find_first_of(":/", hs);
        host = url.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
        if (he != std::string::npos && url[he] == ':') port = (uint16_t)atoi(url.c_str() + he + 1);
    }

    const uint32_t t_enter = millis();
    for (int attempt = 0; attempt < 3; attempt++) {
        bool reuse = g_tls && g_tlsHost == host && g_tlsPort == port &&
                     millis() - g_tlsLastUse < SVC_KEEPALIVE_MS;
        if (g_tls && !reuse) {
            // Another host, or stale: close it first, so the cooldown below
            // covers its teardown like any other.
            if (xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
                tlsDropLocked("replaced");
                xSemaphoreGive(network_mutex);
            }
        }

        // An art download in flight plus our TLS handshake burst is the
        // pkt_rxbuff overflow this firmware is built around avoiding. Wait it out.
        uint32_t waitStart = millis();
        while ((art_download_in_progress || clock_photo_in_progress) &&
               millis() - waitStart < SVC_ART_WAIT_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        sdioPreWait("SVC", reuse ? 0 : SDIO_WAIT_HTTPS_COOLDOWN);

        if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
            *err = "Network busy";
            return false;
        }
        if (art_download_in_progress || clock_photo_in_progress) {
            // Started while we waited for the mutex: give the radio up, and the
            // session with it - art checks DMA before its burst.
            tlsDropLocked("radio wanted");
            xSemaphoreGive(network_mutex);
            continue;
        }
        if (reuse && !g_tls->connected()) {
            tlsDropLocked("closed by server");
            reuse = false;
        }
        {
            unsigned long now = millis();
            unsigned long eNet = now - last_network_end_ms, eHttps = now - last_https_end_ms;
            if (last_network_end_ms > 0 && eNet < SDIO_GENERAL_COOLDOWN_MS)
                vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - eNet));
            if (!reuse && last_https_end_ms > 0 && eHttps < SDIO_HTTPS_TCP_CLOSE_MS)
                vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_TCP_CLOSE_MS - eHttps));
        }
        if (!reuse) {
            size_t dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
            if (dma < SVC_MIN_FREE_DMA) {
                xSemaphoreGive(network_mutex);
                Serial.printf("[SVC] DMA too low for TLS (%u < %u)\n", (unsigned)dma,
                              (unsigned)SVC_MIN_FREE_DMA);
                *err = "Low memory, try again";
                return false;
            }
        }

        uint32_t t0 = millis();
        // Phase timings, to tell the provider's think time from our own receive
        // path: wait (cooldowns + mutex), tls (TCP + handshake, 0 when reused),
        // ttfb (request sent until the headers are in), body (reading it).
        uint32_t t_tls = 0, t_ttfb = 0, t_body = 0;
        int hdr_len = 0;

        if (!reuse) {
            g_tls = new WiFiClientSecure();
            // Same posture as lyrics, art, weather and OTA. The token sent here
            // is one any device on the LAN can already read from the speaker.
            g_tls->setInsecure();
            g_tls->setHandshakeTimeout(SVC_HANDSHAKE_TIMEOUT_S);
            uint32_t t_c = millis();
            bool up = g_tls->connect(host.c_str(), port, SVC_CONNECT_TIMEOUT_MS);
            t_tls = millis() - t_c;
            if (!up) {
                delete g_tls;
                g_tls = nullptr;
                vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_TCP_CLOSE_MS));
                last_network_end_ms = millis();
                last_https_end_ms   = millis();
                xSemaphoreGive(network_mutex);
                Serial.printf("[SVC/T] %s connect failed after %lums\n", label, (unsigned long)t_tls);
                *err = "Could not reach the service";
                return false;
            }
            g_http = new HTTPClient();
            g_tlsHost = host;
            g_tlsPort = port;
        }

        HTTPClient& http = *g_http;
        // begin() on the same host keeps the connected client; end() below
        // clears the request headers and leaves the socket open if the server
        // allows it (HTTP/1.1, no "Connection: close").
        http.begin(*g_tls, url.c_str());
        http.setConnectTimeout(SVC_CONNECT_TIMEOUT_MS);
        http.setTimeout(SVC_HTTP_TIMEOUT_MS);
        http.setReuse(true);
        // The content home page comes back chunked, and reading a chunked body
        // over a kept-alive TLS session sat on read timeouts: 24KB took 41s on
        // the square panel. HTTP/1.0 forbids chunking and closes the session
        // after the body; it is one request per session start.
        http.useHTTP10(!strcmp(label, "content home"));
        http.setUserAgent(kUserAgent);
        for (const auto& h : headers) http.addHeader(h.first.c_str(), h.second.c_str());

        uint32_t t_r = millis();
        *code = body ? http.POST((uint8_t*)body->data(), body->size()) : http.GET();
        t_ttfb = millis() - t_r;
        if (*code > 0) {
            int len = http.getSize();
            hdr_len = len;
            uint32_t t_b = millis();
            if (len > SVC_MAX_BODY_BYTES) {
                Serial.printf("[SVC] %s response too large (%d)\n", label, len);
                *code = -100;
            } else if (len > 0) {
                // Known length: take what has arrived, never ask for more.
                // HTTPClient::getString() reads through readBytes(), which over
                // TLS waited out the whole read timeout whenever available()
                // promised more than one read could return: 14KB getMetadata
                // bodies took 10s and 20s, holding network_mutex throughout, so
                // every speaker request in the meantime failed.
                WiFiClient* st = http.getStreamPtr();
                resp->resize(len);
                int got = 0;
                const uint32_t deadline = millis() + SVC_HTTP_TIMEOUT_MS;
                while (st && got < len && (int32_t)(millis() - deadline) < 0) {
                    int n = st->read((uint8_t*)&(*resp)[got], len - got);
                    if (n > 0) { got += n; continue; }
                    if (!st->connected() && st->available() <= 0) break;
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
                resp->resize(got);
                if (got < len) Serial.printf("[SVC] %s body cut short: %d of %d bytes\n", label, got, len);
            } else {
                String s = http.getString();   // chunked or close-delimited
                resp->assign(s.c_str(), s.length());
            }
            t_body = millis() - t_b;
        }
        http.end();
        size_t dma_open = heap_caps_get_free_size(MALLOC_CAP_DMA);
        const bool kept = *code > 0 && *code != -100 && g_tls->connected();
        if (kept) {
            g_tlsLastUse = millis();
            last_network_end_ms = millis();
        } else {
            tlsDropLocked(*code > 0 ? "not reusable" : "request failed");
        }
        xSemaphoreGive(network_mutex);

        Serial.printf("[SVC] %s -> HTTP %d, %u bytes in %lums\n", label, *code,
                      (unsigned)resp->size(), (unsigned long)(millis() - t0));
        // One greppable line per request. len=-1 means chunked / no
        // Content-Length; rate is the body's effective throughput.
        Serial.printf("[SVC/T] %s %s wait=%lu tls=%lu ttfb=%lu body=%lu bytes=%u len=%d rate=%luKB/s "
                      "dma=%u rssi=%d ap=%s\n",
                      label, reuse ? "REUSED" : "new", (unsigned long)(t0 - t_enter), (unsigned long)t_tls,
                      (unsigned long)t_ttfb, (unsigned long)t_body, (unsigned)resp->size(), hdr_len,
                      t_body ? (unsigned long)(resp->size() / t_body) : 0UL, (unsigned)dma_open,
                      (int)WiFi.RSSI(), WiFi.BSSIDstr().c_str());
        if (*code <= 0 && *code != -100 && reuse && attempt < 2) {
            // A reused session the server had quietly dropped: once more, fresh.
            resp->clear();
            continue;
        }
        if (*code == -100) { *err = "Response too large"; return false; }
        if (*code <= 0)    { *err = "Could not reach the service"; return false; }
        return true;
    }
    *err = "Network busy";
    return false;
}

static const std::string& ctrlId() {
    static std::string id = controllerId();
    return id;
}

// SMAPI is SOAP over HTTPS POST.
static bool httpsPost(const std::string& url, const char* action, const std::string& body,
                      const std::string& bearer, int* code, std::string* resp, String* err) {
    Headers h;
    h.push_back({"Content-Type", "text/xml; charset=\"utf-8\""});
    h.push_back({"SOAPACTION", std::string("\"") + smapi::NS + "#" + action + "\""});
    h.push_back({"Accept-Language", "en-US"});
    h.push_back({"X-Sonos-Controller-ID", ctrlId()});
    if (!bearer.empty()) h.push_back({"Authorization", "Bearer " + bearer});
    return httpsRequest(url, action, &body, h, code, resp, err);
}

// The account and the identity a request is made under, snapshotted together.
static bool loadCredentials(int sid, int serial, Entry* e, smapi::Credentials* c, String* err,
                            SemaphoreHandle_t lock) {
    std::string household, deviceId, zonePlayer;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool have = findEntry(sid, serial, e);
    household = g_household; deviceId = g_deviceId; zonePlayer = g_zonePlayer;
    xSemaphoreGive(lock);
    if (!have) { *err = "Service is no longer linked"; return false; }
    if (e->acct.token == "needs_reauth") { *err = "Sign in again in the Sonos app"; return false; }

    c->caps = e->svc.caps;
    c->auth = e->svc.auth;
    c->deviceId = deviceId;
    c->zonePlayerId = zonePlayer;
    c->householdId = smapi::scopedHouseholdId(household, e->acct.udn);
    c->token = e->acct.token;
    c->key = e->acct.key;
    c->username = e->acct.username;
    c->password = e->acct.password;
    c->timeZone = timeZoneName();
    return true;
}

// New token/key for an account whose token expired. Uses the pair a fault
// handed back when there is one, else asks with refreshAuthToken. Memory only:
// the speaker keeps and refreshes its own copy, as with the desktop controller.
static bool refreshCredentials(int sid, int serial, const Entry& e, const smapi::Credentials& c,
                               const smapi::Fault* fault, String* err, SemaphoreHandle_t lock) {
    std::string tok = fault ? fault->newToken : "", key = fault ? fault->newKey : "";
    if (tok.empty() || key.empty()) {
        // Old pair in SOAP, no Bearer.
        std::string renv = smapi::buildEnvelope("refreshAuthToken", smapi::Fields(), c, true);
        std::string rresp;
        int rcode = 0;
        if (!httpsPost(e.svc.endpoint(), "refreshAuthToken", renv, "", &rcode, &rresp, err)) return false;
        tok = smapi::childText(rresp, "authToken");
        key = smapi::childText(rresp, "privateKey");
    }
    if (tok.empty() || key.empty()) {
        *err = "Sign in again in the Sonos app";
        return false;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    for (auto& ge : g_entries) {
        if (ge.acct.serviceId == sid && ge.acct.serial == serial) {
            ge.acct.token = tok;
            ge.acct.key = key;
        }
    }
    xSemaphoreGive(lock);
    Serial.println("[SVC] Token refreshed");
    return true;
}

// One SMAPI call with the reference's recovery: refresh once on an expired
// token, retry a transient fault, and report anything else.
static bool smapiCall(int sid, int serial, const char* action, const smapi::Fields& fields,
                      std::string* body, String* err, SemaphoreHandle_t lock) {
    bool refreshed = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        Entry e;
        smapi::Credentials c;
        if (!loadCredentials(sid, serial, &e, &c, err, lock)) return false;

        std::string env = smapi::buildEnvelope(action, fields, c);
        std::string bearer = (c.caps & smapi::CAP_BEARER_TOKEN) ? c.token : "";
        int code = 0;
        std::string resp;
        if (!httpsPost(e.svc.endpoint(), action, env, bearer, &code, &resp, err)) return false;

        smapi::Fault fault;
        bool isFault = smapi::parseFault(resp, &fault);
        if (!isFault && code == 200) { body->swap(resp); return true; }
        if (!isFault) {
            // Some providers answer with plain text ("Unauthorized").
            fault.code = "HTTP";
            fault.message = resp.substr(0, 100);
        }
        Serial.printf("[SVC] %s fault: %s / %s\n", action, fault.code.c_str(), fault.message.c_str());

        if (!refreshed && c.auth != "Anonymous" && smapi::isExpiredFault(fault, code)) {
            refreshed = true;
            if (!refreshCredentials(sid, serial, e, c, &fault, err, lock)) return false;
            continue;
        }
        if (attempt < 2 && smapi::isTransient(fault, code)) continue;

        *err = fault.message.empty() ? String("HTTP ") + code : String(fault.message.c_str());
        return false;
    }
    *err = "Service did not answer";
    return false;
}

// ============================================================================
// Content home page — the manifest's "browse" endpoint
// ============================================================================
// Newer services describe their home page in JSON rather than through the SMAPI
// root: the Sonos app's "Your Library" with artists, recent items and so on.
// Each view on that page becomes a section at the top of the service's root;
// its items are embedded in the page, and browsing into one of them goes back
// to SMAPI with the provider's own object id. Mirrors content.py / _content_root
// in the reference.

// Fixed-size and in PSRAM, like a browse page. A home page is ~200 items; held
// as std::strings they all landed in internal RAM (every allocation under 4KB
// does), which took ~50KB off the DMA-capable pool the Wi-Fi link runs on -
// measured: free DMA 110KB -> 61KB after the Spotify home page.
struct ContentView {
    char     id[128];
    char     title[128];
    int      count;
    SvcItem* items;   // PSRAM
};
struct ContentViews {
    ContentView* v = nullptr;   // PSRAM
    int          n = 0;
};
static void freeViews(ContentViews* vs) {
    if (vs->v) {
        for (int i = 0; i < vs->n; i++) if (vs->v[i].items) heap_caps_free(vs->v[i].items);
        heap_caps_free(vs->v);
    }
    vs->v = nullptr;
    vs->n = 0;
}

// ArduinoJson in PSRAM too: its pools are small blocks, i.e. internal RAM by default.
struct SpiRamJsonAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override {
        return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
};
static SpiRamJsonAllocator g_jsonAlloc;

// Only the worker touches these, so no lock: the views of the last content page
// fetched, for browsing into a section without fetching the page again.
static ContentViews g_views;
static int g_viewsSid = -1, g_viewsSerial = -1;
static int g_rootPre = 0;   // sections prepended to that service's SMAPI root
static const char* const kViewPrefix = "@view:";   // our ids for home-page sections
// Our ids for a search: "@search:<mappedId>\x1f<term>". Never sent to a provider;
// doBrowse() turns it into a search call. \x1f because mapped ids contain ':'
// ("catalog:artists:search") and a term can contain anything a keyboard types.
static const char* const kSearchPrefix = "@search:";
static const char        kSearchSep    = '\x1f';

static std::string randomUuid() {
    uint32_t r[4] = {esp_random(), esp_random(), esp_random(), esp_random()};
    char buf[40];
    snprintf(buf, sizeof(buf), "%08x-%04x-4%03x-%04x-%04x%08x", (unsigned)r[0], (unsigned)(r[1] >> 16),
             (unsigned)(r[1] & 0xfff), (unsigned)((r[2] >> 16 & 0x3fff) | 0x8000), (unsigned)(r[2] & 0xffff),
             (unsigned)r[3]);
    return buf;
}

// The browse endpoint from the service manifest, fetched once per account and
// remembered (empty when the manifest has none).
static std::string contentEndpoint(int sid, int serial, SemaphoreHandle_t lock) {
    std::string manifest;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (auto& ge : g_entries) {
        if (ge.acct.serviceId == sid && ge.acct.serial == serial) {
            if (ge.manifestTried) { std::string u = ge.contentUri; xSemaphoreGive(lock); return u; }
            manifest = ge.svc.manifestUri;
        }
    }
    xSemaphoreGive(lock);

    std::string uri, pmap;
    if (manifest.compare(0, 8, "https://") == 0) {
        Headers h;
        h.push_back({"Accept", "application/json"});
        h.push_back({"Accept-Language", "en-US"});
        int code = 0;
        std::string resp;
        String err;
        if (httpsRequest(manifest, "manifest", nullptr, h, &code, &resp, &err) && code == 200) {
            JsonDocument doc;
            if (!deserializeJson(doc, resp)) {
                for (JsonObject ep : doc["endpoints"].as<JsonArray>()) {
                    if (strcmp(ep["type"] | "", "browse") == 0) { uri = ep["uri"] | ""; break; }
                }
                // Newer services (Spotify) name their presentation map here
                // instead of in the service descriptor.
                pmap = doc["presentationMap"]["uri"] | "";
            }
        }
    }
    if (uri.compare(0, 8, "https://") != 0) uri.clear();
    Serial.printf("[SVC] Content endpoint for sid=%d: %s\n", sid, uri.empty() ? "(none)" : uri.c_str());

    xSemaphoreTake(lock, portMAX_DELAY);
    for (auto& ge : g_entries) {
        if (ge.acct.serviceId == sid && ge.acct.serial == serial) {
            ge.manifestTried = true;
            ge.contentUri = uri;
            if (ge.svc.pmapUri.empty() && pmap.compare(0, 8, "https://") == 0) ge.svc.pmapUri = pmap;
        }
    }
    xSemaphoreGive(lock);
    return uri;
}

static bool contentItem(JsonObject item, smapi::Item* out) {
    const char* objectId = item["id"]["objectId"] | "";
    if (!objectId[0]) return false;
    JsonObject rec = item["content"]["container"];
    bool isTrack = false;
    if (rec.isNull()) { rec = item["content"]["track"]; isTrack = true; }
    if (rec.isNull()) return false;

    out->id = objectId;
    out->title = rec["name"] | objectId;
    out->itemType = rec["type"] | (isTrack ? "track" : "container");
    out->subtitle = rec["artist"]["name"] | "";
    out->mimeType = rec["mimeType"] | "";
    const std::string t = smapi::toLower(out->itemType);
    const bool collectionType = t == "album" || t == "artist" || t == "container" ||
                                t == "playlist" || t == "show";
    JsonVariant ce = rec["canEnumerate"];
    out->collection = !isTrack && ((ce.is<bool>() && ce.as<bool>()) || collectionType);
    out->canEnumerate = ce.is<bool>() ? ce.as<bool>() : out->collection;
    JsonVariant cp = rec["canPlay"];
    out->canPlay = cp.is<bool>() ? cp.as<bool>() : (isTrack || t == "album" || t == "playlist");
    // The page does not say how a track is streamed. Spotify ids are
    // recognisable; anything else falls back to the generic track form.
    if (out->mimeType.empty() && out->id.compare(0, 14, "spotify:track:") == 0)
        out->mimeType = "audio/x-spotify";
    return true;
}

// One parsed item into the fixed-size row the UI keeps. False when the id does
// not fit: a truncated id would name a different object.
static bool toSvcItem(const smapi::Item& it, SvcItem* d) {
    if (it.id.size() >= sizeof(d->id)) {
        Serial.printf("[SVC] Skipped item with a %u-byte id\n", (unsigned)it.id.size());
        return false;
    }
    copyStr(d->id, sizeof(d->id), it.id);
    copyStr(d->title, sizeof(d->title), it.title);
    copyStr(d->subtitle, sizeof(d->subtitle), it.subtitle);
    copyStr(d->itemType, sizeof(d->itemType), it.itemType);
    copyStr(d->mimeType, sizeof(d->mimeType), it.mimeType);
    if (it.albumArt.size() < sizeof(d->art)) copyStr(d->art, sizeof(d->art), it.albumArt);
    else d->art[0] = '\0';   // a truncated URL would fetch the wrong image
    d->collection = it.collection;
    d->canPlay = it.canPlay;
    d->canEnumerate = it.canEnumerate;
    return true;
}

// Fetches and parses the content home page into views. False with an empty
// *err when the service simply has no such page.
static bool contentRoot(int sid, int serial, ContentViews* out, String* err,
                        SemaphoreHandle_t lock) {
    std::string uri = contentEndpoint(sid, serial, lock);
    if (uri.empty()) { *err = ""; return false; }

    std::string resp;
    int code = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        Entry e;
        smapi::Credentials c;
        if (!loadCredentials(sid, serial, &e, &c, err, lock)) return false;
        uint32_t uid;
        Headers h;
        h.push_back({"Accept", "application/json"});
        h.push_back({"Accept-Language", "en-US"});
        h.push_back({"X-Sonos-Device-Id", smapi::accountUid(e.acct.udn, &uid) ? c.householdId : c.deviceId});
        h.push_back({"X-Sonos-Corr-Id", randomUuid()});
        h.push_back({"X-Sonos-Controller-ID", ctrlId()});
        if (!c.token.empty()) h.push_back({"Authorization", "Bearer " + c.token});
        if (c.caps & smapi::CAP_CONTEXT) h.push_back({"X-Sonos-Context-TimeZone", c.timeZone});
        resp.clear();
        if (!httpsRequest(uri, "content home", nullptr, h, &code, &resp, err)) return false;
        if (code == 401 && attempt == 0 && c.auth != "Anonymous") {
            if (!refreshCredentials(sid, serial, e, c, nullptr, err, lock)) return false;
            continue;
        }
        break;
    }
    if (code != 200) {
        *err = String("Home page: HTTP ") + code;
        return false;
    }

    // Keep only what is shown: a home page can be large, and most of it is
    // artwork and layout hints.
    JsonDocument filter(&g_jsonAlloc);
    JsonObject fv = filter["views"][0].to<JsonObject>();
    fv["id"]["objectId"] = true;
    fv["content"]["container"]["name"] = true;
    JsonObject fi = fv["items"][0].to<JsonObject>();
    fi["id"]["objectId"] = true;
    for (const char* kind : {"container", "track"}) {
        JsonObject k = fi["content"][kind].to<JsonObject>();
        k["name"] = true;
        k["type"] = true;
        k["canEnumerate"] = true;
        k["canPlay"] = true;
        k["mimeType"] = true;
        k["artist"]["name"] = true;
    }
    JsonDocument doc(&g_jsonAlloc);
    DeserializationError jerr = deserializeJson(doc, resp, DeserializationOption::Filter(filter));
    if (jerr) {
        Serial.printf("[SVC] Home page JSON: %s\n", jerr.c_str());
        *err = "Unexpected home page";
        return false;
    }
    JsonArray views = doc["views"].as<JsonArray>();
    ContentViews vs;
    if (views.size()) {
        vs.v = (ContentView*)heap_caps_calloc(views.size(), sizeof(ContentView),
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!vs.v) { *err = "Out of memory"; return false; }
    }
    for (JsonObject v : views) {
        const char* id = v["id"]["objectId"] | "";
        const char* title = v["content"]["container"]["name"] | "";
        JsonArray items = v["items"].as<JsonArray>();
        if (!id[0] || !title[0] || strlen(id) >= sizeof(vs.v[0].id) || !items.size()) continue;
        ContentView& cv = vs.v[vs.n];
        strncpy(cv.id, id, sizeof(cv.id) - 1);
        strncpy(cv.title, title, sizeof(cv.title) - 1);
        cv.items = (SvcItem*)heap_caps_calloc(items.size(), sizeof(SvcItem), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!cv.items) break;
        for (JsonObject it : items) {
            smapi::Item item;
            if (contentItem(it, &item) && toSvcItem(item, &cv.items[cv.count])) cv.count++;
        }
        Serial.printf("[SVC] Home view %s \"%s\": %d item(s)\n", cv.id, cv.title, cv.count);
        if (cv.count) vs.n++;
        else { heap_caps_free(cv.items); memset(&cv, 0, sizeof(cv)); }
    }
    if (!vs.n) {
        Serial.printf("[SVC] Home page had no usable views (%u bytes): %.300s\n", (unsigned)resp.size(),
                      resp.c_str());
        freeViews(&vs);
        return false;
    }
    *out = vs;
    return true;
}

// ============================================================================
// Worker task
// ============================================================================

void MusicServices::ensureTask() {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (task_) return;
    if (!taskStack_) taskStack_ = (StackType_t*)heap_caps_malloc(SVC_TASK_STACK, MALLOC_CAP_SPIRAM);
    if (taskStack_) {
        task_ = xTaskCreateStaticPinnedToCore(taskEntry, "SvcNet", SVC_TASK_STACK / sizeof(StackType_t),
                                              this, SVC_TASK_PRIORITY, taskStack_, &taskTCB_, 0);
    } else {
        xTaskCreatePinnedToCore(taskEntry, "SvcNet", SVC_TASK_STACK, this, SVC_TASK_PRIORITY, &task_, 0);
    }
}

void MusicServices::taskEntry(void* arg) { static_cast<MusicServices*>(arg)->taskLoop(); }

void MusicServices::taskLoop() {
    for (;;) {
        // While a TLS session is open, wake regularly to close it once idle.
        ulTaskNotifyTake(pdTRUE, tlsOpen() ? pdMS_TO_TICKS(SVC_KEEPALIVE_TICK_MS) : portMAX_DELAY);
        tlsIdleTick();
        for (;;) {
            bool load = false, browse = false, cats = false;
            uint32_t reqId = 0;
            int sid = 0, serial = 0, index = 0, count = 0;
            String objId;
            xSemaphoreTake(lock_, portMAX_DELAY);
            if (g_loadPending) {
                load = true;
                g_loadPending = false;
            } else if (g_catsPending) {
                // Before a browse: it is small, and the search screen is
                // waiting on it to show anything at all.
                cats = true;
                g_catsPending = false;
                sid = g_catsSid; serial = g_catsSerial;
            } else if (g_browsePending) {
                browse = true;
                g_browsePending = false;
                reqId = g_browseReqId; sid = g_browseSid; serial = g_browseSerial;
                index = g_browseIndex; count = g_browseCount; objId = g_browseObjId;
            }
            xSemaphoreGive(lock_);

            if (load) {
                doLoad();
            } else if (cats) {
                doSearchCategories(sid, serial);
            } else if (browse) {
                SvcPage* page = doBrowse(reqId, sid, serial, objId, index, count);
                xSemaphoreTake(lock_, portMAX_DELAY);
                if (g_donePage) freePage(g_donePage);   // never collected: superseded
                g_donePage = page;
                xSemaphoreGive(lock_);
            } else {
                break;
            }
        }
    }
}

void MusicServices::fail(const String& why) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    g_lastError = why;
    xSemaphoreGive(lock_);
    Serial.printf("[SVC] Load failed: %s\n", why.c_str());
    state_ = SVC_FAILED;
}

void MusicServices::doLoad() {
    state_ = SVC_LOADING;
    uint32_t t0 = millis();

    SonosDevice* dev = sonos.getCurrentDevice();
    if (!dev || !dev->connected) { fail("No speaker connected"); return; }
    IPAddress ip = dev->ip;
    String rincon;
    if (xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(100))) {
        rincon = dev->rinconID;
        xSemaphoreGive(sonos.getDeviceMutex());
    }

    String hh = sonos.getHouseholdID();
    if (!hh.length()) {
        // Logging-grade read of the shared code (see the header note), which is
        // all this message needs: tell "the radio is held" from "no answer".
        fail(sonos.lastSoapHttpCode() == SonosController::SOAP_NOT_SENT
                 ? "Network busy - try again"
                 : "Speaker did not give its household");
        return;
    }
    String serialId = sonos.getSystemString("R_TrialZPSerial");
    if (!serialId.length()) Serial.println("[SVC] No R_TrialZPSerial - some services may refuse");

    String encrypted, err;
    if (!captureAccountsEvent(ip, &encrypted, &err)) { fail(err); return; }

    std::string plain;
    if (!decryptAccounts(encrypted, hh.c_str(), &plain, &err)) { fail(err); return; }
    std::vector<smapi::Account> accounts = smapi::parseAccounts(plain);
    std::fill(plain.begin(), plain.end(), '\0');

    String svcResp = sonos.listMusicServices();
    String list = sonos.extractXML(svcResp, "AvailableServiceDescriptorList");
    svcResp = String();
    if (!list.length()) { fail("Speaker did not list its services"); return; }
    std::vector<smapi::Service> services = smapi::parseServices(smapi::xmlUnescape(list.c_str()));
    list = String();

    std::vector<Entry> entries;
    for (const auto& a : accounts) {
        const smapi::Service* svc = nullptr;
        for (const auto& s : services) if (s.id == a.serviceId) { svc = &s; break; }
        if (!svc) continue;
        if (svc->endpoint().empty()) {
            Serial.printf("[SVC] %s: no HTTPS endpoint - skipped\n", svc->name.c_str());
            continue;
        }
        if (!svc->containerType.empty() && svc->containerType != "MService") continue;
        Entry e;
        e.acct = a;
        e.svc = *svc;
        e.label = svc->name;
        entries.push_back(e);
    }
    // Two accounts on one service: tell them apart by nickname.
    for (auto& e : entries) {
        int same = 0;
        for (const auto& o : entries) if (o.acct.serviceId == e.acct.serviceId) same++;
        if (same > 1) {
            if (!e.acct.nickname.empty()) e.label += " (" + e.acct.nickname + ")";
            else e.label += " #" + std::to_string(e.acct.serial);
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return smapi::toLower(a.label) < smapi::toLower(b.label); });

    xSemaphoreTake(lock_, portMAX_DELAY);
    g_entries.swap(entries);
    g_household  = hh.c_str();
    g_deviceId   = serialId.c_str();
    g_zonePlayer = rincon.c_str();
    g_lastError  = "";
    size_t n = g_entries.size();
    xSemaphoreGive(lock_);

    Serial.printf("[SVC] %u account(s), %u service descriptor(s), %u linked source(s) in %lums\n",
                  (unsigned)accounts.size(), (unsigned)services.size(), (unsigned)n,
                  (unsigned long)(millis() - t0));
    xSemaphoreTake(lock_, portMAX_DELAY);
    for (const auto& e : g_entries)
        Serial.printf("[SVC]   %s  sid=%d sn=%d auth=%s caps=%u%s\n", e.label.c_str(), e.acct.serviceId,
                      e.acct.serial, e.svc.auth.c_str(), (unsigned)e.svc.caps,
                      e.svc.manifestUri.empty() ? "" : "  (has manifest)");
    xSemaphoreGive(lock_);
    state_ = SVC_READY;
}

SvcPage* MusicServices::doBrowse(uint32_t reqId, int sid, int serial, const String& objectId,
                                 int index, int count) {
    SvcPage* page = (SvcPage*)heap_caps_calloc(1, sizeof(SvcPage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!page) return nullptr;
    page->reqId = reqId;
    String err;
    smapi::Page p;
    const bool isRoot = objectId == "root";

    if (objectId.startsWith(kSearchPrefix)) {
        const int sep = objectId.indexOf(kSearchSep);
        if (sep < 0) {
            snprintf(page->error, sizeof(page->error), "Bad search");
            return page;
        }
        smapi::Fields f;
        f.push_back({"id", objectId.substring(strlen(kSearchPrefix), sep).c_str()});
        f.push_back({"term", objectId.substring(sep + 1).c_str()});
        f.push_back({"index", std::to_string(index)});
        f.push_back({"count", std::to_string(count)});
        std::string body;
        if (!smapiCall(sid, serial, "search", f, &body, &err, lock_)) {
            snprintf(page->error, sizeof(page->error), "%s", err.c_str());
            return page;
        }
        if (!smapi::parseMetadataResult(body, "searchResult", &p)) {
            snprintf(page->error, sizeof(page->error), "Unexpected answer from the service");
            return page;
        }
    } else if (objectId.startsWith(kViewPrefix)) {
        // A home-page section: its items came embedded in the page.
        const char* viewId = objectId.c_str() + strlen(kViewPrefix);
        if (g_viewsSid != sid || g_viewsSerial != serial) {
            ContentViews vs;
            if (!contentRoot(sid, serial, &vs, &err, lock_)) {
                snprintf(page->error, sizeof(page->error), "%s", err.length() ? err.c_str() : "Section is gone");
                return page;
            }
            freeViews(&g_views);
            g_views = vs;
            g_viewsSid = sid;
            g_viewsSerial = serial;
        }
        const ContentView* view = nullptr;
        for (int i = 0; i < g_views.n; i++) if (strcmp(g_views.v[i].id, viewId) == 0) { view = &g_views.v[i]; break; }
        if (!view) {
            snprintf(page->error, sizeof(page->error), "Section is gone");
            return page;
        }
        // Already in row form: copy the slice straight into the page.
        int from = index < view->count ? index : view->count;
        int n = view->count - from < count ? view->count - from : count;
        page->ok = true;
        page->index = index;
        page->total = view->count;
        if (n > 0) {
            page->items = (SvcItem*)heap_caps_calloc(n, sizeof(SvcItem), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!page->items) {
                page->ok = false;
                snprintf(page->error, sizeof(page->error), "Out of memory");
                return page;
            }
            memcpy(page->items, view->items + from, n * sizeof(SvcItem));
        }
        page->count = n > 0 ? n : 0;
        Serial.printf("[SVC] Browse %s: %d item(s) from %d of %d\n", objectId.c_str(), page->count, index,
                      view->count);
        return page;
    } else {
        // The root of a service with a home page gets its sections on top, then
        // the SMAPI root as before — so nothing the old root offered is lost.
        std::vector<smapi::Item> pre;
        if (isRoot && index == 0) {
            ContentViews vs;
            String cerr;
            if (contentRoot(sid, serial, &vs, &cerr, lock_)) {
                freeViews(&g_views);
                g_views = vs;
                g_viewsSid = sid;
                g_viewsSerial = serial;
                for (int i = 0; i < g_views.n; i++) {
                    const ContentView& v = g_views.v[i];
                    smapi::Item it;
                    it.id = std::string(kViewPrefix) + v.id;
                    it.title = v.title;
                    it.itemType = "section";
                    it.collection = true;
                    it.canEnumerate = true;
                    it.canPlay = false;
                    pre.push_back(it);
                }
            } else if (cerr.length()) {
                Serial.printf("[SVC] No home page: %s\n", cerr.c_str());
            }
            g_rootPre = (int)pre.size();
        }
        const int pre_n = isRoot ? g_rootPre : 0;

        smapi::Fields f;
        f.push_back({"id", objectId.c_str()});
        f.push_back({"index", std::to_string(isRoot && index > 0 ? index - pre_n : index)});
        f.push_back({"count", std::to_string(count)});

        std::string body;
        if (!smapiCall(sid, serial, "getMetadata", f, &body, &err, lock_)) {
            if (pre.empty()) {
                snprintf(page->error, sizeof(page->error), "%s", err.c_str());
                return page;
            }
            Serial.printf("[SVC] SMAPI root failed (%s) - home page only\n", err.c_str());
            p.total = 0;
        } else if (!smapi::parseMetadataResult(body, "getMetadataResult", &p)) {
            if (pre.empty()) {
                snprintf(page->error, sizeof(page->error), "Unexpected answer from the service");
                return page;
            }
            p = smapi::Page();
        }
        body.clear();
        body.shrink_to_fit();
        if (!pre.empty()) p.items.insert(p.items.begin(), pre.begin(), pre.end());
        // Positions as the UI sees them: sections first, then SMAPI's.
        if (isRoot) {
            p.index = index;
            p.total += pre_n;
        }
    }

    page->ok = true;
    page->index = p.index;
    page->total = p.total;
    if (!p.items.empty()) {
        page->items = (SvcItem*)heap_caps_calloc(p.items.size(), sizeof(SvcItem),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!page->items) {
            page->ok = false;
            snprintf(page->error, sizeof(page->error), "Out of memory");
            return page;
        }
    }
    int n = 0;
    for (const auto& it : p.items) {
        if (!toSvcItem(it, &page->items[n])) continue;
        n++;
        // One line per row, as the local browse does — the provider's own ids
        // and types are what any per-service fix has to be written against.
        Serial.printf("[SVC]   #%d %s%s%s type=%s id=%s  \"%s\"\n", p.index + n - 1,
                      it.collection ? "C" : "M", it.canEnumerate ? "e" : "-", it.canPlay ? "p" : "-",
                      it.itemType.c_str(), it.id.c_str(), it.title.c_str());
    }
    page->count = n;
    Serial.printf("[SVC] Browse %s: %d item(s) from %d of %d\n", objectId.c_str(), n, p.index, p.total);
    return page;
}

// Fetches one account's presentation map and keeps its search categories. The
// map is public and the same for every account of a service, but it is cached
// per account like the content endpoint, which keeps the bookkeeping in one
// place; two accounts of one service cost a second small GET at most.
void MusicServices::doSearchCategories(int sid, int serial) {
    auto setState = [&](int st, std::vector<smapi::SearchCategory>* cats) {
        xSemaphoreTake(lock_, portMAX_DELAY);
        for (auto& ge : g_entries) {
            if (ge.acct.serviceId != sid || ge.acct.serial != serial) continue;
            ge.catsState = (decltype(ge.catsState))st;
            if (cats) ge.cats = *cats;
        }
        xSemaphoreGive(lock_);
    };

    std::string pmap;
    bool known = false, manifestTried = false;
    xSemaphoreTake(lock_, portMAX_DELAY);
    for (const auto& ge : g_entries) {
        if (ge.acct.serviceId == sid && ge.acct.serial == serial) {
            known = true;
            pmap = ge.svc.pmapUri;
            manifestTried = ge.manifestTried;
        }
    }
    xSemaphoreGive(lock_);
    if (!known) return;

    if (pmap.empty() && !manifestTried) {
        contentEndpoint(sid, serial, lock_);   // also records the manifest's pmap
        xSemaphoreTake(lock_, portMAX_DELAY);
        for (const auto& ge : g_entries)
            if (ge.acct.serviceId == sid && ge.acct.serial == serial) pmap = ge.svc.pmapUri;
        xSemaphoreGive(lock_);
    }

    std::vector<smapi::SearchCategory> cats;
    if (pmap.compare(0, 8, "https://") == 0) {
        Headers h;
        h.push_back({"Accept-Language", "en-US"});
        int code = 0;
        std::string resp;
        String err;
        if (!httpsRequest(pmap, "pmap", nullptr, h, &code, &resp, &err) || code != 200) {
            Serial.printf("[SVC] Presentation map for sid=%d failed: %s (HTTP %d)\n", sid,
                          err.c_str(), code);
            setState(Entry::CATS_FAILED, nullptr);
            return;
        }
        cats = smapi::parseSearchCategories(resp);
    }
    String names;
    for (const auto& c : cats) {
        names += " ";
        names += c.id.c_str();
        names += "=";
        names += c.mappedId.c_str();
    }
    Serial.printf("[SVC] Search categories for sid=%d:%s\n", sid, names.length() ? names.c_str() : " (none)");
    setState(Entry::CATS_READY, &cats);
}

// ============================================================================
// UI-thread API
// ============================================================================

void MusicServices::requestLoad(bool force) {
    ensureTask();
    if (!task_) return;
    if (state_ == SVC_LOADING) return;
    if (state_ == SVC_READY && !force) return;
    state_ = SVC_LOADING;   // set here so the Sources screen shows "loading" straight away
    xSemaphoreTake(lock_, portMAX_DELAY);
    g_loadPending = true;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

String MusicServices::exportCache() {
    if (!lock_ || state_ != SVC_READY) return "";
    JsonDocument doc(&g_jsonAlloc);
    xSemaphoreTake(lock_, portMAX_DELAY);
    doc["v"]   = 1;
    doc["hh"]  = g_household;
    doc["dev"] = g_deviceId;
    doc["zp"]  = g_zonePlayer;
    JsonArray arr = doc["entries"].to<JsonArray>();
    for (const auto& e : g_entries) {
        JsonObject o = arr.add<JsonObject>();
        o["label"] = e.label;
        JsonObject a = o["a"].to<JsonObject>();
        a["sid"] = e.acct.serviceId; a["type"] = e.acct.serviceType; a["sn"] = e.acct.serial;
        a["udn"] = e.acct.udn; a["user"] = e.acct.username; a["pass"] = e.acct.password;
        a["tok"] = e.acct.token; a["key"] = e.acct.key; a["nick"] = e.acct.nickname;
        JsonObject v = o["s"].to<JsonObject>();
        v["id"] = e.svc.id; v["caps"] = e.svc.caps; v["name"] = e.svc.name; v["uri"] = e.svc.uri;
        v["suri"] = e.svc.secureUri; v["auth"] = e.svc.auth; v["ct"] = e.svc.containerType;
        v["man"] = e.svc.manifestUri; v["pmap"] = e.svc.pmapUri;
    }
    xSemaphoreGive(lock_);
    String out;
    serializeJson(doc, out);
    return out;
}

bool MusicServices::importCache(const String& json) {
    ensureTask();
    if (!lock_ || state_ == SVC_LOADING) return false;
    JsonDocument doc(&g_jsonAlloc);
    if (deserializeJson(doc, json) || doc["v"] != 1) return false;
    std::vector<Entry> entries;
    for (JsonObject o : doc["entries"].as<JsonArray>()) {
        Entry e;
        e.label = o["label"] | "";
        JsonObject a = o["a"];
        e.acct.serviceId = a["sid"] | 0; e.acct.serviceType = a["type"] | 0; e.acct.serial = a["sn"] | 0;
        e.acct.udn = a["udn"] | ""; e.acct.username = a["user"] | ""; e.acct.password = a["pass"] | "";
        e.acct.token = a["tok"] | ""; e.acct.key = a["key"] | ""; e.acct.nickname = a["nick"] | "";
        JsonObject v = o["s"];
        e.svc.id = v["id"] | 0; e.svc.caps = v["caps"] | 0u; e.svc.name = v["name"] | "";
        e.svc.uri = v["uri"] | ""; e.svc.secureUri = v["suri"] | ""; e.svc.auth = v["auth"] | "";
        e.svc.containerType = v["ct"] | ""; e.svc.manifestUri = v["man"] | ""; e.svc.pmapUri = v["pmap"] | "";
        if (e.acct.serviceId && !e.svc.endpoint().empty()) entries.push_back(e);
    }
    if (entries.empty()) return false;
    xSemaphoreTake(lock_, portMAX_DELAY);
    g_entries.swap(entries);
    g_household  = doc["hh"]  | "";
    g_deviceId   = doc["dev"] | "";
    g_zonePlayer = doc["zp"]  | "";
    g_lastError  = "";
    size_t n = g_entries.size();
    xSemaphoreGive(lock_);
    state_ = SVC_READY;
    Serial.printf("[SVC] %u linked source(s) restored from the cache\n", (unsigned)n);
    return true;
}

String MusicServices::lastError() {
    if (!lock_) return "";
    xSemaphoreTake(lock_, portMAX_DELAY);
    String e = g_lastError;
    xSemaphoreGive(lock_);
    return e;
}

int MusicServices::sources(SvcSource* out, int max) {
    if (!lock_ || state_ != SVC_READY) return 0;
    xSemaphoreTake(lock_, portMAX_DELAY);
    int n = 0;
    for (const auto& e : g_entries) {
        if (n >= max) break;
        out[n].serviceId = e.acct.serviceId;
        out[n].serial = e.acct.serial;
        out[n].anonymous = e.svc.auth == "Anonymous";
        copyStr(out[n].label, sizeof(out[n].label), e.label);
        n++;
    }
    xSemaphoreGive(lock_);
    return n;
}

uint32_t MusicServices::requestBrowse(int serviceId, int serial, const char* objectId, int index, int count) {
    ensureTask();
    if (!task_) return 0;
    xSemaphoreTake(lock_, portMAX_DELAY);
    uint32_t id = g_nextReqId++;
    g_browsePending = true;
    g_browseReqId = id;
    g_browseSid = serviceId;
    g_browseSerial = serial;
    g_browseObjId = objectId;
    g_browseIndex = index;
    g_browseCount = count;
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
    return id;
}

int MusicServices::searchCategories(int serviceId, int serial, SvcCategory* out, int max) {
    ensureTask();
    if (!task_ || state_ != SVC_READY) return SVC_CATS_FAILED;
    int n = 0;
    bool queue = false;
    xSemaphoreTake(lock_, portMAX_DELAY);
    Entry* e = nullptr;
    for (auto& ge : g_entries)
        if (ge.acct.serviceId == serviceId && ge.acct.serial == serial) { e = &ge; break; }
    if (!e) {
        n = SVC_CATS_FAILED;
    } else if (e->catsState == Entry::CATS_READY) {
        for (const auto& c : e->cats) {
            if (n >= max) break;
            copyStr(out[n].id, sizeof(out[n].id), c.id);
            copyStr(out[n].mappedId, sizeof(out[n].mappedId), c.mappedId);
            n++;
        }
    } else if (e->catsState == Entry::CATS_FAILED) {
        e->catsState = Entry::CATS_UNTRIED;   // the next call retries
        n = SVC_CATS_FAILED;
    } else {
        if (e->catsState == Entry::CATS_UNTRIED) {
            // One slot: an account still waiting in it goes back to untried,
            // or it would read "loading" forever.
            if (g_catsPending && (g_catsSid != serviceId || g_catsSerial != serial)) {
                for (auto& ge : g_entries)
                    if (ge.acct.serviceId == g_catsSid && ge.acct.serial == g_catsSerial &&
                        ge.catsState == Entry::CATS_LOADING)
                        ge.catsState = Entry::CATS_UNTRIED;
            }
            e->catsState = Entry::CATS_LOADING;
            g_catsPending = true;
            g_catsSid = serviceId;
            g_catsSerial = serial;
            queue = true;
        }
        n = SVC_CATS_LOADING;
    }
    xSemaphoreGive(lock_);
    if (queue) xTaskNotifyGive(task_);
    return n;
}

uint32_t MusicServices::requestSearch(int serviceId, int serial, const char* mappedId, const char* term,
                                      int index, int count) {
    String id = kSearchPrefix;
    id += mappedId;
    id += kSearchSep;
    id += term;
    return requestBrowse(serviceId, serial, id.c_str(), index, count);
}

SvcPage* MusicServices::takePage(uint32_t reqId) {
    if (!lock_) return nullptr;
    SvcPage* p = nullptr;
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (g_donePage && g_donePage->reqId == reqId) {
        p = g_donePage;
        g_donePage = nullptr;
    }
    xSemaphoreGive(lock_);
    return p;
}

void MusicServices::freePage(SvcPage* page) {
    if (!page) return;
    if (page->items) heap_caps_free(page->items);
    heap_caps_free(page);
}

bool MusicServices::buildPlayback(int serviceId, int serial, const SvcItem& item,
                                  String* uri, String* didl, bool* container) {
    if (!lock_) return false;
    Entry e;
    xSemaphoreTake(lock_, portMAX_DELAY);
    bool have = findEntry(serviceId, serial, &e);
    xSemaphoreGive(lock_);
    if (!have) return false;

    smapi::Item it;
    it.id = item.id;
    it.title = item.title;
    it.itemType = item.itemType;
    it.mimeType = item.mimeType;
    it.collection = item.collection;
    it.canPlay = item.canPlay;
    it.canEnumerate = item.canEnumerate;
    smapi::Playback pb;
    if (!smapi::buildPlayback(it, serviceId, serial, e.acct.udn, &pb)) return false;
    *uri = pb.uri.c_str();
    *didl = pb.didl.c_str();
    *container = pb.container;
    return true;
}
