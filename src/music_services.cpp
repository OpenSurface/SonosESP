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

// One HTTPS exchange under the full SDIO envelope. `body` null means GET.
// `label` is only for the log.
static bool httpsRequest(const std::string& url, const char* label, const std::string* body,
                         const Headers& headers, int* code, std::string* resp, String* err) {
    if (ota_in_progress) { *err = "Busy updating"; return false; }

    for (int attempt = 0; attempt < 3; attempt++) {
        // An art download in flight plus our TLS handshake burst is the
        // pkt_rxbuff overflow this firmware is built around avoiding. Wait it out.
        uint32_t waitStart = millis();
        while ((art_download_in_progress || clock_photo_in_progress) &&
               millis() - waitStart < SVC_ART_WAIT_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        sdioPreWait("SVC", SDIO_WAIT_HTTPS_COOLDOWN);

        if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
            *err = "Network busy";
            return false;
        }
        if (art_download_in_progress || clock_photo_in_progress) {
            xSemaphoreGive(network_mutex);
            continue;   // started while we waited for the mutex: go round again
        }
        {
            unsigned long now = millis();
            unsigned long eNet = now - last_network_end_ms, eHttps = now - last_https_end_ms;
            if (last_network_end_ms > 0 && eNet < SDIO_GENERAL_COOLDOWN_MS)
                vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - eNet));
            if (last_https_end_ms > 0 && eHttps < SDIO_HTTPS_TCP_CLOSE_MS)
                vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_TCP_CLOSE_MS - eHttps));
        }
        size_t dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        if (dma < SVC_MIN_FREE_DMA) {
            xSemaphoreGive(network_mutex);
            Serial.printf("[SVC] DMA too low for TLS (%u < %u)\n", (unsigned)dma, (unsigned)SVC_MIN_FREE_DMA);
            *err = "Low memory, try again";
            return false;
        }

        uint32_t t0 = millis();
        {
            WiFiClientSecure client;
            // Same posture as lyrics, art, weather and OTA. The token sent here
            // is one any device on the LAN can already read from the speaker.
            client.setInsecure();
            client.setHandshakeTimeout(SVC_HANDSHAKE_TIMEOUT_S);
            HTTPClient http;
            http.begin(client, url.c_str());
            http.setConnectTimeout(SVC_CONNECT_TIMEOUT_MS);
            http.setTimeout(SVC_HTTP_TIMEOUT_MS);
            http.setReuse(false);
            http.setUserAgent(kUserAgent);
            for (const auto& h : headers) http.addHeader(h.first.c_str(), h.second.c_str());

            *code = body ? http.POST((uint8_t*)body->data(), body->size()) : http.GET();
            if (*code > 0) {
                int len = http.getSize();
                if (len > SVC_MAX_BODY_BYTES) {
                    Serial.printf("[SVC] %s response too large (%d)\n", label, len);
                    *code = -100;
                } else {
                    String s = http.getString();
                    resp->assign(s.c_str(), s.length());
                }
            }
            if (WiFiClient* st = http.getStreamPtr()) {
                for (int i = 0; i < 200 && st->connected(); i++) vTaskDelay(pdMS_TO_TICKS(1));
            }
            http.end();
            client.stop();   // free the mbedTLS record buffers now (~32KB DMA)
        }
        vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_TCP_CLOSE_MS));
        last_network_end_ms = millis();
        last_https_end_ms   = millis();
        xSemaphoreGive(network_mutex);

        Serial.printf("[SVC] %s -> HTTP %d, %u bytes in %lums\n", label, *code,
                      (unsigned)resp->size(), (unsigned long)(millis() - t0));
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

struct ContentView {
    std::string id, title;
    std::vector<smapi::Item> items;
};

// Only the worker touches these, so no lock: the views of the last content page
// fetched, for browsing into a section without fetching the page again.
static std::vector<ContentView> g_views;
static int g_viewsSid = -1, g_viewsSerial = -1;
static int g_rootPre = 0;   // sections prepended to that service's SMAPI root
static const char* const kViewPrefix = "@view:";   // our ids for home-page sections

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

    std::string uri;
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

// Fetches and parses the content home page into views. False with an empty
// *err when the service simply has no such page.
static bool contentRoot(int sid, int serial, std::vector<ContentView>* views, String* err,
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
    JsonDocument filter;
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
    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, resp, DeserializationOption::Filter(filter));
    if (jerr) {
        Serial.printf("[SVC] Home page JSON: %s\n", jerr.c_str());
        *err = "Unexpected home page";
        return false;
    }
    for (JsonObject v : doc["views"].as<JsonArray>()) {
        ContentView cv;
        cv.id = v["id"]["objectId"] | "";
        cv.title = v["content"]["container"]["name"] | "";
        if (cv.id.empty() || cv.title.empty()) continue;
        for (JsonObject it : v["items"].as<JsonArray>()) {
            smapi::Item item;
            if (contentItem(it, &item)) cv.items.push_back(item);
        }
        Serial.printf("[SVC] Home view %s \"%s\": %u item(s)\n", cv.id.c_str(), cv.title.c_str(),
                      (unsigned)cv.items.size());
        if (!cv.items.empty()) views->push_back(cv);
    }
    if (views->empty()) {
        Serial.printf("[SVC] Home page had no usable views (%u bytes): %.300s\n", (unsigned)resp.size(),
                      resp.c_str());
    }
    return !views->empty();
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
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            bool load = false, browse = false;
            uint32_t reqId = 0;
            int sid = 0, serial = 0, index = 0, count = 0;
            String objId;
            xSemaphoreTake(lock_, portMAX_DELAY);
            if (g_loadPending) {
                load = true;
                g_loadPending = false;
            } else if (g_browsePending) {
                browse = true;
                g_browsePending = false;
                reqId = g_browseReqId; sid = g_browseSid; serial = g_browseSerial;
                index = g_browseIndex; count = g_browseCount; objId = g_browseObjId;
            }
            xSemaphoreGive(lock_);

            if (load) {
                doLoad();
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

    if (objectId.startsWith(kViewPrefix)) {
        // A home-page section: its items came embedded in the page.
        std::string viewId = objectId.c_str() + strlen(kViewPrefix);
        if (g_viewsSid != sid || g_viewsSerial != serial) {
            std::vector<ContentView> views;
            if (!contentRoot(sid, serial, &views, &err, lock_)) {
                snprintf(page->error, sizeof(page->error), "%s", err.length() ? err.c_str() : "Section is gone");
                return page;
            }
            g_views.swap(views);
            g_viewsSid = sid;
            g_viewsSerial = serial;
        }
        const ContentView* view = nullptr;
        for (const auto& v : g_views) if (v.id == viewId) { view = &v; break; }
        if (!view) {
            snprintf(page->error, sizeof(page->error), "Section is gone");
            return page;
        }
        p.index = index;
        p.total = (int)view->items.size();
        for (int i = index; i < (int)view->items.size() && i < index + count; i++)
            p.items.push_back(view->items[i]);
    } else {
        // The root of a service with a home page gets its sections on top, then
        // the SMAPI root as before — so nothing the old root offered is lost.
        std::vector<smapi::Item> pre;
        if (isRoot && index == 0) {
            std::vector<ContentView> views;
            String cerr;
            if (contentRoot(sid, serial, &views, &cerr, lock_)) {
                g_views.swap(views);
                g_viewsSid = sid;
                g_viewsSerial = serial;
                for (const auto& v : g_views) {
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
        if (it.id.size() >= sizeof(page->items[0].id)) {
            Serial.printf("[SVC] Skipped item with a %u-byte id\n", (unsigned)it.id.size());
            continue;
        }
        SvcItem& d = page->items[n++];
        copyStr(d.id, sizeof(d.id), it.id);
        copyStr(d.title, sizeof(d.title), it.title);
        copyStr(d.subtitle, sizeof(d.subtitle), it.subtitle);
        copyStr(d.itemType, sizeof(d.itemType), it.itemType);
        copyStr(d.mimeType, sizeof(d.mimeType), it.mimeType);
        d.collection = it.collection;
        d.canPlay = it.canPlay;
        d.canEnumerate = it.canEnumerate;
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
