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

// Base64 payload per line. MUST be a multiple of 4: a base64 group is 4
// characters encoding 3 bytes, and splitting a group across a newline shifts
// every byte that follows it.
static const int kLineLen = 76;
static const int kPerLine = (kLineLen / 4) * 3;   // 57 source bytes per line

// Every payload line carries its own index: "0A3F:AAAA...".
//
// Other FreeRTOS tasks print to this same port and nothing prevents one landing
// in the middle of a payload line. That line is then unrecoverable — and
// without an index there is no way to know WHICH line was damaged, so the frame
// simply arrives short. With an index the host knows exactly which lines it is
// missing and asks for those again, so a stray log line costs one retransmitted
// line instead of the entire capture.
static void emitLine(uint32_t idx, const char* payload) {
    char out[8 + kLineLen + 1];
    snprintf(out, sizeof(out), "%04X:%s", (unsigned)idx, payload);
    Serial.println(out);
}

// Encode one line's worth of source bytes (57 -> 76 base64 characters).
static void encodeLine(size_t byteOff, char* dst) {
    static const char* B64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t* p = s_buf;
    int col = 0;
    size_t end = byteOff + kPerLine;
    if (end > s_len) end = s_len;

    for (size_t i = byteOff; i < end; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16;
        if (i + 1 < s_len) v |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < s_len) v |= p[i + 2];
        dst[col++] = B64[(v >> 18) & 63];
        dst[col++] = B64[(v >> 12) & 63];
        dst[col++] = (i + 1 < s_len) ? B64[(v >> 6) & 63] : '=';
        dst[col++] = (i + 2 < s_len) ? B64[ v        & 63] : '=';
    }
    dst[col] = 0;
}

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

static void releaseBuffer(void) {
    if (s_buf) { heap_caps_free(s_buf); s_buf = nullptr; }
    s_len   = 0;
    s_ready = false;
}

static void dumpScreenshot(void) {
    const uint32_t nLines = (uint32_t)((s_len + kPerLine - 1) / kPerLine);

    // The byte and line counts go in the header so the host can prove it
    // received everything. A truncated transfer otherwise decodes into a
    // plausible-looking image with a corrupt tail — worse than a clear failure.
    Serial.printf("[shot] %u %u %u %u\n",
                  (unsigned)s_w, (unsigned)s_h, (unsigned)s_len, (unsigned)nLines);

    // USB-CDC drops writes it cannot place in the TX buffer — it does not block
    // and does not retry — so a host that falls behind silently loses whole
    // lines. Raising the TX timeout makes write() wait for space instead of
    // discarding; flushing periodically stops the buffer ever filling.
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(1000);
#endif

    char payload[kLineLen + 1];
    for (uint32_t i = 0; i < nLines; i++) {
        encodeLine((size_t)i * kPerLine, payload);
        emitLine(i, payload);

        // Deliberately NO vTaskDelay here. Yielding hands the CPU to tasks that
        // then print into the middle of a line, and every such line costs a
        // retransmit. The watchdog needs the reset, not a yield.
        if ((i & 0x1F) == 0) {
            Serial.flush();
            esp_task_wdt_reset();
        }
    }

    Serial.println("[/shot]");
    Serial.flush();
    esp_task_wdt_reset();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);   // back to non-blocking so logging never stalls the UI
#endif

    s_ready = false;   // buffer deliberately kept, for "shotline" retransmits
}

// Resend one line. The host asks for these when a log line has landed inside
// one. The capture buffer is still held from the dump.
static void resendLine(uint32_t idx) {
    if (!s_buf || (size_t)idx * kPerLine >= s_len) {
        Serial.printf("[shot-err] line %u unavailable\n", (unsigned)idx);
        return;
    }
    char payload[kLineLen + 1];
    encodeLine((size_t)idx * kPerLine, payload);
    emitLine(idx, payload);
    Serial.flush();
}

void screenshotPoll(void) {
    // A frame captured on a previous pass is ready — ship it.
    if (s_ready) { dumpScreenshot(); return; }

    static char cmd[32];
    static uint8_t n = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            cmd[n] = 0;
            if (n) {
                if (strcmp(cmd, "screenshot") == 0) {
                    if (s_request) {
                        Serial.println("[shot-busy] request already queued");
                    } else {
                        releaseBuffer();                    // drop any previous frame
                        s_request = true;
                        lv_obj_t* scr = lv_screen_active();
                        if (scr) lv_obj_invalidate(scr);    // force a flush to capture
                    }
                } else if (strncmp(cmd, "shotline ", 9) == 0) {
                    resendLine((uint32_t)strtoul(cmd + 9, nullptr, 16));
                } else if (strcmp(cmd, "shotfree") == 0) {
                    releaseBuffer();
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
