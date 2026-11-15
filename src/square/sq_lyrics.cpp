// sq_lyrics.cpp — lyrics for the square UI: fetch, parse, store (no LVGL).
//
// Source: LRCLIB (lrclib.net), free and keyless. It answers with
//   syncedLyrics   LRC text, one "[mm:ss.xx]line" per line, cued to the line
//   plainLyrics    the same words without timing
//   instrumental   true when the track has no words at all
// Synced lyrics are cued per LINE, not per word, so the screen highlights a
// whole line at a time.
//
// The exact lookup (/api/get) wants the title as LRCLIB knows it; Spotify's
// titles often carry " - Remastered 2011" or " (feat. X)", so a miss falls back
// to /api/search with the title cut before that noise.
//
// One task, PSRAM stack, newest request wins: the player asks on every update
// and only a new artist+title starts a fetch. Results replace the store under a
// mutex and bump sq_lyrics_version; the UI rebuilds when it sees the bump.
#include "sq_work.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#define LYR_MAX_LINES  200
#define LYR_TEXT       128
#define LYR_MAX_BODY   (256 * 1024)
#define LYR_DELAY_MS   1200   // let the cover download first after a track change

struct LyrLine { int32_t ms; char text[LYR_TEXT]; };

struct LyrReq {
    uint32_t gen;
    int      duration;
    char     artist[96];
    char     title[128];
};

volatile uint32_t sq_lyrics_version = 0;

static LyrLine*          s_lines = nullptr;    // PSRAM, the published store
static int               s_count = 0;
static SqLyrState        s_state = SQ_LYR_NONE;
static SemaphoreHandle_t s_lock  = nullptr;
static QueueHandle_t     s_queue = nullptr;    // length 1, overwritten
static volatile uint32_t s_gen   = 0;          // the request the store should answer
static char              s_key[224] = "";      // artist \x1f title of s_gen

static LyrLine* s_work = nullptr;              // PSRAM, the task's scratch
static uint8_t* s_body = nullptr;              // PSRAM, one HTTP body

// ── Store ───────────────────────────────────────────────────────────────────
static void publish(uint32_t gen, SqLyrState st, int count) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (gen == s_gen) {
        if (count > 0) memcpy(s_lines, s_work, count * sizeof(LyrLine));
        s_count = count;
        s_state = st;
        sq_lyrics_version++;
    }
    xSemaphoreGive(s_lock);
}

void sqLyricsWant(const char* artist, const char* title, int duration_s, bool eligible) {
    if (!s_lock) return;
    char key[sizeof(s_key)];
    if (eligible && artist && *artist && title && *title)
        snprintf(key, sizeof(key), "%s\x1f%s", artist, title);
    else
        key[0] = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool same = !strcmp(key, s_key);
    if (!same) {
        strlcpy(s_key, key, sizeof(s_key));
        s_gen++;
        s_count = 0;
        s_state = key[0] ? SQ_LYR_LOADING : SQ_LYR_NONE;
        sq_lyrics_version++;
    }
    uint32_t gen = s_gen;
    xSemaphoreGive(s_lock);
    if (same || !key[0]) return;

    LyrReq r = {};
    r.gen = gen;
    r.duration = duration_s;
    strlcpy(r.artist, artist, sizeof(r.artist));
    strlcpy(r.title, title, sizeof(r.title));
    xQueueOverwrite(s_queue, &r);
}

SqLyrState sqLyricsState(void) { return s_state; }
void sqLyricsLock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
void sqLyricsUnlock(void) { xSemaphoreGive(s_lock); }
int  sqLyricsCount(void)  { return s_count; }
int  sqLyricsMs(int i)    { return (i >= 0 && i < s_count) ? s_lines[i].ms : -1; }
const char* sqLyricsText(int i) { return (i >= 0 && i < s_count) ? s_lines[i].text : ""; }

// The line playing at `pos_ms`: the last one cued at or before it, -1 before the first.
int sqLyricsIndexAt(int pos_ms) {
    int lo = 0, hi = s_count - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s_lines[mid].ms <= pos_ms) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return best;
}

// ── Parsing ─────────────────────────────────────────────────────────────────
// "[mm:ss]", "[mm:ss.x]", "[mm:ss.xx]", "[mm:ss.xxx]" -> ms; advances *p past ']'.
// Returns -1 for anything else ("[ar:Artist]", "[offset:+100]"...).
static int32_t parseTag(const char** p) {
    const char* s = *p;
    if (*s != '[') return -1;
    ++s;
    if (!isdigit((unsigned char)*s)) return -1;
    int32_t mm = 0, ss = 0, frac = 0, scale = 1;
    while (isdigit((unsigned char)*s)) mm = mm * 10 + (*s++ - '0');
    if (*s++ != ':') return -1;
    if (!isdigit((unsigned char)*s)) return -1;
    while (isdigit((unsigned char)*s)) ss = ss * 10 + (*s++ - '0');
    if (*s == '.' || *s == ':') {
        ++s;
        while (isdigit((unsigned char)*s)) { if (scale < 1000) { frac = frac * 10 + (*s - '0'); scale *= 10; } ++s; }
    }
    if (*s != ']') return -1;
    *p = s + 1;
    return mm * 60000 + ss * 1000 + (scale > 1 ? frac * 1000 / scale : 0);
}

// Copies at most n-1 bytes without cutting a UTF-8 sequence in half.
static void copyUtf8(char* dst, const char* src, size_t len, size_t n) {
    if (len > n - 1) {
        len = n - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) --len;
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

static void trimInto(char* dst, const char* b, const char* e, size_t n) {
    while (b < e && (*b == ' ' || *b == '\t' || *b == '\r')) ++b;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) --e;
    copyUtf8(dst, b, e - b, n);
}

static int parseSynced(const char* lrc) {
    int n = 0;
    for (const char* line = lrc; *line && n < LYR_MAX_LINES;) {
        const char* end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        // A line may carry several cues: "[00:12.00][01:40.00]chorus".
        int32_t cues[8];
        int ncues = 0;
        const char* p = line;
        for (;;) {
            int32_t ms = parseTag(&p);
            if (ms < 0) break;
            if (ncues < 8) cues[ncues++] = ms;
        }
        if (ncues) {
            char text[LYR_TEXT];
            trimInto(text, p, end, sizeof(text));
            // An empty cue is an instrumental break: show dots so the previous
            // line does not stay lit through a long solo.
            if (!text[0]) strlcpy(text, "• • •", sizeof(text));
            for (int c = 0; c < ncues && n < LYR_MAX_LINES; ++c) {
                s_work[n].ms = cues[c];
                strlcpy(s_work[n].text, text, LYR_TEXT);
                ++n;
            }
        }
        line = *end ? end + 1 : end;
    }
    // Multi-cue lines arrive out of order: insertion sort (n is small).
    for (int i = 1; i < n; ++i) {
        LyrLine t = s_work[i];
        int j = i - 1;
        while (j >= 0 && s_work[j].ms > t.ms) { s_work[j + 1] = s_work[j]; --j; }
        s_work[j + 1] = t;
    }
    // Leading dots before the first words say nothing.
    int skip = 0;
    while (skip < n && !strcmp(s_work[skip].text, "• • •")) ++skip;
    if (skip) { memmove(s_work, s_work + skip, (n - skip) * sizeof(LyrLine)); n -= skip; }
    return n;
}

static int parsePlain(const char* txt) {
    int n = 0;
    bool blank = true;   // collapse runs of empty lines into one paragraph break
    for (const char* line = txt; *line && n < LYR_MAX_LINES;) {
        const char* end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        char text[LYR_TEXT];
        trimInto(text, line, end, sizeof(text));
        if (text[0] || !blank) {
            s_work[n].ms = -1;
            strlcpy(s_work[n].text, text, LYR_TEXT);
            ++n;
        }
        blank = !text[0];
        line = *end ? end + 1 : end;
    }
    while (n > 0 && !s_work[n - 1].text[0]) --n;
    return n;
}

// ── HTTP ────────────────────────────────────────────────────────────────────
struct LyrSink : public Stream {
    uint8_t* buf; size_t cap; size_t len = 0; bool overflow = false;
    LyrSink(uint8_t* b, size_t c) : buf(b), cap(c) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* d, size_t n) override {
        if (len + n > cap) { overflow = true; return 0; }
        memcpy(buf + len, d, n); len += n; return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
};

static void urlEncode(String& out, const char* s) {
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else if (c == ' ') out += '+';
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
}

// GET into s_body (NUL-terminated). Returns the HTTP status, or < 0.
static int httpGet(const String& url, size_t* len) {
    WiFiClientSecure tls;
    tls.setInsecure();   // public lyrics: integrity is not worth a CA bundle here
    HTTPClient http;
    if (!http.begin(tls, url)) return -1;
    http.useHTTP10(true);          // no chunked keep-alive body to wait out
    http.setTimeout(8000);
    http.setConnectTimeout(5000);
    http.setUserAgent("SonosESP/1.0");
    int code = http.GET();
    if (code == 200) {
        LyrSink sink(s_body, LYR_MAX_BODY - 1);
        int w = http.writeToStream(&sink);
        if (sink.overflow || w < 0) code = -2;
        *len = sink.len;
        s_body[sink.len] = 0;
    }
    http.end();
    return code;
}

struct PsramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void  deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static PsramAllocator s_alloc;

// Turns one LRCLIB record into the scratch store.
static SqLyrState fromRecord(JsonVariantConst rec, int* count) {
    *count = 0;
    const char* synced = rec["syncedLyrics"] | "";
    const char* plain  = rec["plainLyrics"] | "";
    if (*synced && (*count = parseSynced(synced)) > 0) return SQ_LYR_SYNCED;
    if (*plain && (*count = parsePlain(plain)) > 0)    return SQ_LYR_PLAIN;
    return SQ_LYR_MISSING;   // instrumental, or an empty record
}

// "Song - Remastered 2011" / "Song (feat. X)" / "Song [Live]" -> "Song".
static void cleanTitle(char* out, size_t n, const char* title) {
    strlcpy(out, title, n);
    const char* cuts[] = {" - ", " (", " ["};
    for (const char* c : cuts) {
        char* p = strstr(out, c);
        if (p && p != out) *p = 0;
    }
}

// "A, B & C" -> "A".
static void mainArtist(char* out, size_t n, const char* artist) {
    strlcpy(out, artist, n);
    const char* cuts[] = {", ", " & ", " feat", " ft."};
    for (const char* c : cuts) {
        char* p = strstr(out, c);
        if (p && p != out) *p = 0;
    }
}

static SqLyrState fetch(const LyrReq& r, int* count) {
    *count = 0;
    String url = "https://lrclib.net/api/get?artist_name=";
    urlEncode(url, r.artist);
    url += "&track_name=";
    urlEncode(url, r.title);
    if (r.duration > 0) { url += "&duration="; url += r.duration; }

    size_t len = 0;
    uint32_t t0 = millis();
    int code = httpGet(url, &len);
    Serial.printf("[SQ/LYR] get %d (%u B) in %lums\n", code, (unsigned)len, millis() - t0);
    if (code == 200) {
        JsonDocument filter;
        filter["syncedLyrics"] = true;
        filter["plainLyrics"] = true;
        JsonDocument doc(&s_alloc);
        if (!deserializeJson(doc, (const char*)s_body, len, DeserializationOption::Filter(filter))) {
            SqLyrState st = fromRecord(doc.as<JsonVariantConst>(), count);
            if (st != SQ_LYR_MISSING) return st;
        }
    } else if (code != 404) {
        return code < 0 ? SQ_LYR_NONE : SQ_LYR_MISSING;   // NONE: network, try again later
    }

    // Not under that exact name: search with the title stripped of its noise.
    char title[128], artist[96];
    cleanTitle(title, sizeof(title), r.title);
    mainArtist(artist, sizeof(artist), r.artist);
    url = "https://lrclib.net/api/search?track_name=";
    urlEncode(url, title);
    url += "&artist_name=";
    urlEncode(url, artist);
    t0 = millis();
    code = httpGet(url, &len);
    Serial.printf("[SQ/LYR] search %d (%u B) in %lums\n", code, (unsigned)len, millis() - t0);
    if (code != 200) return code < 0 ? SQ_LYR_NONE : SQ_LYR_MISSING;

    JsonDocument filter;
    filter[0]["syncedLyrics"] = true;
    filter[0]["plainLyrics"] = true;
    filter[0]["duration"] = true;
    JsonDocument doc(&s_alloc);
    if (deserializeJson(doc, (const char*)s_body, len, DeserializationOption::Filter(filter)))
        return SQ_LYR_MISSING;
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    // Best: synced and the right length (a live take or an edit is out of step),
    // then any synced, then plain words.
    JsonVariantConst pick;
    for (int pass = 0; pass < 3 && pick.isNull(); ++pass) {
        for (JsonVariantConst rec : arr) {
            bool synced = *(rec["syncedLyrics"] | "") != 0;
            bool plain  = *(rec["plainLyrics"] | "") != 0;
            int  dur    = rec["duration"] | 0;
            bool fits   = r.duration <= 0 || (dur > 0 && abs(dur - r.duration) <= 4);
            if ((pass == 0 && synced && fits) || (pass == 1 && synced) || (pass == 2 && plain)) {
                pick = rec;
                break;
            }
        }
    }
    if (pick.isNull()) return SQ_LYR_MISSING;
    return fromRecord(pick, count);
}

static void lyricsTask(void*) {
    LyrReq r;
    for (;;) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) != pdTRUE) continue;
        // A burst of track changes (skipping) only fetches the one that stays.
        vTaskDelay(pdMS_TO_TICKS(LYR_DELAY_MS));
        if (r.gen != s_gen) continue;
        if (WiFi.status() != WL_CONNECTED) { publish(r.gen, SQ_LYR_NONE, 0); continue; }
        int count = 0;
        SqLyrState st = fetch(r, &count);
        if (st == SQ_LYR_NONE && r.gen == s_gen) {
            // Network trouble: one more go before giving up on this track.
            vTaskDelay(pdMS_TO_TICKS(3000));
            if (r.gen != s_gen) continue;
            st = fetch(r, &count);
            if (st == SQ_LYR_NONE) st = SQ_LYR_MISSING;
        }
        Serial.printf("[SQ/LYR] %s - %s: %s, %d lines (stack low %u)\n", r.artist, r.title,
                      st == SQ_LYR_SYNCED ? "synced" : st == SQ_LYR_PLAIN ? "plain" : "none", count,
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        publish(r.gen, st, count);
    }
}

void sqLyricsStart(void) {
    s_lines = (LyrLine*)heap_caps_calloc(LYR_MAX_LINES, sizeof(LyrLine), MALLOC_CAP_SPIRAM);
    s_work  = (LyrLine*)heap_caps_calloc(LYR_MAX_LINES, sizeof(LyrLine), MALLOC_CAP_SPIRAM);
    s_body  = (uint8_t*)heap_caps_malloc(LYR_MAX_BODY, MALLOC_CAP_SPIRAM);
    s_lock  = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(1, sizeof(LyrReq));
    // PSRAM stack, like the artwork task: no flash writes here, and mbedTLS
    // allocates in PSRAM (main_square.cpp).
    static StaticTask_t tcb;
    const size_t stack_words = 8192;
    StackType_t* stack = (StackType_t*)heap_caps_malloc(stack_words * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    xTaskCreateStaticPinnedToCore(lyricsTask, "SqLyr", stack_words, nullptr, 1, stack, &tcb, 0);
}
