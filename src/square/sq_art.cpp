// sq_art.cpp — artwork for the square UI (see sq_work.h).
//
// The ESP32-S3 has no JPEG codec, so covers are decoded in software. Two things
// keep that cheap:
//   - JPEGDEC decodes straight to 1/2, 1/4 or 1/8 scale, so a 640px Spotify
//     cover becomes ~160px without ever existing at full size;
//   - each image is decoded once, at the size it is shown, and kept in PSRAM.
// Progressive JPEGs (JPEGDEC cannot scale those) go through stb_image.
#include "sq_work.h"
#include "ui_common.h"
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <JPEGDEC.h>
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>

extern bool decodeJPEGProgressiveStb(const uint8_t* buf, size_t len, uint16_t** out, int* out_w,
                                     int* out_h);

volatile uint32_t sq_art_version = 0;

#define ART_SLOTS        96
#define ART_MAX_BYTES    (512 * 1024)
#define ART_QUEUE_LEN    24

struct ArtSlot {
    uint32_t       key;        // hash(url) ^ size
    int            size;
    uint32_t       last_use;   // millis() of the last get
    enum { EMPTY, QUEUED, READY, FAILED } state;
    uint16_t*      pixels;     // PSRAM, size*size
    lv_image_dsc_t dsc;
};

struct ArtReq {
    uint32_t key;
    int      size;
    char     url[384];
};

static ArtSlot*          s_slots = nullptr;   // PSRAM
static SemaphoreHandle_t s_lock  = nullptr;
static QueueHandle_t     s_queue = nullptr;
static uint8_t*          s_dl    = nullptr;   // download buffer, PSRAM

static uint32_t artKey(const char* url, int size) {
    uint32_t h = 2166136261u;
    for (const char* p = url; *p; ++p) { h ^= (uint8_t)*p; h *= 16777619u; }
    return h ^ ((uint32_t)size * 2654435761u);
}

// Picks a slot for a new key: an empty one, else the least recently used
// READY/FAILED one. Caller holds the lock.
static ArtSlot* slotFor(uint32_t key) {
    ArtSlot* victim = nullptr;
    for (int i = 0; i < ART_SLOTS; ++i) {
        ArtSlot& s = s_slots[i];
        if (s.state == ArtSlot::EMPTY) return &s;
        if (s.state == ArtSlot::QUEUED) continue;
        if (!victim || s.last_use < victim->last_use) victim = &s;
    }
    if (victim && victim->pixels) { heap_caps_free(victim->pixels); victim->pixels = nullptr; }
    return victim;
}

const lv_image_dsc_t* sqArtGet(const char* url, int size) {
    if (!url || !*url || !s_slots) return nullptr;
    if (strncmp(url, "http", 4) != 0 && url[0] != '/') return nullptr;   // "unknown", "", ...
    uint32_t key = artKey(url, size);
    const lv_image_dsc_t* out = nullptr;
    bool enqueue = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ArtSlot* hit = nullptr;
    for (int i = 0; i < ART_SLOTS; ++i)
        if (s_slots[i].state != ArtSlot::EMPTY && s_slots[i].key == key) { hit = &s_slots[i]; break; }
    if (hit) {
        hit->last_use = millis();
        if (hit->state == ArtSlot::READY) out = &hit->dsc;
    } else {
        ArtSlot* s = slotFor(key);
        if (s) {
            memset(s, 0, sizeof(*s));
            s->key = key;
            s->size = size;
            s->state = ArtSlot::QUEUED;
            s->last_use = millis();
            enqueue = true;
        }
    }
    xSemaphoreGive(s_lock);
    if (enqueue) {
        ArtReq r = {};
        r.key = key;
        r.size = size;
        strlcpy(r.url, url, sizeof(r.url));
        if (xQueueSend(s_queue, &r, 0) != pdTRUE) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            for (int i = 0; i < ART_SLOTS; ++i)
                if (s_slots[i].key == key && s_slots[i].state == ArtSlot::QUEUED) s_slots[i].state = ArtSlot::EMPTY;
            xSemaphoreGive(s_lock);
        }
    }
    return out;
}

// ── Download ────────────────────────────────────────────────────────────────
// A Stream that writes into a fixed buffer, for HTTPClient::writeToStream().
struct BufSink : public Stream {
    uint8_t* buf; size_t cap; size_t len = 0; bool overflow = false;
    BufSink(uint8_t* b, size_t c) : buf(b), cap(c) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* d, size_t n) override {
        if (len + n > cap) { overflow = true; return 0; }
        memcpy(buf + len, d, n); len += n; return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
};
static int download(const char* url, uint8_t* buf, int cap) {
    String full = url;
    if (full.startsWith("/")) {
        // Sonos-relative (/getaa?...): served by the speaker itself.
        SonosDevice* d = sonos.getCurrentDevice();
        if (!d) return -1;
        full = "http://" + d->ip.toString() + ":1400" + full;
    }
    HTTPClient http;
    WiFiClientSecure tls;
    WiFiClient plain;
    bool https = full.startsWith("https://");
    if (https) {
        tls.setInsecure();   // public cover art: integrity is not worth a CA bundle here
        if (!http.begin(tls, full)) return -1;
    } else {
        if (!http.begin(plain, full)) return -1;
    }
    http.setTimeout(8000);
    http.setConnectTimeout(5000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[SQ/ART] GET %d (%s) - internal %u/%u, psram %u/%u, wifi %d\n", code,
                      code < 0 ? http.errorToString(code).c_str() : "http",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), (int)WiFi.status());
        http.end();
        return code < 0 ? code : -code;   // always negative: never a byte count
    }
    // writeToStream() undoes chunked transfer encoding, which reading the raw
    // socket did not (Radio France covers came back as a single byte).
    BufSink sink(buf, cap);
    int declared = http.getSize();
    int w = http.writeToStream(&sink);
    http.end();
    if (sink.len < 64)
        Serial.printf("[SQ/ART] Tiny body: http=%d declared=%d wrote=%d len=%u\n", code, declared, w,
                      (unsigned)sink.len);
    if (sink.overflow) return -1;
    return w < 0 ? w : (int)sink.len;
}

// ── Decode ──────────────────────────────────────────────────────────────────
struct JpegCtx { uint16_t* dst; int w, h; };

static int jpegDraw(JPEGDRAW* d) {
    JpegCtx* c = (JpegCtx*)d->pUser;
    for (int y = 0; y < d->iHeight; ++y) {
        int dy = d->y + y;
        if (dy >= c->h) break;
        int w = d->iWidthUsed;
        if (d->x + w > c->w) w = c->w - d->x;
        if (w <= 0) continue;
        memcpy(c->dst + dy * c->w + d->x, d->pPixels + y * d->iWidth, w * 2);
    }
    return 1;
}

// Centre-crop to a square and box/bilinear-scale to size x size.
static uint16_t* fitSquare(const uint16_t* src, int sw, int sh, int size) {
    uint16_t* out = (uint16_t*)heap_caps_malloc(size * size * 2, MALLOC_CAP_SPIRAM);
    if (!out) return nullptr;
    int side = sw < sh ? sw : sh;
    int ox = (sw - side) / 2, oy = (sh - side) / 2;
    for (int y = 0; y < size; ++y) {
        int y0 = oy + y * side / size, y1 = oy + (y + 1) * side / size;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < size; ++x) {
            int x0 = ox + x * side / size, x1 = ox + (x + 1) * side / size;
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint16_t* row = src + yy * sw;
                for (int xx = x0; xx < x1; ++xx) {
                    uint16_t p = row[xx];
                    r += p >> 11; g += (p >> 5) & 0x3F; b += p & 0x1F; n++;
                }
            }
            out[y * size + x] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
        }
    }
    return out;
}

// PNG (radio logos): decoded at full size, then scaled like a JPEG.
struct PngCtx { PNG* png; uint16_t* dst; int w, h; uint16_t* line; };
static int pngDraw(PNGDRAW* d) {
    PngCtx* c = (PngCtx*)d->pUser;
    // Never let the decoder write into the image directly: a line it reports
    // wider than the header said, or a row past the bottom, overran the block
    // and corrupted the heap (crash a few seconds after opening Favoris).
    if (d->y < 0 || d->y >= c->h || d->iWidth <= 0 || d->iWidth > 1024) return 1;
    c->png->getLineAsRGB565(d, c->line, PNG_RGB565_LITTLE_ENDIAN, 0x00161339);
    memcpy(c->dst + d->y * c->w, c->line, (d->iWidth < c->w ? d->iWidth : c->w) * 2);
    return 1;
}

static uint16_t* decodePng(const uint8_t* buf, int len, int size) {
    PNG* png = (PNG*)heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM);
    if (!png) return nullptr;
    new (png) PNG();
    uint16_t* full = nullptr;
    int w = 0, h = 0;
    // Byte 28 of the IHDR is the interlace method; PNGdec has no Adam7 support.
    const bool interlaced = len > 28 && buf[28] != 0;
    uint16_t* line = (uint16_t*)heap_caps_malloc(1024 * 2, MALLOC_CAP_SPIRAM);
    if (!interlaced && line && png->openRAM((uint8_t*)buf, len, pngDraw) == PNG_SUCCESS) {
        w = png->getWidth(); h = png->getHeight();
        int ch = 1;
        switch (png->getPixelType()) {
            case PNG_PIXEL_TRUECOLOR:       ch = 3; break;
            case PNG_PIXEL_GRAY_ALPHA:      ch = 2; break;
            case PNG_PIXEL_TRUECOLOR_ALPHA: ch = 4; break;
            default:                        ch = 1; break;
        }
        const int pitch = (w * ch * png->getBpp() + 7) / 8;
        const bool fits = 2 * (pitch + 1) <= PNG_MAX_BUFFERED_PIXELS;   // see platformio.ini
        if (!fits) Serial.printf("[SQ/ART] PNG %dx%d too wide for the decoder - skipped\n", w, h);
        if (fits && w > 0 && h > 0 && w <= 1024 && h <= 1024)
            full = (uint16_t*)heap_caps_calloc(w * h, 2, MALLOC_CAP_SPIRAM);
        if (full) {
            PngCtx ctx = {png, full, w, h, line};
            if (png->decode(&ctx, 0) != PNG_SUCCESS) { heap_caps_free(full); full = nullptr; }
        }
        png->close();
    }
    if (line) heap_caps_free(line);
    if (interlaced) Serial.println("[SQ/ART] Interlaced PNG - skipped");
    png->~PNG();
    heap_caps_free(png);
    if (!full) return nullptr;
    uint16_t* out = fitSquare(full, w, h, size);
    heap_caps_free(full);
    return out;
}

static uint16_t* decode(const uint8_t* buf, int len, int size) {
    if (len >= 8 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
        return decodePng(buf, len, size);
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
        Serial.printf("[SQ/ART] Unknown image format (%d bytes) - skipped\n", len);
        return nullptr;
    }
    JPEGDEC* jpeg = (JPEGDEC*)heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
    if (!jpeg) return nullptr;
    new (jpeg) JPEGDEC();
    uint16_t* full = nullptr;
    int fw = 0, fh = 0;
    bool progressive = false;
    bool too_big = false;
    if (jpeg->openRAM((uint8_t*)buf, len, jpegDraw)) {
        progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
        // stb decodes a progressive JPEG at full size and keeps every DCT
        // coefficient too: ~9 bytes a pixel, 18MB for a 1400px cover. Its
        // attempts drained the PSRAM the Wi-Fi and lwIP buffers live in, and
        // every connection failed for a while after. Small ones only.
        if (progressive && (jpeg->getWidth() > 640 || jpeg->getHeight() > 640)) {
            too_big = true;
            progressive = false;
            Serial.printf("[SQ/ART] Progressive JPEG %dx%d - too big to decode, skipped\n",
                          jpeg->getWidth(), jpeg->getHeight());
        }
        if (!progressive && !too_big) {
            int w = jpeg->getWidth(), h = jpeg->getHeight();
            int side = w < h ? w : h;
            int opt = 0, div = 1;
            // Largest reduction that still leaves at least `size` pixels.
            if (side / 8 >= size) { opt = JPEG_SCALE_EIGHTH; div = 8; }
            else if (side / 4 >= size) { opt = JPEG_SCALE_QUARTER; div = 4; }
            else if (side / 2 >= size) { opt = JPEG_SCALE_HALF; div = 2; }
            fw = (w + div - 1) / div;
            fh = (h + div - 1) / div;
            full = (uint16_t*)heap_caps_calloc(fw * fh, 2, MALLOC_CAP_SPIRAM);
            if (full) {
                JpegCtx ctx = {full, fw, fh};
                jpeg->setUserPointer(&ctx);
                jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
                if (!jpeg->decode(0, 0, opt)) { heap_caps_free(full); full = nullptr; }
            }
        }
        jpeg->close();
    }
    jpeg->~JPEGDEC();
    heap_caps_free(jpeg);
    if (progressive && !decodeJPEGProgressiveStb(buf, len, &full, &fw, &fh)) full = nullptr;
    if (!full) return nullptr;
    uint16_t* out = fitSquare(full, fw, fh, size);
    heap_caps_free(full);
    return out;
}

static void artTask(void*) {
    ArtReq r;
    for (;;) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) != pdTRUE) continue;
        if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(500)); }
        uint32_t t0 = millis();
        int n = download(r.url, s_dl, ART_MAX_BYTES);
        uint16_t* px = n > 0 ? decode(s_dl, n, r.size) : nullptr;
        if (!px) Serial.printf("[SQ/ART] Failed (%d) %.80s\n", n, r.url);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < ART_SLOTS; ++i) {
            ArtSlot& s = s_slots[i];
            if (s.key != r.key || s.state != ArtSlot::QUEUED) continue;
            if (px) {
                s.pixels = px;
                s.dsc = {};
                s.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
                s.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
                s.dsc.header.w = r.size;
                s.dsc.header.h = r.size;
                s.dsc.header.stride = r.size * 2;
                s.dsc.data = (const uint8_t*)px;
                s.dsc.data_size = r.size * r.size * 2;
                s.state = ArtSlot::READY;
                px = nullptr;
            } else {
                s.state = ArtSlot::FAILED;
            }
            break;
        }
        xSemaphoreGive(s_lock);
        if (px) heap_caps_free(px);   // slot was recycled meanwhile
        sq_art_version++;
        if (n > 0) Serial.printf("[SQ/ART] %d B -> %dpx in %lums (stack low %u)\n", n, r.size,
                                 millis() - t0, (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
}

void sqArtStart(void) {
    s_slots = (ArtSlot*)heap_caps_calloc(ART_SLOTS, sizeof(ArtSlot), MALLOC_CAP_SPIRAM);
    s_dl    = (uint8_t*)heap_caps_malloc(ART_MAX_BYTES, MALLOC_CAP_SPIRAM);
    s_lock  = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(ART_QUEUE_LEN, sizeof(ArtReq));
    // PSRAM stack: this task never writes flash, and mbedTLS allocates on the
    // heap (in PSRAM too, see main_square.cpp), so internal RAM is not needed.
    // Measured high-water mark: ~3KB used of 24KB with an https download.
    static StaticTask_t tcb;
    const size_t stack_words = 12288;
    StackType_t* stack = (StackType_t*)heap_caps_malloc(stack_words * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    xTaskCreateStaticPinnedToCore(artTask, "SqArt", stack_words, nullptr, 1, stack, &tcb, 0);
}

// The Sonos polling task asks this before each poll. On the P4 an artwork
// download competes with SOAP for the SDIO link to the Wi-Fi chip; the S3's
// radio is on-chip, so the square build never needs to hold polling back.
bool isAlbumArtPending() { return false; }
