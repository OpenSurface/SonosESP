#include "screenshot.h"
#include "config.h"
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

// ── State shared between the flush callback and the main task ───────────────
// s_request is set by the serial command and cleared by the flush callback;
// s_ready is set by the flush callback and cleared by the dump. Both are single
// writer / single reader in each direction, so a plain volatile bool is enough
// and no mutex is needed on the flush path (which must stay cheap).
static volatile bool s_request = false;
static volatile bool s_ready   = false;
static uint8_t*      s_buf     = nullptr;
static size_t        s_len     = 0;
static uint32_t      s_w = 0, s_h = 0;

// Base64 line length. MUST be a multiple of 4: a base64 group is 4 characters
// encoding 3 bytes, and splitting a group across a newline shifts every byte
// that follows it.
static const int kLineLen = 76;

void screenshotCaptureHook(const uint8_t* px_map, const lv_area_t* area) {
    // The entire cost of this feature on the hot path.
    if (!s_request || s_ready) return;

    // With LV_DISPLAY_RENDER_MODE_FULL this is always the whole screen, but a
    // partial area would mean copying DISPLAY_WIDTH*DISPLAY_HEIGHT out of a
    // smaller buffer — a read overrun. Leave the request pending and try again
    // on the next flush rather than capture a torn or short frame.
    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    if (w != DISPLAY_WIDTH || h != DISPLAY_HEIGHT) return;

    const size_t need = (size_t)w * h * 2;   // RGB565, LV_COLOR_DEPTH 16

    if (!s_buf) {
        // PSRAM deliberately: this is 768KB on the 4" and 1.2MB on the 7", and
        // internal DMA-capable heap is the scarce resource on this board.
        s_buf = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
        if (!s_buf) {
            s_request = false;
            Serial.println("[shot-err] could not allocate capture buffer");
            return;
        }
    }

    memcpy(s_buf, px_map, need);
    s_w = w; s_h = h; s_len = need;

    s_request = false;
    s_ready   = true;
}

static void dumpScreenshot(void) {
    static const char* B64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    // The byte count goes in the header so the host can prove it received the
    // whole thing. A truncated transfer otherwise decodes into a plausible-
    // looking image with a corrupt tail, which is worse than a clear failure.
    Serial.printf("[shot] %u %u %u\n", (unsigned)s_w, (unsigned)s_h, (unsigned)s_len);

    // USB-CDC drops writes it cannot place in the TX buffer — it does not block
    // and does not retry — so a host that falls behind silently loses whole
    // lines mid-stream. Raising the TX timeout makes write() wait for space
    // instead of discarding, and flushing periodically stops the buffer ever
    // filling in the first place. Without this the frame arrives 99% complete
    // with a few lines missing, which decodes to a shifted, corrupt image.
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(1000);
#endif

    const uint8_t* p = s_buf;
    char line[kLineLen + 1];
    int  col   = 0;
    int  lines = 0;

    for (size_t i = 0; i < s_len; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16;
        if (i + 1 < s_len) v |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < s_len) v |= p[i + 2];

        line[col++] = B64[(v >> 18) & 63];
        line[col++] = B64[(v >> 12) & 63];
        line[col++] = (i + 1 < s_len) ? B64[(v >> 6) & 63] : '=';
        line[col++] = (i + 2 < s_len) ? B64[ v        & 63] : '=';

        if (col >= kLineLen) {
            line[col] = 0;
            Serial.println(line);
            col = 0;
            // ~1MB of serial writes, on the task that owns the watchdog. The
            // flush drains the CDC buffer so it never overflows; the yield lets
            // the USB stack actually run, which it cannot do if this loop hogs
            // the core.
            if ((++lines & 0x1F) == 0) {
                Serial.flush();
                esp_task_wdt_reset();
                vTaskDelay(1);
            }
        }
    }
    if (col) { line[col] = 0; Serial.println(line); }

    Serial.println("[/shot]");
    Serial.flush();
    esp_task_wdt_reset();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);   // back to non-blocking so logging never stalls the UI
#endif

    // Give the memory straight back. Holding 768KB of PSRAM permanently for a
    // feature used occasionally is not a trade worth making on a board that has
    // had heap pressure problems.
    heap_caps_free(s_buf);
    s_buf = nullptr;
    s_len = 0;
    s_ready = false;
}

void screenshotPoll(void) {
    // A frame captured on a previous pass is ready — ship it.
    if (s_ready) { dumpScreenshot(); return; }

    static char cmd[24];
    static uint8_t n = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            cmd[n] = 0;
            if (n && strcmp(cmd, "screenshot") == 0) {
                if (s_request) {
                    Serial.println("[shot-busy] request already queued");
                } else {
                    s_request = true;
                    lv_obj_t* scr = lv_screen_active();
                    if (scr) lv_obj_invalidate(scr);   // force a flush to capture
                }
            }
            n = 0;
        } else if (n < sizeof(cmd) - 1) {
            cmd[n++] = c;
        } else {
            n = 0;   // overlong garbage — resync on the next newline
        }
    }
}
