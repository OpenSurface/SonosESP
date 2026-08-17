/**
 * UI Album Art Handling
 * Album art loading with:
 *   - ESP32-P4 hardware JPEG decoder (baseline JPEG, div-8 dimensions)
 *   - JPEGDEC SW fallback (baseline JPEG, non-div-8 or HW failure)
 *   - stb_image (progressive JPEG / SOF2 — full all-scan decode)
 *   - PNGdec (PNG)
 *   - Bilinear scaling to 420x420 display size
 */

#include "ui_common.h"
#include "config.h"
#include "ui_theme.h"
#include "ui_network_guard.h"
#include <lwip/sockets.h>   // lwip_setsockopt / SO_RCVBUF
#include <lwip/netdb.h>     // getaddrinfo / freeaddrinfo (for artPreConnectHTTP)
#include <PNGdec.h>
// Undefine shared macros from PNGdec before JPEGDEC redefines them (same author, same macros)
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <JPEGDEC.h>

// ESP32-P4 Hardware JPEG Decoder
#include "driver/jpeg_decode.h"
static jpeg_decoder_handle_t hw_jpeg_decoder = nullptr;

// stb_image full progressive JPEG decoder (defined in stb_jpeg.cpp)
extern bool decodeJPEGProgressiveStb(const uint8_t* buf, size_t len,
                                     uint16_t** out, int* out_w, int* out_h);

// Software JPEG decoder callback globals (set before decode, cleared after)
static uint16_t* sw_jpeg_output = nullptr;
static int sw_jpeg_width = 0;
static int sw_jpeg_height = 0;

// ── Pre-connect helpers ───────────────────────────────────────────────────────
// Root cause of pkt_rxbuff :928: server blasts initial TCP cwnd (~10 segments)
// into C6 pkt_rxbuff before P4 lwIP can ACK. SO_RCVBUF does NOT control the
// TCP window advertisement (pcb->rcv_wnd = 65534 baked into pre-compiled liblwip.a).
// Setting SO_RCVBUF constrains the app-layer recv buffer, which gradually reduces
// the advertised window as the buffer fills — limiting subsequent bursts but NOT
// the initial cwnd blast. HTTPClient::connect() reuses the pre-connected socket
// instead of reconnecting (it checks connected() first — returns true).

// Parse "http://HOST:PORT/PATH" URL. Fills host_buf, *port_out. Returns true on success.
static bool artParseHttpHost(const char* url, char* host_buf, size_t host_buf_len, uint16_t* port_out) {
    if (strncmp(url, "http://", 7) != 0) return false;
    const char* p = url + 7;
    const char* slash = strchr(p, '/');
    size_t auth_len = slash ? (size_t)(slash - p) : strlen(p);
    const char* colon = (const char*)memchr(p, ':', auth_len);
    size_t host_len;
    if (colon) {
        host_len = (size_t)(colon - p);
        *port_out = (uint16_t)atoi(colon + 1);
    } else {
        host_len = auth_len;
        *port_out = 80;
    }
    if (host_len == 0 || host_len >= host_buf_len) return false;
    memcpy(host_buf, p, host_len);
    host_buf[host_len] = '\0';
    return true;
}

// Strip HTTP chunked transfer encoding in-place.
// Sonos getaa at :1400 ignores HTTP/1.0 requests and returns chunked for x-sonos-http
// sources (BBC Sounds, Audible, Amazon Music). Without dechunking, the buffer starts with
// "1000\r\n" (hex chunk-size header) before the JPEG/PNG magic bytes, causing format
// detection to fail and art to show as placeholder.
// Returns the new data length; leaves buf unchanged and returns len if not chunked.
static size_t dechunkBuffer(uint8_t* buf, size_t len) {
    if (len < 5) return len;
    // Quick check: chunked data starts with ASCII hex digit(s) followed by \r\n
    uint8_t c0 = buf[0];
    bool firstIsHex = (c0 >= '0' && c0 <= '9') || (c0 >= 'a' && c0 <= 'f') || (c0 >= 'A' && c0 <= 'F');
    if (!firstIsHex) return len;
    // Confirm by finding \r\n within the first 10 bytes
    bool hasCRLF = false;
    for (int i = 1; i < 10 && i < (int)len - 1; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n') { hasCRLF = true; break; }
    }
    if (!hasCRLF) return len;

    uint8_t* src = buf;
    uint8_t* dst = buf;
    const uint8_t* end = buf + len;
    size_t newLen = 0;

    while (src < end) {
        // Parse hex chunk size
        char sizeBuf[16]; int sLen = 0;
        while (src < end && sLen < 15) {
            uint8_t ch = *src;
            if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))
                { sizeBuf[sLen++] = ch; src++; }
            else break;
        }
        sizeBuf[sLen] = '\0';
        if (sLen == 0) break;
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') src += 2;
        size_t chunkSz = (size_t)strtoul(sizeBuf, nullptr, 16);
        if (chunkSz == 0) break;  // terminal chunk
        size_t avail = (size_t)(end - src);
        if (chunkSz > avail) chunkSz = avail;
        if (dst != src) memmove(dst, src, chunkSz);
        dst += chunkSz; src += chunkSz; newLen += chunkSz;
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') src += 2;
    }
    return (newLen > 0) ? newLen : len;
}

// Pre-connect HTTP socket with SO_RCVBUF set BEFORE TCP SYN.
// Returns connected WiFiClient on success, unconnected on failure (caller falls back to http.begin(url)).
static WiFiClient artPreConnectHTTP(const char* url, int timeout_ms) {
    char host[128];
    uint16_t port = 80;
    if (!artParseHttpHost(url, host, sizeof(host), &port)) return WiFiClient();

    // Resolve host (IP string → no DNS; hostname → DNS query)
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return WiFiClient();

    int sockfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { freeaddrinfo(res); return WiFiClient(); }

    // Set SO_RCVBUF before connect — constrains app recv buffer → limits subsequent
    // window updates after the initial cwnd burst (does not affect SYN-ACK window).
    int rcvbuf = ART_TCP_RCVBUF;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // Non-blocking connect with select timeout (matches NetworkClient::connect())
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
    lwip_connect(sockfd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    fd_set fdset;
    struct timeval tv;
    FD_ZERO(&fdset); FD_SET(sockfd, &fdset);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (lwip_select(sockfd + 1, nullptr, &fdset, nullptr, &tv) <= 0) {
        lwip_close(sockfd); return WiFiClient();
    }
    int sockerr = 0; socklen_t slen = sizeof(int);
    lwip_getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &sockerr, &slen);
    if (sockerr != 0) { lwip_close(sockfd); return WiFiClient(); }

    // Set timeouts, switch back to blocking (matches NetworkClient::connect())
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) & (~O_NONBLOCK));

    // Set SO_RCVBUF AGAIN now that the PCB exists (PCB is created by lwip_connect()).
    // The pre-connect setsockopt (line ~90) races with the SYN: the PCB doesn't exist
    // yet, so lwIP cannot apply the window constraint to the SYN. The server sees the
    // default TCP_WND (65534) and can blast up to 65KB immediately. By setting SO_RCVBUF
    // here — after the 3-way handshake completes but BEFORE the GET request is sent —
    // lwIP updates pcb->rcv_wnd to 8192. The GET's ACK will carry this small window.
    // Server sees window=8192 and limits its initial response burst to ~6KB → safe.
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    return WiFiClient(sockfd);  // NetworkClient(int fd) — marks as _connected=true
}
// ─────────────────────────────────────────────────────────────────────────────

// ── Diagnostic logging helpers ────────────────────────────────────────────────
static void artLogSDIO(const char* tag) {
    unsigned long now = millis();
    Serial.printf("[ART/%s] SDIO: 500=%ldms net=%ldms https=%ldms q=%ldms art_end=%ldms adlp=%d\n", tag,
        last_transient_500_ms    ? (long)(now - last_transient_500_ms)    : -1L,
        last_network_end_ms      ? (long)(now - last_network_end_ms)      : -1L,
        last_https_end_ms        ? (long)(now - last_https_end_ms)        : -1L,
        last_queue_fetch_time    ? (long)(now - last_queue_fetch_time)    : -1L,
        last_art_download_end_ms ? (long)(now - last_art_download_end_ms) : -1L,
        (int)art_download_in_progress);
}
static void artLogMem(const char* tag) {
    Serial.printf("[ART/%s] MEM: heap=%u dma=%u psram=%u stk=%u\n", tag,
        heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
        heap_caps_get_free_size(MALLOC_CAP_DMA),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        uxTaskGetStackHighWaterMark(NULL));
}
// ─────────────────────────────────────────────────────────────────────────────

// Album Art Functions
static uint32_t color_r_sum = 0, color_g_sum = 0, color_b_sum = 0;
static int color_sample_count = 0;
static int jpeg_image_width = 0;   // Store full image width for callback
static int jpeg_image_height = 0;  // Store full image height for callback
static int jpeg_output_width = 0;  // Actual decoded output width
static int jpeg_output_height = 0; // Actual decoded output height
static uint16_t* jpeg_decode_buffer = nullptr;  // Destination for JPEG/PNG decode

// PNG decoder instance
static PNG png;

// Smooth background color transition state
static uint32_t current_bg_color = 0x1a1a1a;
static uint32_t target_bg_color = 0x1a1a1a;

// 2-slot LRU album art cache in PSRAM — instant display on prev/next, no re-download
struct ArtCacheEntry {
    char url[512];
    uint16_t* pixels;        // ART_PX*ART_PX*2 bytes (~352KB each)
    uint32_t dominant_color;
    bool valid;
};
static ArtCacheEntry art_cache[2] = {};
static int art_cache_lru = 0;  // Index of most recently used slot

// Interpolate a single 8-bit channel
static inline uint8_t lerp8(uint8_t a, uint8_t b, int t) {
    return (uint8_t)(a + ((int)(b - a) * t) / 255);
}

// Apply interpolated color to all UI elements (called by LVGL animation engine)
static void color_anim_cb(void* var, int32_t t) {
    uint8_t r = lerp8((current_bg_color >> 16) & 0xFF, (target_bg_color >> 16) & 0xFF, t);
    uint8_t g = lerp8((current_bg_color >> 8) & 0xFF, (target_bg_color >> 8) & 0xFF, t);
    uint8_t b = lerp8(current_bg_color & 0xFF, target_bg_color & 0xFF, t);

    lv_color_t color = lv_color_make(r, g, b);
    if (panel_art) lv_obj_set_style_bg_color(panel_art, color, LV_PART_MAIN);
    if (panel_right) lv_obj_set_style_bg_color(panel_right, color, LV_PART_MAIN);

    // Brighten by 3x (capped at 255) with minimum floor of 80
    uint8_t br = (uint8_t)max(min((int)r * 3, 255), 80);
    uint8_t bg = (uint8_t)max(min((int)g * 3, 255), 80);
    uint8_t bb = (uint8_t)max(min((int)b * 3, 255), 80);
    lv_color_t bright = lv_color_make(br, bg, bb);

    if (slider_progress) {
        lv_obj_set_style_bg_color(slider_progress, bright, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_progress, bright, LV_PART_KNOB);
    }
    if (btn_play) lv_obj_set_style_bg_color(btn_play, bright, LV_STATE_PRESSED);
    if (btn_prev) {
        lv_obj_set_style_bg_color(btn_prev, bright, LV_STATE_PRESSED);
        lv_obj_t* ico = lv_obj_get_child(btn_prev, 0);
        if (ico) lv_obj_set_style_text_color(ico, bright, LV_STATE_PRESSED);
    }
    if (btn_next) {
        lv_obj_set_style_bg_color(btn_next, bright, LV_STATE_PRESSED);
        lv_obj_t* ico = lv_obj_get_child(btn_next, 0);
        if (ico) lv_obj_set_style_text_color(ico, bright, LV_STATE_PRESSED);
    }
    if (btn_mute) lv_obj_set_style_bg_color(btn_mute, bright, LV_STATE_PRESSED);
    if (btn_shuffle) lv_obj_set_style_bg_color(btn_shuffle, bright, LV_STATE_PRESSED);
    if (btn_repeat) lv_obj_set_style_bg_color(btn_repeat, bright, LV_STATE_PRESSED);
    if (btn_queue) lv_obj_set_style_bg_color(btn_queue, bright, LV_STATE_PRESSED);

    // Theme backdrop rides the same interpolation, so an ambient background
    // cross-fades between tracks instead of snapping. No-op for the Classic theme.
    themeApplyBackdrop(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

// Save final color as new baseline when animation completes
static void color_anim_done_cb(lv_anim_t* a) {
    current_bg_color = target_bg_color;
}

// Smoothly transition background color over 500ms
void setBackgroundColor(uint32_t hex_color) {
    target_bg_color = hex_color;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &target_bg_color);
    lv_anim_set_values(&anim, 0, 255);
    lv_anim_set_duration(&anim, 300);
    lv_anim_set_exec_cb(&anim, color_anim_cb);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, color_anim_done_cb);
    lv_anim_start(&anim);
}

// Sample pixels for dominant color extraction
void sampleDominantColor(uint16_t* buffer, int width, int height) {
    color_r_sum = 0;
    color_g_sum = 0;
    color_b_sum = 0;
    color_sample_count = 0;

    // Sample edge pixels (top, bottom, left, right margins)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Sample only edges (50px margin) and every 20th pixel (optimized: was 15, saves ~25% sampling time)
            if (((x | y) % 20 == 0) && (y < 50 || y > height - 50 || x < 50 || x > width - 50)) {
                uint16_t pixel = buffer[y * width + x];

                // Convert RGB565 to RGB888
                color_r_sum += ((pixel >> 8) & 0xF8);
                color_g_sum += ((pixel >> 3) & 0xFC);
                color_b_sum += ((pixel << 3) & 0xF8);
                color_sample_count++;
            }
        }
    }
}

// Fast bilinear scaling using fixed-point math
// src_stride: row width in pixels of the source buffer (may differ from src_w due to HW decoder padding)
void scaleImageBilinear(uint16_t* src, int src_w, int src_h, int src_stride, uint16_t* dst, int dst_w, int dst_h) {
    // Validate dimensions to prevent overflow (should never happen with 2048x2048 limit, but be safe)
    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 ||
        src_w > 4096 || src_h > 4096 || dst_w > 4096 || dst_h > 4096) {
        Serial.printf("[ART] Invalid scaling dimensions: %dx%d -> %dx%d\n", src_w, src_h, dst_w, dst_h);
        return;
    }

    // Use 16.16 fixed-point for integer math (faster than float)
    // Cast to int64_t to prevent overflow during shift, then cast back to int
    int x_ratio = (int)(((int64_t)(src_w - 1) << 16) / dst_w);
    int y_ratio = (int)(((int64_t)(src_h - 1) << 16) / dst_h);

    for (int dst_y = 0; dst_y < dst_h; dst_y++) {
        int src_y_fp = dst_y * y_ratio;
        int y0 = src_y_fp >> 16;
        int y1 = min(y0 + 1, src_h - 1);
        int y_weight = (src_y_fp >> 8) & 0xFF;  // 0-255

        uint16_t* dst_row = &dst[dst_y * dst_w];
        uint16_t* src_row0 = &src[y0 * src_stride];
        uint16_t* src_row1 = &src[y1 * src_stride];

        for (int dst_x = 0; dst_x < dst_w; dst_x++) {
            int src_x_fp = dst_x * x_ratio;
            int x0 = src_x_fp >> 16;
            int x1 = min(x0 + 1, src_w - 1);
            int x_weight = (src_x_fp >> 8) & 0xFF;  // 0-255

            // Get 4 surrounding pixels
            uint16_t p00 = src_row0[x0];
            uint16_t p10 = src_row0[x1];
            uint16_t p01 = src_row1[x0];
            uint16_t p11 = src_row1[x1];

            // Extract RGB components (RGB565)
            uint8_t r00 = (p00 >> 11) & 0x1F;
            uint8_t g00 = (p00 >> 5) & 0x3F;
            uint8_t b00 = p00 & 0x1F;

            uint8_t r10 = (p10 >> 11) & 0x1F;
            uint8_t g10 = (p10 >> 5) & 0x3F;
            uint8_t b10 = p10 & 0x1F;

            uint8_t r01 = (p01 >> 11) & 0x1F;
            uint8_t g01 = (p01 >> 5) & 0x3F;
            uint8_t b01 = p01 & 0x1F;

            uint8_t r11 = (p11 >> 11) & 0x1F;
            uint8_t g11 = (p11 >> 5) & 0x3F;
            uint8_t b11 = p11 & 0x1F;

            // Bilinear interpolation using integer math
            // top = p00 * (1-x) + p10 * x
            // bot = p01 * (1-x) + p11 * x
            // result = top * (1-y) + bot * y
            int r_top = (r00 * (256 - x_weight) + r10 * x_weight) >> 8;
            int g_top = (g00 * (256 - x_weight) + g10 * x_weight) >> 8;
            int b_top = (b00 * (256 - x_weight) + b10 * x_weight) >> 8;

            int r_bot = (r01 * (256 - x_weight) + r11 * x_weight) >> 8;
            int g_bot = (g01 * (256 - x_weight) + g11 * x_weight) >> 8;
            int b_bot = (b01 * (256 - x_weight) + b11 * x_weight) >> 8;

            uint8_t r = (r_top * (256 - y_weight) + r_bot * y_weight) >> 8;
            uint8_t g = (g_top * (256 - y_weight) + g_bot * y_weight) >> 8;
            uint8_t b = (b_top * (256 - y_weight) + b_bot * y_weight) >> 8;

            // Pack back to RGB565
            dst_row[dst_x] = (r << 11) | (g << 5) | b;
        }
    }
}

// JPEGDEC SW callback - decode MCU blocks to PSRAM output buffer
static int jpegDrawCallback(JPEGDRAW* pDraw) {
    if (!sw_jpeg_output || !pDraw->pPixels) return 0;

    // Copy decoded MCU block to output buffer
    for (int y = 0; y < pDraw->iHeight; y++) {
        int dst_y = pDraw->y + y;
        if (dst_y < 0 || dst_y >= sw_jpeg_height) continue;
        int dst_x = pDraw->x;
        if (dst_x < 0 || dst_x >= sw_jpeg_width) continue;
        int copy_w = min(pDraw->iWidth, sw_jpeg_width - dst_x);
        memcpy(&sw_jpeg_output[dst_y * sw_jpeg_width + dst_x],
               &pDraw->pPixels[y * pDraw->iWidth],
               copy_w * sizeof(uint16_t));
    }
    return 1;  // Continue decoding
}

// Software JPEG decode fallback for non-progressive JPEG (non-div-8 dimensions, HW failures).
// Progressive JPEG is handled separately by decodeJPEGProgressiveStb() (stb_image).
// Returns true on success. Caller must heap_caps_free(*out_buffer) when done.
static bool decodeJPEGSoftware(uint8_t* buf, size_t len, uint16_t** out_buffer, int* out_w, int* out_h) {
    // Allocate JPEGDEC in PSRAM (~18KB struct - too large for stack, wastes DRAM if static)
    JPEGDEC* sw_jpeg = (JPEGDEC*)heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
    if (!sw_jpeg) {
        Serial.println("[ART] SW JPEG alloc failed for decoder");
        return false;
    }
    new (sw_jpeg) JPEGDEC();  // Placement new to construct

    bool success = false;
    if (sw_jpeg->openRAM(buf, len, jpegDrawCallback)) {
        int w = sw_jpeg->getWidth();
        int h = sw_jpeg->getHeight();

        if (w <= 0 || h <= 0 || w > 2048 || h > 2048) {
            Serial.printf("[ART] SW JPEG invalid dimensions: %dx%d\n", w, h);
            sw_jpeg->close();
        } else {
            // Allocate output buffer in PSRAM
            size_t buf_size = (size_t)w * h * 2;
            uint16_t* output = (uint16_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
            if (!output) {
                Serial.printf("[ART] SW JPEG alloc failed: %d bytes\n", (int)buf_size);
                sw_jpeg->close();
            } else {
                memset(output, 0, buf_size);

                // Set globals for callback
                sw_jpeg_output = output;
                sw_jpeg_width = w;
                sw_jpeg_height = h;

                // Decode with RGB565 little-endian output (matches LVGL)
                sw_jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
                int result = sw_jpeg->decode(0, 0, 0);
                sw_jpeg->close();
                sw_jpeg_output = nullptr;

                if (result == 1) {
                    Serial.printf("[ART] SW JPEG decoded: %dx%d\n", w, h);
                    *out_buffer = output;
                    *out_w = w;
                    *out_h = h;
                    success = true;
                } else {
                    Serial.printf("[ART] SW JPEG decode failed: %d\n", result);
                    heap_caps_free(output);
                }
            }
        }
    } else {
        Serial.println("[ART] SW JPEG openRAM failed");
    }

    sw_jpeg->~JPEGDEC();  // Explicit destructor
    heap_caps_free(sw_jpeg);
    return success;
}

// PNGdec callback - decode to temporary buffer
static int pngDraw(PNGDRAW* pDraw) {
    if (!jpeg_decode_buffer) return 0;

    // Get RGB565 pixels from PNG decoder
    // Static: png is a single global instance, pngDraw is only ever called from the
    // art task — no reentrancy. Avoids stack allocation per row during PNG decode.
    //
    // SIZE IS SAFETY-CRITICAL: getLineAsRGB565() writes pDraw->iWidth pixels and takes
    // no length argument, so this buffer MUST hold the widest PNG that decodeToRGB565()
    // accepts (PNG_MAX_DECODE_WIDTH). It was previously 512 while widths up to 2048 were
    // allowed through — a 600px station logo (we request maxWidth=600) overran it.
    static uint16_t lineBuffer[PNG_MAX_DECODE_WIDTH];
    int w = pDraw->iWidth;
    if (w > PNG_MAX_DECODE_WIDTH) return 0;   // rejected upstream; never write past the buffer

    // Convert PNG line to RGB565
    png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    int y = pDraw->y;
    if (y < 0 || y >= jpeg_image_height) return 1;

    // Copy to decode buffer
    int copy_w = w;
    if (copy_w > jpeg_image_width) copy_w = jpeg_image_width;

    memcpy(&jpeg_decode_buffer[y * jpeg_image_width], lineBuffer, copy_w * 2);

    // Track output dimensions
    if (copy_w > jpeg_output_width) jpeg_output_width = copy_w;
    if (y + 1 > jpeg_output_height) jpeg_output_height = y + 1;

    return 1;  // Continue decoding
}

// ── Decode result ─────────────────────────────────────────────────────────────
struct DecodeResult {
    uint16_t* pixels;  // PSRAM RGB565 buffer; caller must heap_caps_free() if ok==true
    int       w;       // actual image width
    int       h;       // actual image height
    int       stride;  // row pitch in pixels (may differ from w for HW JPEG padded output)
    bool      ok;
};
static const DecodeResult kDecodeFail = {nullptr, 0, 0, 0, false};

// ── Unified decode dispatcher ──────────────────────────────────────────────────
// Detects format, strips COM markers, dispatches to the appropriate decoder.
// On success: returns allocated PSRAM RGB565 buffer — caller must heap_caps_free(result.pixels).
// On failure: returns kDecodeFail (pixels==nullptr).
static DecodeResult decodeToRGB565(uint8_t* buf, size_t len, bool isJPEG, bool isPNG) {

    // ── PNG PATH ──────────────────────────────────────────────────────────────
    if (isPNG) {
        int pngResult = png.openRAM(buf, (int)len, pngDraw);
        if (pngResult != 0) {
            Serial.printf("[ART] PNG openRAM failed - error code: %d\n", pngResult);
            return kDecodeFail;
        }
        int w = png.getWidth();
        int h = png.getHeight();
        // Width limit is tied to pngDraw()'s row buffer — see PNG_MAX_DECODE_WIDTH.
        if (w <= 0 || h <= 0 || w > PNG_MAX_DECODE_WIDTH || h > PNG_MAX_DECODE_HEIGHT ||
            (size_t)w * h * 2 > 10 * 1024 * 1024) {
            Serial.printf("[ART] Invalid PNG dimensions: %dx%d (max %dx%d, 10MB)\n",
                          w, h, PNG_MAX_DECODE_WIDTH, PNG_MAX_DECODE_HEIGHT);
            png.close();
            return kDecodeFail;
        }
        size_t decoded_size = (size_t)w * h * 2;
        uint16_t* decoded_buffer = (uint16_t*)heap_caps_malloc(decoded_size, MALLOC_CAP_SPIRAM);
        if (!decoded_buffer) {
            Serial.printf("[ART] Failed to allocate %d bytes for decoded image\n", (int)decoded_size);
            png.close();
            return kDecodeFail;
        }
        memset(decoded_buffer, 0, decoded_size);

        // Set globals for pngDraw callback
        jpeg_decode_buffer  = decoded_buffer;
        jpeg_image_width    = w;
        jpeg_image_height   = h;
        jpeg_output_width   = 0;
        jpeg_output_height  = 0;

        png.decode(NULL, 0);
        png.close();
        jpeg_decode_buffer = nullptr;

        // Compact step: if callback only wrote a sub-region, crop to actual output
        int out_w = (jpeg_output_width  > 0) ? jpeg_output_width  : w;
        int out_h = (jpeg_output_height > 0) ? jpeg_output_height : h;

        uint16_t* src_buffer = decoded_buffer;
        if (out_w != w || out_h != h) {
            size_t compact_size = (size_t)out_w * out_h * 2;
            uint16_t* compact_buffer = (uint16_t*)heap_caps_malloc(compact_size, MALLOC_CAP_SPIRAM);
            if (compact_buffer) {
                for (int y = 0; y < out_h; y++)
                    memcpy(compact_buffer + (size_t)y * out_w,
                           decoded_buffer + (size_t)y * w,
                           out_w * 2);
                heap_caps_free(decoded_buffer);
                src_buffer = compact_buffer;
            } else {
                // compact alloc failed — use full decoded_buffer with original dims
                out_w = w;
                out_h = h;
            }
        }
        return {src_buffer, out_w, out_h, out_w, true};
    }

    // ── JPEG PATH ─────────────────────────────────────────────────────────────
    if (isJPEG) {

        if (hw_jpeg_decoder) {
            // Step 0: strip COM markers (0xFFFE) — HW decoder returns error 258 on them.
            // While stripping, also detect a "Lavc" signature inside any COM segment.
            // FFmpeg/libavcodec-encoded JPEGs (e.g. some Spotify CDN images) ship with
            // optimized Huffman tables that trip the ESP32-P4 HW Huffman decoder and
            // render as horizontal stripes. When detected, we route to the SW decoder.
            size_t cleaned_size = len;
            bool has_lavc_marker = false;
            {
                uint8_t* p   = buf;
                uint8_t* end = buf + len;
                uint8_t* dst = buf;
                while (p + 3 < end) {
                    if (p[0] == 0xFF && p[1] == 0xFE) {
                        uint16_t marker_len = ((uint16_t)p[2] << 8) | p[3];
                        if (marker_len < 2) break;
                        // Payload runs from p+4 for (marker_len - 2) bytes. Scan for "Lavc".
                        if (!has_lavc_marker && marker_len >= 6 && p + 2 + marker_len <= end) {
                            uint8_t* pl     = p + 4;
                            uint8_t* pl_end = p + 2 + marker_len;
                            for (uint8_t* sc = pl; sc + 4 <= pl_end; sc++) {
                                if (sc[0] == 'L' && sc[1] == 'a' && sc[2] == 'v' && sc[3] == 'c') {
                                    has_lavc_marker = true;
                                    break;
                                }
                            }
                        }
                        p += 2 + marker_len;
                        continue;
                    }
                    if (dst != p) *dst = *p;
                    dst++; p++;
                }
                while (p < end) { if (dst != p) *dst = *p; dst++; p++; }
                cleaned_size = (size_t)(dst - buf);
            }

            // Step 0b: scan for SOF2 (progressive JPEG marker)
            bool is_progressive = false;
            bool use_sw_fallback = false;
            for (size_t pi = 0; pi + 1 < cleaned_size; pi++) {
                if (buf[pi] == 0xFF && buf[pi+1] == 0xC2) { is_progressive = true; break; }
            }
            if (is_progressive) use_sw_fallback = true;
            if (has_lavc_marker) {
                Serial.println("[ART] FFmpeg/Lavc-encoded JPEG detected — routing to SW decoder");
                use_sw_fallback = true;
            }

            // Step 1: HW header parse
            jpeg_decode_picture_info_t pic_info = {};
            uint16_t* decoded_pixels = nullptr;
            int final_w = 0, final_h = 0, final_stride = 0;
            bool hw_decode_success = false;

            if (!use_sw_fallback) {
                esp_err_t hw_ret = jpeg_decoder_get_info(buf, cleaned_size, &pic_info);
                int w = (int)pic_info.width;
                int h = (int)pic_info.height;
                if (hw_ret == ESP_OK && w == 0 && h == 0) {
                    // Progressive detected by HW parser
                    use_sw_fallback = true;
                    is_progressive  = true;
                } else if (hw_ret != ESP_OK) {
                    Serial.printf("[ART] HW header parse failed: %d, trying SW fallback\n", hw_ret);
                    use_sw_fallback = true;
                } else if (w <= 0 || h <= 0 || w > 2048 || h > 2048) {
                    Serial.printf("[ART] JPEG dimensions out of range: %dx%d\n", w, h);
                    return kDecodeFail;
                } else {
                    // ESP32-P4 HW JPEG decoder produces 16-byte-aligned output regardless
                    // of subsampling (per ESP-IDF docs for YUV420/422 — and empirically the
                    // same applies to YUV444). Dimensions not divisible by 16 trip a stride
                    // mismatch that renders as horizontal banding. Force SW for those.
                    bool hw_compatible = (w % 16 == 0) && (h % 16 == 0);
                    if (!hw_compatible) {
                        Serial.printf("[ART] JPEG %dx%d not HW-compatible (non-div-16), using SW fallback\n", w, h);
                        use_sw_fallback = true;
                    } else {
                        // HW FAST PATH
                        int out_w = ((w + 15) / 16) * 16;
                        int out_h = ((h + 15) / 16) * 16;
                        bool is_grayscale = (pic_info.sample_method == JPEG_DOWN_SAMPLING_GRAY);
                        Serial.printf("[ART] JPEG: %dx%d (output: %dx%d)%s\n", w, h, out_w, out_h,
                                      is_grayscale ? " [GRAYSCALE]" : "");
                        size_t bytes_per_pixel = is_grayscale ? 1 : 2;
                        size_t decoded_size_hw = (size_t)out_w * out_h * bytes_per_pixel;
                        jpeg_decode_memory_alloc_cfg_t rx_mem_cfg = {
                            .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER
                        };
                        size_t rx_buffer_size = 0;
                        uint8_t* hw_out_buf = (uint8_t*)jpeg_alloc_decoder_mem(decoded_size_hw,
                                                                                &rx_mem_cfg,
                                                                                &rx_buffer_size);
                        if (!hw_out_buf) {
                            Serial.printf("[ART] DMA alloc failed (%d bytes), trying SW fallback\n",
                                          (int)decoded_size_hw);
                            use_sw_fallback = true;
                        } else {
                            jpeg_decode_cfg_t decode_cfg = {
                                .output_format = is_grayscale ? JPEG_DECODE_OUT_FORMAT_GRAY
                                                              : JPEG_DECODE_OUT_FORMAT_RGB565,
                                .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
                                .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
                            };
                            uint32_t out_size = 0;
                            esp_err_t hw_ret2 = jpeg_decoder_process(hw_jpeg_decoder, &decode_cfg,
                                                                      buf, cleaned_size,
                                                                      hw_out_buf, rx_buffer_size,
                                                                      &out_size);
                            if (hw_ret2 != ESP_OK) {
                                Serial.printf("[ART] HW decode failed: %d, trying SW fallback\n", hw_ret2);
                                heap_caps_free(hw_out_buf);
                                use_sw_fallback = true;
                            } else {
                                if (is_grayscale) {
                                    Serial.println("[ART] Converting grayscale to RGB565");
                                    uint16_t* rgb_buf = (uint16_t*)heap_caps_malloc(
                                        (size_t)out_w * out_h * 2, MALLOC_CAP_SPIRAM);
                                    if (rgb_buf) {
                                        for (int i = 0; i < out_w * out_h; i++) {
                                            uint8_t g = hw_out_buf[i];
                                            rgb_buf[i] = ((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3);
                                        }
                                        heap_caps_free(hw_out_buf);
                                        hw_out_buf = (uint8_t*)rgb_buf;
                                    } else {
                                        heap_caps_free(hw_out_buf);
                                        hw_ret2 = ESP_FAIL;
                                        use_sw_fallback = true;
                                    }
                                }
                                if (hw_ret2 == ESP_OK) {
                                    Serial.printf("[ART] HW decoded: %u bytes\n", out_size);
                                    decoded_pixels   = (uint16_t*)hw_out_buf;
                                    final_w          = w;
                                    final_h          = h;
                                    final_stride     = out_w;
                                    hw_decode_success = true;
                                }
                            }
                        }
                    }
                }
            }

            // Step 2: SW fallback
            if (use_sw_fallback && !hw_decode_success) {
                uint16_t* sw_buf = nullptr;
                int sw_w = 0, sw_h = 0;
                bool sw_ok;
                if (is_progressive) {
                    Serial.println("[ART] Progressive JPEG → stb_image decode");
                    sw_ok = decodeJPEGProgressiveStb(buf, cleaned_size, &sw_buf, &sw_w, &sw_h);
                } else {
                    sw_ok = decodeJPEGSoftware(buf, cleaned_size, &sw_buf, &sw_w, &sw_h);
                }
                if (sw_ok) {
                    decoded_pixels   = sw_buf;
                    final_w          = sw_w;
                    final_h          = sw_h;
                    final_stride     = sw_w;
                    hw_decode_success = true;
                }
            }

            if (hw_decode_success && decoded_pixels)
                return {decoded_pixels, final_w, final_h, final_stride, true};
            return kDecodeFail;

        } else {
            // hw_jpeg_decoder == nullptr — SW-only path
            // FIX: scan for SOF2 so progressive JPEGs route to stb_image (not JPEGDEC)
            bool is_progressive = false;
            for (size_t pi = 0; pi + 1 < len; pi++) {
                if (buf[pi] == 0xFF && buf[pi+1] == 0xC2) { is_progressive = true; break; }
            }
            uint16_t* sw_buf = nullptr;
            int sw_w = 0, sw_h = 0;
            bool sw_ok;
            if (is_progressive) {
                Serial.println("[ART] Progressive JPEG → stb_image decode (HW unavailable)");
                sw_ok = decodeJPEGProgressiveStb(buf, len, &sw_buf, &sw_w, &sw_h);
            } else {
                Serial.println("[ART] HW JPEG unavailable, using SW decode");
                sw_ok = decodeJPEGSoftware(buf, len, &sw_buf, &sw_w, &sw_h);
            }
            if (sw_ok) return {sw_buf, sw_w, sw_h, sw_w, true};
            return kDecodeFail;
        }
    }

    // Unknown format
    Serial.println("[ART] Unknown image format (not JPEG or PNG)");
    return kDecodeFail;
}

// ── Scale decoded pixels to 420×420 and push to display ───────────────────────
static void displayArt(const DecodeResult& dec, const char* url) {
    memset(art_temp_buffer, 0, ART_PX * ART_PX * 2);
    Serial.printf("[ART] Bilinear scaling %dx%d -> 420x420 (stride=%d)\n", dec.w, dec.h, dec.stride);
    scaleImageBilinear(dec.pixels, dec.w, dec.h, dec.stride, art_temp_buffer, ART_PX, ART_PX);
    Serial.println("[ART] Scaling complete");

    sampleDominantColor(art_temp_buffer, ART_PX, ART_PX);
    uint32_t new_color = 0x1a1a1a;
    if (color_sample_count > 0) {
        uint8_t avg_r = color_r_sum / color_sample_count;
        uint8_t avg_g = color_g_sum / color_sample_count;
        uint8_t avg_b = color_b_sum / color_sample_count;
        avg_r = (avg_r * 4) / 10;
        avg_g = (avg_g * 4) / 10;
        avg_b = (avg_b * 4) / 10;
        new_color = ((uint32_t)avg_r << 16) | ((uint32_t)avg_g << 8) | avg_b;
    }

    // Generate smooth blurred fullscreen background:
    // 1. Downsample 420×420 → 32×32 via box average (each output pixel = avg of 13×13 input pixels)
    // 2. Apply a 3×3 box blur pass on the 32×32 result — smooths hard colour-zone edges
    // 3. Bilinear upscale 32×32 → 800×480
    // Entire process runs once per track change in art task — zero LVGL/rendering overhead.
    // (LVGL 9.5 blur_radius would re-allocate a ~768KB layer buffer on every dirty redraw.)
    // Blur scratch lives in PSRAM, allocated once and reused for the lifetime of the task.
    // These were `static uint16_t [64*64]` (8KB each) in .bss, permanently holding 16KB of
    // INTERNAL DMA-capable SRAM — the same scarce pool WiFi/SDIO, mbedTLS and the JPEG
    // decoder draw from, and which ART_MIN_DMA_PRE_BURST (56KB) vs the ~68KB WiFi-connected
    // ceiling is tuned against. Pure per-track scratch never needs DMA, so this returns
    // 16KB of headroom — more than the entire margin those thresholds fight over.
    static uint16_t* blur_tiny   = nullptr;
    static uint16_t* blur_smooth = nullptr;
    bool blur_generated = false;   // gates blur_bg_ready so a skip can't publish stale pixels
    if (blur_bg_buf) {
        if (!blur_tiny)
            blur_tiny = (uint16_t*)heap_caps_malloc(64 * 64 * 2, MALLOC_CAP_SPIRAM);
        if (!blur_smooth)
            blur_smooth = (uint16_t*)heap_caps_malloc(64 * 64 * 2, MALLOC_CAP_SPIRAM);
        if (!blur_tiny || !blur_smooth)
            Serial.println("[ART] Blur scratch alloc failed - skipping blurred background");
    }
    // Art itself still displays if this is skipped; only the blurred backdrop is lost.
    if (blur_bg_buf && blur_tiny && blur_smooth) {
        const int TINY = 64;

        // Step 1: box-average downsample — map each tiny pixel to its art region
        for (int ty = 0; ty < TINY; ty++) {
            int y0 = ty * ART_PX / TINY, y1 = (ty + 1) * ART_PX / TINY;
            for (int tx = 0; tx < TINY; tx++) {
                int x0 = tx * ART_PX / TINY, x1 = (tx + 1) * ART_PX / TINY;
                uint32_t r = 0, g = 0, b = 0, n = 0;
                for (int py = y0; py < y1; py++) {
                    uint16_t* row = &art_temp_buffer[py * ART_PX + x0];
                    for (int px = 0; px < (x1 - x0); px++) {
                        uint16_t p = row[px];
                        r += (p >> 11) & 0x1F;
                        g += (p >> 5)  & 0x3F;
                        b +=  p        & 0x1F;
                        n++;
                    }
                }
                blur_tiny[ty * TINY + tx] = n ? (uint16_t)(((r/n) << 11) | ((g/n) << 5) | (b/n)) : 0;
            }
        }

        // Step 2: 5 passes of 3×3 box blur — strong Gaussian approximation on the 64×64 buffer.
        // Each pass ~64×64×9 = 37K ops. Five passes total ~185K ops → <1ms at 400MHz.
        uint16_t* src_buf = blur_tiny;
        uint16_t* dst_buf = blur_smooth;
        for (int pass = 0; pass < 5; pass++) {
            for (int ty = 0; ty < TINY; ty++) {
                for (int tx = 0; tx < TINY; tx++) {
                    uint32_t r = 0, g = 0, b = 0, n = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        int ny = ty + dy;
                        if (ny < 0 || ny >= TINY) continue;
                        for (int dx = -1; dx <= 1; dx++) {
                            int nx = tx + dx;
                            if (nx < 0 || nx >= TINY) continue;
                            uint16_t p = src_buf[ny * TINY + nx];
                            r += (p >> 11) & 0x1F;
                            g += (p >> 5)  & 0x3F;
                            b +=  p        & 0x1F;
                            n++;
                        }
                    }
                    dst_buf[ty * TINY + tx] = (uint16_t)(((r/n) << 11) | ((g/n) << 5) | (b/n));
                }
            }
            // Swap buffers for next pass
            uint16_t* tmp = src_buf; src_buf = dst_buf; dst_buf = tmp;
        }
        // src_buf holds the final blurred result. Darken to ~35% for text readability.
        for (int i = 0; i < TINY * TINY; i++) {
            uint16_t p = src_buf[i];
            uint32_t ri = ((p >> 11) & 0x1F) * 35 / 100;
            uint32_t gi = ((p >> 5)  & 0x3F) * 35 / 100;
            uint32_t bi = ( p        & 0x1F) * 35 / 100;
            src_buf[i] = (uint16_t)((ri << 11) | (gi << 5) | bi);
        }

        // Step 3: bilinear upscale to full screen
        scaleImageBilinear(src_buf, TINY, TINY, TINY, blur_bg_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        blur_generated = true;
    }

    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
        memcpy(art_buffer, art_temp_buffer, ART_PX * ART_PX * 2);
        last_art_url   = url;
        dominant_color = new_color;
        art_ready      = true;
        color_ready    = true;
        blur_bg_ready  = blur_generated;  // false if blur scratch alloc failed (no stale publish)
        // Persist the same fact for themeSet(), which republishes on theme change and
        // cannot see this function-local. False here means the buffer still holds the
        // PREVIOUS track's blur, so it must not be shown against the new artwork.
        blur_bg_valid  = blur_generated;
        xSemaphoreGive(art_mutex);
    }

    if (art_cache[0].pixels && art_cache[1].pixels) {
        int slot = 1 - art_cache_lru;
        memcpy(art_cache[slot].pixels, art_temp_buffer, ART_PX * ART_PX * 2);
        strncpy(art_cache[slot].url, url, sizeof(art_cache[slot].url) - 1);
        art_cache[slot].url[sizeof(art_cache[slot].url) - 1] = '\0';
        art_cache[slot].dominant_color = new_color;
        art_cache[slot].valid          = true;
        art_cache_lru = slot;
    }
}

// Prepare and sanitize album art URL
// Handles: HTML entity decoding, Sonos Radio URL extraction, size reduction, URL encoding
static String prepareAlbumArtURL(const String& rawUrl) {
    String fetchUrl = decodeHTMLEntities(rawUrl);

    // Sonos Radio fix: extract high-quality art from embedded mark parameter
    is_sonos_radio_art = false;  // Reset flag
    int markIndex = fetchUrl.indexOf("mark=http");
    if (markIndex == -1) {
        markIndex = fetchUrl.indexOf("mark=https");
    }
    if (fetchUrl.indexOf("sonosradio.imgix.net") != -1 && markIndex != -1) {
        Serial.println("[ART] Sonos Radio art detected");
        int markStart = markIndex + 5;  // After "mark="
        int markEnd = fetchUrl.indexOf("&", markStart);
        if (markEnd == -1) markEnd = fetchUrl.length();

        fetchUrl = fetchUrl.substring(markStart, markEnd);
        is_sonos_radio_art = true;
        Serial.printf("[ART] Extracted: %s\n", fetchUrl.c_str());
    }

    // Plex Media Server photo transcoder: bump small thumbnail requests to 600px.
    // Plex Sonos integration can request tiny thumbnails (e.g. width=64&height=64) resulting
    // in a heavily-upscaled pixelated image that appears "tiny" quality on screen.
    // Pattern: http://x.x.x.x:32400/photo/:/transcode?width=N&height=N&...
    if (fetchUrl.indexOf(":32400/photo/:/transcode") != -1) {
        const char* params[2] = {"width=", "height="};
        for (int pi = 0; pi < 2; pi++) {
            int idx = fetchUrl.indexOf(params[pi]);
            if (idx != -1) {
                int numStart = idx + strlen(params[pi]);
                int numEnd = numStart;
                while (numEnd < (int)fetchUrl.length() && isDigit(fetchUrl[numEnd])) numEnd++;
                if (numEnd > numStart && fetchUrl.substring(numStart, numEnd).toInt() < 400) {
                    fetchUrl = fetchUrl.substring(0, numStart) + "600" + fetchUrl.substring(numEnd);
                }
            }
        }
    }

    // Reduce image size for known providers to stay under size limit
    // Deezer: 1000x1000 → 400x400
    if (fetchUrl.indexOf("dzcdn.net") != -1) {
        fetchUrl.replace("/1000x1000-", "/400x400-");
    }
    // TuneIn (cdn-profiles.tunein.com): keep original size
    // Note: logoq is 145x145, logog is 600x600 (too big for PNG decode)
    if (fetchUrl.indexOf("cdn-profiles.tunein.com") != -1 && fetchUrl.indexOf("?d=") != -1) {
        fetchUrl.replace("?d=1024", "?d=400");
        fetchUrl.replace("?d=600", "?d=400");
    }
    // Spotify: Keep original resolution (640x640) since HTTP is lightweight
    // No size reduction needed - HTTP has no TLS overhead!

    // Universal HTTP downgrade: try HTTP for ALL art HTTPS URLs
    // Removes ALL TLS overhead (handshake, encryption, DMA memory)
    // This is the KEY to SDIO stability - no TLS = no crashes!
    // If a server refuses HTTP, the request returns non-200 and we show placeholder.
    // Redirect following is disabled in the downloader to prevent unexpected HTTPS loops.
    if (fetchUrl.startsWith("https://")) {
        fetchUrl.replace("https://", "http://");
    }

    // Sonos getaa URLs can contain unescaped '?' and '&' in the u= parameter; encode them only
    if (fetchUrl.indexOf("/getaa?") != -1) {
        int uPos = fetchUrl.indexOf("u=");
        if (uPos != -1) {
            int uStart = uPos + 2;
            int uEnd = fetchUrl.indexOf("&", uStart);
            if (uEnd == -1) uEnd = fetchUrl.length();
            String uValue = fetchUrl.substring(uStart, uEnd);
            String uEncoded = "";
            for (int i = 0; i < uValue.length(); i++) {
                char c = uValue[i];
                if (c == '?') {
                    uEncoded += "%3F";
                } else if (c == '&') {
                    uEncoded += "%26";
                } else {
                    uEncoded += c;
                }
            }
            fetchUrl = fetchUrl.substring(0, uStart) + uEncoded + fetchUrl.substring(uEnd);
        }
        // Request largest available art from the Sonos proxy (embedded or folder art)
        fetchUrl += "&maxWidth=600&maxHeight=600";
    }

    return fetchUrl;
}

// Check if URL points to a private/local network IP (no TLS, no SDIO pressure)
static bool isPrivateIP(const char* url) {
    const char* host = strstr(url, "://");
    if (!host) return false;
    host += 3;  // Skip past "://"
    return (strncmp(host, "192.168.", 8) == 0 ||
            strncmp(host, "10.", 3) == 0 ||
            strncmp(host, "172.", 4) == 0);
}

// Create (or recreate) the album art task with a PSRAM-allocated stack.
// PSRAM stack frees ~20KB of DMA-capable internal SRAM for WiFi/SDIO buffers.
// The stack pointer is allocated once and reused across task recreations (OTA restart, clock screen).
void createArtTask() {
    if (!art_task_stack) {
        art_task_stack = (StackType_t*)heap_caps_malloc(ART_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM);
    }
    if (art_task_stack) {
        albumArtTaskHandle = xTaskCreateStaticPinnedToCore(
            albumArtTask, "Art", ART_TASK_STACK_SIZE / sizeof(StackType_t),
            NULL, ART_TASK_PRIORITY, art_task_stack, &albumArtTaskTCB, 0);
    } else {
        Serial.println("[ART] PSRAM stack alloc failed — using internal SRAM");
        xTaskCreatePinnedToCore(albumArtTask, "Art", ART_TASK_STACK_SIZE, NULL,
                                ART_TASK_PRIORITY, &albumArtTaskHandle, 0);
    }
}

void albumArtTask(void* param) {
    // Guard against PSRAM leak on OTA recovery: task may be killed/recreated while globals
    // already hold valid pointers. Only allocate if not yet allocated.
    if (!art_buffer)
        art_buffer     = (uint16_t*)heap_caps_malloc(ART_PX * ART_PX * 2, MALLOC_CAP_SPIRAM);
    if (!art_temp_buffer)
        art_temp_buffer = (uint16_t*)heap_caps_malloc(ART_PX * ART_PX * 2, MALLOC_CAP_SPIRAM);
    if (!blur_bg_buf)
        blur_bg_buf = (uint16_t*)heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    if (!art_buffer || !art_temp_buffer) { vTaskDelete(NULL); return; }

    if (!art_cache[0].pixels)
        art_cache[0].pixels = (uint16_t*)heap_caps_malloc(ART_PX * ART_PX * 2, MALLOC_CAP_SPIRAM);
    if (!art_cache[1].pixels)
        art_cache[1].pixels = (uint16_t*)heap_caps_malloc(ART_PX * ART_PX * 2, MALLOC_CAP_SPIRAM);
    if (!art_cache[0].pixels || !art_cache[1].pixels) {
        Serial.println("[ART] LRU cache allocation failed — cache disabled");
    }

    // Permanent 280KB PSRAM download buffer — allocated once, never freed.
    // Eliminates per-download PSRAM heap alloc/free fragmentation:
    //   - Back-to-back downloads can't fail due to heap fragmentation (even with 10MB+ free PSRAM)
    //   - No risk of malloc returning nullptr mid-download due to previous alloc not yet freed by lwIP
    // Safe to reuse across iterations: bytesRead/pre_drained track how much is valid each download.
    static uint8_t* art_jpgbuf = nullptr;
    if (!art_jpgbuf)
        art_jpgbuf = (uint8_t*)heap_caps_malloc(MAX_ART_SIZE, MALLOC_CAP_SPIRAM);
    if (!art_jpgbuf)
        Serial.println("[ART] art_jpgbuf PSRAM alloc failed — downloads will be disabled");

    // Initialize ESP32-P4 Hardware JPEG Decoder
    jpeg_decode_engine_cfg_t hw_jpeg_cfg = {
        .intr_priority = 0,
        .timeout_ms = 1000,  // 1 second timeout
    };
    esp_err_t ret = jpeg_new_decoder_engine(&hw_jpeg_cfg, &hw_jpeg_decoder);
    if (ret != ESP_OK) {
        Serial.printf("[ART] Failed to init hardware JPEG decoder: %d\n", ret);
        hw_jpeg_decoder = nullptr;
    } else {
        Serial.println("[ART] Hardware JPEG decoder initialized!");
    }

    static char url[512];
    static char last_failed_url[512] = "";  // Track failed URLs to prevent infinite retry
    static int consecutive_failures = 0;

    while (1) {
        // Check if shutdown requested (for OTA update)
        if (art_shutdown_requested) {
            Serial.println("[ART] Shutdown requested");
            Serial.printf("[ART] Shutdown complete - Free DMA: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
            // This task is the ONLY writer that clears art_download_in_progress. Abort
            // paths deliberately leave it set, so if we exit here holding it, nothing can
            // ever clear it: pollingTask's early-exit guard then skips every SOAP until
            // the art task is recreated (clock screen exit / OTA). That leaves SDIO fully
            // silent for minutes — the C6 DMA clock-gate condition — and freezes playback
            // state so the clock screen's auto-exit never fires.
            art_download_in_progress = false;
            albumArtTaskHandle = NULL;  // Clear handle before deleting
            vTaskDelete(NULL);  // Delete self
            return;
        }

        // Clear abort flag at top of loop (will be acted on if set during download)
        if (art_abort_download) {
            art_abort_download = false;
        }

        url[0] = '\0';  // Clear URL
        bool isStationLogo = false;  // Track if this is a station logo (PNG allowed)
        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(10))) {
            if (pending_art_url.length() > 0 && pending_art_url != last_art_url) {
                isStationLogo = pending_is_station_logo;  // Capture flag while holding mutex
                String fetchUrl = prepareAlbumArtURL(pending_art_url);

                if (fetchUrl != last_art_url) {
                    strncpy(url, fetchUrl.c_str(), sizeof(url) - 1);
                    url[sizeof(url) - 1] = '\0';
                    // Only reset failure tracking for genuinely new URLs.
                    // If this URL previously failed, keep the counter so it reaches
                    // ART_DECODE_MAX_FAILURES and shows the placeholder instead of
                    // looping forever (the old code reset to 0 on every retry).
                    if (strcmp(url, last_failed_url) != 0) {
                        consecutive_failures = 0;
                        last_failed_url[0] = '\0';
                    }
                } else {
                    // fetchUrl already matches last_art_url — art is already displayed.
                    // Sync pending_art_url to the processed URL so the outer guard
                    // (pending_art_url != last_art_url) catches it on the next poll.
                    // Without this, Sonos Radio URLs (raw != extracted) spam the log
                    // every 100ms because pending_art_url never equals last_art_url.
                    pending_art_url = last_art_url;
                }
            }
            xSemaphoreGive(art_mutex);
        }
        if (url[0] == '\0') {
            // No URL pending. If a download just completed, flag=true (set after sdioPreWait).
            // Keep it true for SDIO_INTER_DOWNLOAD_MS so that if a back-to-back URL arrives,
            // the url!='\0' path below clears it before sdioPreWait (where polling runs).
            // After SDIO_INTER_DOWNLOAD_MS with no new URL, clear it — nothing is downloading.
            if (last_art_download_end_ms == 0 ||
                millis() - last_art_download_end_ms >= SDIO_INTER_DOWNLOAD_MS) {
                art_download_in_progress = false;  // nothing to download — polling can resume
            }
            vTaskDelay(pdMS_TO_TICKS(ART_CHECK_INTERVAL_MS));
            continue;
        }
        if (url[0] != '\0') {
            // Clear flag BEFORE sdioPreWait so polling keeps SDIO warm during the wait.
            // Flag is set to true AFTER sdioPreWait returns, just before the mutex acquire.
            // This is the key fix for the storm-gate / DMA clock-gate conflict:
            //   - flag=true during sdioPreWait → polling blocked → SDIO idle → DMA clock-gate → crash
            //   - flag=false during sdioPreWait → polling runs → SDIO stays warm → no clock-gate
            //     → storm gate drains pkt_rxbuff → server burst fits → no :928 crash
            art_download_in_progress = false;
            Serial.printf("[ART] URL: %s\n", url);

            // LRU cache check — serve instantly without network if we already have this art
            {
                bool cache_hit = false;
                for (int i = 0; i < 2; i++) {
                    if (art_cache[i].valid && art_cache[i].pixels &&
                        strncmp(art_cache[i].url, url, sizeof(art_cache[i].url)) == 0) {
                        Serial.printf("[ART] Cache hit slot %d — skipping download\n", i);
                        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                            memcpy(art_buffer, art_cache[i].pixels, ART_PX * ART_PX * 2);
                            last_art_url = url;
                            dominant_color = art_cache[i].dominant_color;
                            art_ready = true;
                            color_ready = true;
                            art_cache_lru = i;
                            xSemaphoreGive(art_mutex);
                        }
                        cache_hit = true;
                        break;
                    }
                }
                if (cache_hit) continue;
            }

            // Simple WiFi check - don't try to download if not connected
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[ART] WiFi not connected, skipping");
                // Mark as done to prevent retry loop when WiFi is down
                if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                    last_art_url = url;
                    xSemaphoreGive(art_mutex);
                }
                vTaskDelay(pdMS_TO_TICKS(2000));  // Wait longer for WiFi recovery
                continue;
            }

            // Detect if URL is from Sonos device itself (e.g., /getaa for YouTube Music)
            // These don't need per-chunk mutex since Sonos HTTP server serializes requests anyway
            bool isFromSonosDevice = (strstr(url, ":1400/") != nullptr);
            bool isLocalNetwork = isFromSonosDevice || isPrivateIP(url);
            bool use_https = (strncmp(url, "https://", 8) == 0);

            // Scoped HTTP/HTTPS download - ensures TLS session is freed after each download
            {
                // preClient MUST be declared before http: C++ destroys locals in reverse
                // order, so preClient outlives http → http.end() can call _client->stop()
                // on a still-valid preClient before preClient's destructor runs.
                WiFiClient preClient;   // pre-connected with SO_RCVBUF (HTTP only)
                HTTPClient http;
                WiFiClientSecure secure_client;
                bool mutex_acquired = false;
                bool http_early_closed = false;  // set true after early http.end() before decode/scale

                // PRE-WAIT: SDIO crash-defence before acquiring mutex.
                // art_download_in_progress is FALSE here — polling runs during this wait,
                // keeping SDIO active (prevents C6 DMA clock-gate from SDIO idle).
                //
                // Queue-poll cooldown only: after updateQueue() 20KB Browse response,
                // combined SDIO traffic with art download overflows pkt_rxbuff.
                // Storm gate and HTTPS cooldown omitted: cause unnecessary SDIO idle
                // → DMA clock-gate → crash.
                uint32_t prewait_flags = SDIO_WAIT_QUEUE_POLL;
                if (!sdioPreWait("ART", prewait_flags, &art_abort_download, &art_shutdown_requested)) {
                    art_abort_download = false;  // track changed during wait — pick up new URL
                    continue;
                }

                // Suppress polling. DMA safety check BEFORE mutex or TCP connection.
                // Burst size 13-22KB observed from 192.168.2.36:1400 (server cached TCP cwnd;
                // SO_RCVBUF does NOT limit initial burst on this platform). Need DMA ≥
                // ART_MIN_DMA_PRE_BURST to ensure dl-start DMA ≥ ART_TCP_RCVBUF_DL_SAFETY.
                art_download_in_progress = true;
                // Read DMA ONCE: same value used for the log and the check (TOCTOU fix).
                size_t dma_snap = heap_caps_get_free_size(MALLOC_CAP_DMA);
                artLogSDIO("flag-set");
                Serial.printf("[ART/flag-set] MEM: heap=%u dma=%u psram=%u stk=%u\n",
                    heap_caps_get_free_size(MALLOC_CAP_DEFAULT), (unsigned)dma_snap,
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM), uxTaskGetStackHighWaterMark(NULL));
                // Count consecutive DMA-too-low aborts across ANY URL.
                // Per-URL tracking was wrong: new song = new URL = counter reset = WiFi recovery never fires.
                static int dma_fail_count = 0;
                if (dma_snap < ART_MIN_DMA_PRE_BURST) {
                    // Before counting as a genuine abort: check whether another task (e.g. lyrics
                    // HTTPS) currently holds the network_mutex. If so, the DMA dip is transient —
                    // mbedTLS/TLS session frees ~44KB when the mutex is released (task done).
                    // Triggering WiFi stop here would kill that active TLS connection, causing its
                    // task to retry, which opens another TLS session, which drops DMA again →
                    // infinite loop. Instead: wait for the mutex to become free (≤10s), let DMA
                    // recover, then retry. Only count genuine depletions (mutex idle, DMA still low).
                    if (network_mutex != NULL && xSemaphoreGetMutexHolder(network_mutex) != NULL) {
                        art_download_in_progress = false;
                        Serial.printf("[ART] DMA low (%uKB) but mutex held — waiting for release\n",
                                      (unsigned)(dma_snap / 1024));
                        unsigned long t_wait = millis();
                        while (xSemaphoreGetMutexHolder(network_mutex) != NULL
                               && millis() - t_wait < 10000) {
                            vTaskDelay(pdMS_TO_TICKS(200));
                        }
                        // Don't increment dma_fail_count — transient TLS dip, not real depletion.
                        continue;
                    }
                    dma_fail_count++;
                    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
                    Serial.printf("[ART] DMA too low (%u < %u, largest=%uKB) — abort #%d\n",
                                  (unsigned)dma_snap, (unsigned)ART_MIN_DMA_PRE_BURST,
                                  (unsigned)(largest / 1024), dma_fail_count);
                    if (dma_fail_count >= 3) {
                        // DMA depleted after 3 consecutive aborts across any URL.
                        // CRITICAL: art task and clockBgTask both run with PSRAM stacks.
                        // esp_restart() and Preferences.begin() both call
                        // spi_flash_disable_interrupts_caches_and_other_cpu() which asserts
                        // esp_task_stack_is_sane_cache_disabled() → cache_utils.c:127 crash.
                        // FIX: delegate WiFi stop + reconnect/restart to mainAppTask (SRAM stack).
                        Serial.printf("[ART] DMA low x%d — requesting main task recovery\n",
                                      dma_fail_count);
                        art_download_in_progress = false;    // keep SDIO warm during recovery
                        art_dma_recovery_requested = true;   // mainAppTask picks this up each loop

                        // Wait for mainAppTask to complete recovery.
                        // If restart: esp_restart() fires from SRAM stack — we never return.
                        // If reconnect: mainAppTask clears the flag when WiFi is back up.
                        // 55s timeout: worst-case mainAppTask takes 2s WiFi-stop drain
                        // + 30s reconnect + 15s DMA stabilisation + 5s buffer = 52s.
                        // Original 25s caused art task to time out mid-reconnect, reset
                        // dma_fail_count to 0, re-enter the abort loop, hit 3 aborts again
                        // at ~second 31, and request a SECOND recovery while mainAppTask
                        // was still inside WiFi.begin() — cascading reconnect loop.
                        unsigned long t_rec = millis();
                        while (art_dma_recovery_requested && millis() - t_rec < 55000) {
                            vTaskDelay(pdMS_TO_TICKS(500));
                        }
                        dma_fail_count = 0;
                        last_art_download_end_ms = millis();
                    } else {
                        // Clear flag so polling keeps SDIO warm during recovery wait.
                        // Flag will be set true again after sdioPreWait on the next iteration.
                        art_download_in_progress = false;
                        vTaskDelay(pdMS_TO_TICKS(2000));  // brief wait before retry
                        // Do NOT set last_art_download_end_ms — no network happened.
                    }
                    continue;
                }

                dma_fail_count = 0;  // DMA healthy — reset consecutive failure counter

                // Acquire network mutex (all network activity serialized)
                uint32_t t_mutex_start = millis();
                mutex_acquired = xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_ART_MS));
                if (!mutex_acquired) {
                    Serial.println("[ART] Failed to acquire network mutex - skipping download");
                }

                if (mutex_acquired) {
                    Serial.printf("[ART/mutex] acquired after %lums\n", millis() - t_mutex_start);
                    artLogMem("mutex");
                    artLogSDIO("mutex");
                    // ABORT CHECK: If track changed while waiting for mutex, bail out immediately
                    if (art_abort_download) {
                        Serial.println("[ART] Track changed while waiting for mutex - skipping");
                        art_abort_download = false;
                        if (preClient.connected()) {
                            // preClient destructor (scope exit) fires TCP RST — give SDIO time to flush
                            // before next download attempt starts.
                            last_network_end_ms = millis();
                            vTaskDelay(pdMS_TO_TICKS(SDIO_TCP_CLOSE_MS));
                        }
                        xSemaphoreGive(network_mutex);
                        mutex_acquired = false;
                        continue;
                    }

                    // Re-check cooldowns under mutex (another task may have used network while we waited)
                    // SDIO_GENERAL_COOLDOWN_MS (200ms) applied OUTSIDE the mutex in sdioPreWait.
                    // Inside the mutex we need enough time for the last SOAP's TCP FIN-ACK to
                    // arrive and be drained from C6 pkt_rxbuff before we open the art connection.
                    // WiFi FIN-ACK RTT on LAN: ~10-30ms. The old 10ms was too short:
                    // SOAP #N completes → mutex released → art acquires mutex (net=0ms) → only
                    // 23ms passes before http.GET() → FIN-ACK still in transit → GET response
                    // arrives simultaneously → pkt_rxbuff overflow → :928.
                    // 50ms covers WiFi RTT with margin and is well below DMA clock-gate threshold.
                    {
                        const unsigned long kInnerNetCooldownMs = 50;
                        if (last_network_end_ms > 0) {
                            unsigned long elapsed = millis() - last_network_end_ms;
                            if (elapsed < kInnerNetCooldownMs) {
                                vTaskDelay(pdMS_TO_TICKS(kInnerNetCooldownMs - elapsed));
                            }
                        }
                    }
                    // HTTPS TCP residue drain — catches lyrics→art race.
                    // If lyrics HTTPS completed while art waited for the mutex, last_https_end_ms
                    // is fresh. Art is HTTP-only (HTTPS downgraded in prepareAlbumArtURL), so we
                    // only need TCP FIN-ACK drain time. On LAN, TLS close_notify + TCP FIN-ACK
                    // completes in <30ms; SDIO_TCP_CLOSE_MS (200ms) is ample margin.
                    // Was SDIO_HTTPS_TCP_CLOSE_MS (500ms) — too long: if lyrics finished 100ms
                    // before mutex acquisition, art waits 400ms inside mutex → SDIO idle →
                    // C6 DMA clock-gate → pkt_rxbuff overflow on next TCP connection.
                    if (last_https_end_ms > 0) {
                        unsigned long elapsed = millis() - last_https_end_ms;
                        if (elapsed < SDIO_TCP_CLOSE_MS) {
                            vTaskDelay(pdMS_TO_TICKS(SDIO_TCP_CLOSE_MS - elapsed));
                        }
                    }
                    // Inside-mutex rechecks for queue-poll and inter-download: the full cooldowns
                    // (3000ms / 1000ms) already ran in sdioPreWait OUTSIDE the mutex where
                    // pollingTask can fire SOAPs to keep SDIO warm. Here we only guard against
                    // the race where the timestamp was updated AFTER sdioPreWait returned but
                    // BEFORE we acquired the mutex. In that window the elapsed time is at most
                    // a few hundred ms, so TCP residue drain time (SDIO_TCP_CLOSE_MS = 200ms)
                    // is all we need. Waiting the full 3000ms / 1000ms inside the mutex blocks
                    // pollingTask and creates SDIO silence → P4 SDIO DMA clock-gate → crash.
                    if (last_queue_fetch_time > 0) {
                        unsigned long elapsed = millis() - last_queue_fetch_time;
                        if (elapsed < SDIO_TCP_CLOSE_MS) {
                            vTaskDelay(pdMS_TO_TICKS(SDIO_TCP_CLOSE_MS - elapsed));
                        }
                    }
                    if (last_art_download_end_ms > 0) {
                        unsigned long elapsed = millis() - last_art_download_end_ms;
                        if (elapsed < SDIO_TCP_CLOSE_MS) {
                            vTaskDelay(pdMS_TO_TICKS(SDIO_TCP_CLOSE_MS - elapsed));
                        }
                    }
                    // Post-500 settle: if a Sonos 500 occurred within the last SDIO_TCP_CLOSE_MS
                    // (200ms), wait for its TCP FIN-ACK to drain before starting the art connection.
                    // Keeps inside-mutex wait ≤ 200ms — longer waits block pollingTask (holds
                    // network_mutex) → SDIO idle → P4 DMA clock-gate → pkt_rxbuff overflow.
                    // TCP FIN-ACK on LAN completes in <5ms; 200ms is more than sufficient.
                    if (last_transient_500_ms > 0) {
                        unsigned long elapsed = millis() - last_transient_500_ms;
                        if (elapsed < SDIO_TCP_CLOSE_MS) {
                            Serial.printf("[ART] Post-500 drain: waiting %lums\n", SDIO_TCP_CLOSE_MS - elapsed);
                            vTaskDelay(pdMS_TO_TICKS(SDIO_TCP_CLOSE_MS - elapsed));
                        }
                    }

                    // Set up HTTP connection (inside mutex - all network activity serialized)
                    if (use_https) {
                        secure_client.setInsecure();
                        http.begin(secure_client, url);
                    } else {
                        // Pre-connect: note SO_RCVBUF set here may not take effect on the SYN
                        // (lwIP PCB doesn't exist until lwip_connect()). We set SO_RCVBUF again
                        // on the actual stream fd after GET returns — see below.
                        int pre_timeout = isLocalNetwork ? 3000 : 10000;
                        preClient = artPreConnectHTTP(url, pre_timeout);
                        if (preClient.connected()) {
                            Serial.printf("[ART] TCP pre-connect OK fd=%d SO_RCVBUF=%d\n", preClient.fd(), ART_TCP_RCVBUF);
                            http.begin(preClient, url);
                        } else {
                            Serial.println("[ART] TCP pre-connect FAILED — fallback http.begin() (NO SO_RCVBUF!)");
                            http.begin(url);
                        }
                    }
                    // Local network: 3s timeout (LAN responds in <1s, 3s catches slow devices)
                    // Internet: 10s timeout (CDN/remote servers can be slow)
                    http.setTimeout(isLocalNetwork ? 3000 : 10000);
                    // Disable redirects: we downgrade https→http in prepareAlbumArtURL.
                    // If a server redirects back to https, we must NOT follow it (would create
                    // unexpected TLS session). Non-200 responses show placeholder instead.
                    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
                    // Force HTTP/1.0: prevents Transfer-Encoding: chunked responses.
                    // Plex Direct (plex.direct:32400) returns chunked with no Content-Length.
                    // getStreamPtr()->readBytes() reads raw bytes — chunk size headers land in
                    // jpgBuf[0..5] ("1000\r\n") before JPEG magic → format detection fails.
                    // HTTP/1.0 servers must send flat response; all art sources support it.
                    http.useHTTP10(true);

                    // Re-apply SO_RCVBUF immediately before GET (belt-and-suspenders).
                    // artPreConnectHTTP sets it post-connect, but if lwIP's internal ACK for
                    // SYN-ACK carried TCP_WND=65534 (before our setsockopt), the server may
                    // still believe the window is large. Resetting here forces pcb->rcv_wnd
                    // to 4096 so the GET's TCP header advertises the constrained window.
                    // vTaskDelay(1ms) gives the lwIP task a scheduling slice to propagate
                    // the option change to the PCB before the GET segment is sent.
                    if (!use_https && preClient.connected()) {
                        int rcvbuf = ART_TCP_RCVBUF;
                        lwip_setsockopt(preClient.fd(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                    // Use the permanent static PSRAM download buffer — no per-download alloc.
                    // art_jpgbuf is allocated once in albumArtTask init and reused every iteration.
                    const size_t max_art_size = MAX_ART_SIZE;
                    uint8_t* jpgBuf = art_jpgbuf;  // static PSRAM buffer; null only if init failed
                    size_t pre_drained = 0;
                    if (!jpgBuf) Serial.println("[ART] art_jpgbuf unavailable — full drain disabled");
                    size_t dma_pre_get = heap_caps_get_free_size(MALLOC_CAP_DMA);
                    artLogMem("pre-GET");
                    artLogSDIO("pre-GET");
                    uint32_t t_get = millis();
                    int code = http.GET();
                    // Full body drain: read the ENTIRE content-length immediately after GET
                    // returns, before any other processing. Each readBytes() calls tcp_recved()
                    // which frees pbufs and opens the window for the next server window.
                    // With SO_RCVBUF=4096, server sends ≤4096 bytes at a time; we drain
                    // each window as it arrives. By the time the main read loop starts,
                    // the server has nothing left to send → no burst → no pkt_rxbuff overflow.
                    // Root cause of :928: pre-drain stopping at available()==0 left the NEXT
                    // in-flight window arriving during setup code (size checks / logs / setsockopt)
                    // → two simultaneous windows fill C6 pkt_rxbuff → crash.
                    int full_drain_target = (code == 200) ? http.getSize() : 0;
                    if (code == 200 && jpgBuf) {
                        WiFiClient* _sd = http.getStreamPtr();
                        bool _known = (full_drain_target > 0 &&
                                       full_drain_target < (int)max_art_size);
                        size_t _target = _known ? (size_t)full_drain_target : max_art_size;
                        if (_sd) {
                            uint32_t _ds = millis();
                            while (pre_drained < _target && millis() - _ds < 15000) {
                                if (art_abort_download || art_shutdown_requested) break;
                                int _a = _sd->available();
                                if (_a <= 0) {
                                    if (!_sd->connected()) break;  // server closed = EOF
                                    vTaskDelay(pdMS_TO_TICKS(1));  // yield for TCP stack
                                    continue;
                                }
                                size_t _c = min((size_t)min(_a, 4096), _target - pre_drained);
                                size_t _g = _sd->readBytes(jpgBuf + pre_drained, _c);
                                if (!_g) break;
                                pre_drained += _g;
                                // 5ms inter-chunk yield: gives SDIO time to drain residual
                                // packets (500 FIN-ACK, SSDP, etc.) between server windows.
                                // Without this, rapid tcp_recved() calls fire successive
                                // windows faster than P4 SDIO can pull them from C6
                                // pkt_rxbuff → :928 overflow. Same fix as main read loop.
                                vTaskDelay(pdMS_TO_TICKS(5));
                            }
                        }
                    }
                    Serial.printf("[ART] GET+drain → code=%d in %lums (drained=%u/%d DMA=%uKB)\n",
                        code, millis() - t_get, (unsigned)pre_drained, full_drain_target,
                        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024));
                    // Keep mutex locked for entire download

                    if (code == 200) {
                int len = full_drain_target > 0 ? full_drain_target : http.getSize();
                const bool len_known = (len > 0);
                if ((len_known && len < (int)max_art_size) || !len_known) {
                    size_t alloc_len = len_known ? (size_t)len : max_art_size;
                    // jpgBuf points to the static 280KB PSRAM buffer (art_jpgbuf).
                    // pre_drained bytes already in jpgBuf[0..pre_drained-1] (full body if drain succeeded).
                    // alloc_len ≤ MAX_ART_SIZE, so the buffer is always large enough.

                    if (len_known) {
                        Serial.printf("[ART] Downloading album art: %d bytes\n", len);
                    } else {
                        Serial.println("[ART] Downloading album art: unknown length");
                    }
                    artLogMem("dl-start");
                    // DL-start DMA check — after pre-drain, burst pbufs should be freed.
                    // If DMA still low (pre-drain insufficient or alloc failed), abort.
                    size_t dma_dl_start = heap_caps_get_free_size(MALLOC_CAP_DMA);
                    Serial.printf("[ART/burst] pre-GET=%u dl-start=%u burst=%u bytes\n",
                                  (unsigned)dma_pre_get, (unsigned)dma_dl_start,
                                  dma_pre_get > dma_dl_start ? (unsigned)(dma_pre_get - dma_dl_start) : 0u);
                    if (dma_dl_start < ART_TCP_RCVBUF_DL_SAFETY || !jpgBuf) {
                        if (!jpgBuf)
                            Serial.println("[ART] art_jpgbuf unavailable — aborting");
                        else
                            Serial.printf("[ART] DMA too low at dl-start (%u < %u) — aborting\n",
                                          (unsigned)dma_dl_start, (unsigned)ART_TCP_RCVBUF_DL_SAFETY);
                        jpgBuf = nullptr;  // static buffer — don't free, just mark unavailable for this iteration
                        http.end();
                        vTaskDelay(pdMS_TO_TICKS(SDIO_TCP_CLOSE_MS));
                        last_network_end_ms      = millis();
                        last_art_download_end_ms = millis();
                        xSemaphoreGive(network_mutex);
                        mutex_acquired = false;
                        continue;
                    }
                    {  // scope: dma_dl_start >= ART_TCP_RCVBUF_DL_SAFETY && jpgBuf valid
                    if (jpgBuf) {
                        WiFiClient* stream = http.getStreamPtr();

                        // SO_RCVBUF: set on the actual stream fd immediately after GET.
                        // artPreConnectHTTP() tries to set this BEFORE lwip_connect(), but
                        // lwIP doesn't create the PCB (and thus pcb->rcv_wnd) until connect()
                        // is called. If SO_RCVBUF is set before the PCB exists, lwIP cannot
                        // apply it to the SYN → server sees TCP_WND=65534 → blasts 53KB initial
                        // burst into C6 pkt_rxbuff → SDIO overflow. Setting it HERE, after GET,
                        // takes effect immediately: subsequent ACKs advertise the constrained
                        // window → server slows to ≤8KB chunks → SDIO handles them safely.
                        // The initial burst (already received) is absorbed by the 5ms inter-chunk
                        // delay and the chunked read loop — no additional SDIO frames arrive until
                        // we read and ACK, at which point the new window applies.
                        if (!use_https && stream) {
                            int rcvbuf = ART_TCP_RCVBUF;
                            int stream_fd = stream->fd();
                            int pre_fd   = preClient.connected() ? preClient.fd() : -1;
                            Serial.printf("[ART] stream->fd()=%d preClient.fd()=%d SO_RCVBUF=%d\n",
                                          stream_fd, pre_fd, rcvbuf);
                            if (stream_fd >= 0) {
                                lwip_setsockopt(stream_fd, SOL_SOCKET, SO_RCVBUF,
                                                &rcvbuf, sizeof(rcvbuf));
                            }
                        }

                        // Chunked reading to avoid WiFi buffer issues
                        const size_t chunkSize = ART_CHUNK_SIZE;
                        size_t bytesRead = pre_drained;  // continue from pre-drain position
                        bool readSuccess = true;
                        uint32_t t_dl_start = millis();

                        // Read loop: keep going while connected OR data still buffered.
                        // IMPORTANT: check bytesRead < alloc_len FIRST (short-circuit).
                        // When full body pre-drain succeeded (bytesRead == alloc_len), this
                        // exits immediately WITHOUT calling stream->connected(). After full EOF
                        // drain, WiFiClient may free its internal socket handle — calling
                        // connected() on it causes a Load Access Fault (confirmed by addr2line).
                        while (bytesRead < alloc_len && stream &&
                               (stream->connected() || stream->available())) {
                            // Belt-and-suspenders: check DMA during read. If WiFi dynamic RX
                            // buffers allocate ~15KB after dl-start (pool not yet fully used),
                            // DMA can drop from ~22KB to ~7KB — near the :928 crash floor.
                            // ART_DMA_MID_READ_MIN (8KB) catches this before it becomes fatal.
                            // Checked every chunk (4KB) = at most ~200ms sampling interval.
                            if (heap_caps_get_free_size(MALLOC_CAP_DMA) < ART_DMA_MID_READ_MIN) {
                                Serial.printf("[ART] DMA critical during read (%u < %u) — aborting\n",
                                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                                    (unsigned)ART_DMA_MID_READ_MIN);
                                readSuccess = false;
                                break;
                            }
                            // Check if source changed or OTA starting - abort download immediately
                            if (art_abort_download || art_shutdown_requested) {
                                Serial.printf("[ART] %s - aborting current download\n",
                                    art_shutdown_requested ? "OTA shutdown" : "Source changed");
                                if (art_abort_download) art_abort_download = false;
                                readSuccess = false;
                                break;
                            }

                            size_t available = stream->available();
                            if (available == 0) {
                                vTaskDelay(pdMS_TO_TICKS(1));
                                // Only break if connection closed AND no buffered data
                                if (!stream->connected() && stream->available() == 0) break;
                                continue;
                            }

                            size_t remaining = len_known ? ((size_t)len - bytesRead) : (alloc_len - bytesRead);
                            size_t toRead = min(chunkSize, remaining);
                            toRead = min(toRead, available);

                            size_t actualRead = stream->readBytes(jpgBuf + bytesRead, toRead);

                            if (actualRead == 0) {
                                if (len_known) {
                                    Serial.printf("[ART] Read timeout at %d/%d bytes\n", (int)bytesRead, len);
                                    readSuccess = false;
                                }
                                break;
                            }

                            bytesRead += actualRead;
                            // Log every ~80KB (20 chunks × 4KB)
                            if (chunkSize > 0 && (bytesRead / chunkSize) % 20 == 1 && bytesRead < (size_t)(len > 0 ? len : (int)alloc_len)) {
                                Serial.printf("[ART] DL progress: %u/%d bytes avail=%u conn=%d\n",
                                    (unsigned)bytesRead, len, (unsigned)stream->available(), (int)stream->connected());
                            }
                            // 5ms inter-chunk yield: gives lwIP uninterrupted time between reads
                            // to drain any background WiFi frames (Sonos SSDP/mDNS multicast
                            // announcements that arrive as HLS transition completes) from C6
                            // pkt_rxbuff. Without this, art's TCP data + concurrent multicast
                            // bursts exhaust pkt_rxbuff → sdio_push_data_to_queue :928.
                            // This is safe: WiFi.setSleep(false) prevents C6 power-save so no
                            // wake-up burst occurs, and Sonos uses pure SOAP-polling (no UPnP
                            // NOTIFY push events) so the only traffic is art's own TCP segments
                            // plus occasional network multicasts.
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }

                        // Re-acquire mutex for cleanup (http.end, timestamp update)
                        if (!mutex_acquired) {
                            mutex_acquired = xSemaphoreTake(network_mutex, pdMS_TO_TICKS(5000));
                            if (!mutex_acquired) {
                                Serial.println("[ART] Warning: couldn't re-acquire mutex for cleanup");
                            }
                        }

                        if (!len_known && bytesRead >= max_art_size) {
                            Serial.println("[ART] Album art too large (max 280KB)");
                            readSuccess = false;
                        }

                        Serial.printf("[ART] Album art read: %d bytes (len_known=%d) in %lums\n", (int)bytesRead, len_known ? 1 : 0, millis() - t_dl_start);

                        // If download failed/aborted, close connection and free TLS/DMA resources
                        if (!readSuccess) {
                            Serial.println("[ART] Download failed/aborted - closing connection");
                            stream->stop();  // TCP RST - kills connection immediately
                            jpgBuf = nullptr;  // static buffer — don't free
                            // CRITICAL: http.end() + secure_client.stop() free TLS/DMA memory
                            // After TCP RST, these won't send SDIO traffic (socket is dead)
                            // but they WILL release DMA buffers used by esp-aes
                            http.end();
                            if (use_https) secure_client.stop();
                            // Wait for in-flight packets to flush
                            // Local Sonos: 300ms (was 50ms - insufficient for partial large download;
                            //   ~28 TCP ACKs still in SDIO TX queue when retry TCP SYN fires → Crash A)
                            // Local NAS/Plex: 300ms (same reasoning as Sonos)
                            // Internet HTTP: 300ms (simple TCP cleanup)
                            // Internet HTTPS: 1000ms (TLS session + TCP cleanup)
                            vTaskDelay(pdMS_TO_TICKS(isLocalNetwork ? 300 : (use_https ? 1000 : 300)));
                            last_network_end_ms      = millis();
                            last_art_download_end_ms = millis();   // partial downloads still leave SDIO residue
                            if (use_https) last_https_end_ms = millis();
                            if (mutex_acquired) {
                                xSemaphoreGive(network_mutex);
                                mutex_acquired = false;
                            }
                            // Clear abort flag if set during download
                            if (art_abort_download) {
                                art_abort_download = false;
                            }
                            continue;
                        }

                        int read = bytesRead;
                        // STRICT size check: JPEG needs ALL bytes (missing EOI marker → HW decoder timeout)
                        // Only allow exact match for known-length downloads
                        bool sizeOk = len_known ? (read == len) : (read > 0);
                        if (len_known && read != len) {
                            Serial.printf("[ART] Incomplete download: %d/%d bytes (%d missing)\n", read, len, len - read);
                            // Track incomplete downloads as failures to prevent infinite retry
                            if (strcmp(url, last_failed_url) == 0) {
                                consecutive_failures++;
                            } else {
                                strncpy(last_failed_url, url, sizeof(last_failed_url) - 1);
                                last_failed_url[sizeof(last_failed_url) - 1] = '\0';
                                consecutive_failures = 1;
                            }
                            if (consecutive_failures >= 5) {
                                Serial.printf("[ART] Incomplete %d times, giving up on this URL\n", consecutive_failures);
                                if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                    last_art_url = url;
                                    art_show_placeholder = true;
                                    xSemaphoreGive(art_mutex);
                                }
                                consecutive_failures = 0;
                                last_failed_url[0] = '\0';
                            }
                        }
                        if (sizeOk && readSuccess) {
                            // Keep TCP open during decode/scale (PASSIVE CLOSE fix):
                            // Previously we closed early with http.end() + SO_LINGER{1,0} before decode.
                            // SO_LINGER is NOT compiled in the pioarduino pre-built framework → setsockopt
                            // returns -1 silently → FIN-close → TIME_WAIT → each art PCB holds ~8.7KB DMA
                            // for 120s. 3 downloads = 26KB DMA lost; combined with SOAP PCBs = 91KB lost
                            // total → art DMA wait stuck at 28KB indefinitely → art never loads.
                            //
                            // Fix: Leave TCP open. Server sends FIN after last byte (arrives in pkt_rxbuff
                            // during decode/scale as 1 tiny TCP segment — safe, polling suppressed by
                            // art_download_in_progress=true, no UPnP NOTIFY events to compound it).
                            // lwIP auto-ACKs server FIN → we enter CLOSE_WAIT. After decode/scale, http.end()
                            // sends our FIN → LAST_ACK → CLOSED. Server enters TIME_WAIT, not us → zero DMA
                            // cost on P4 side per art download.
                            artLogMem("post-close");

                            // ── Dechunk if needed ─────────────────────────────
                            // Sonos getaa at :1400 returns chunked encoding for
                            // x-sonos-http sources even when HTTP/1.0 was requested.
                            read = (int)dechunkBuffer(jpgBuf, (size_t)read);

                            // ── Format detection ──────────────────────────────
                            bool isJPEG = (read >= 3 && jpgBuf[0] == 0xFF
                                                      && jpgBuf[1] == 0xD8
                                                      && jpgBuf[2] == 0xFF);
                            bool isPNG  = (read >= 4 && jpgBuf[0] == 0x89
                                                      && jpgBuf[1] == 0x50
                                                      && jpgBuf[2] == 0x4E
                                                      && jpgBuf[3] == 0x47);

                            if (!isJPEG && !isPNG && read >= 12) {
                                Serial.printf("[ART] Unknown format — first 12 bytes: "
                                              "%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X\n",
                                              jpgBuf[0],  jpgBuf[1],  jpgBuf[2],  jpgBuf[3],
                                              jpgBuf[4],  jpgBuf[5],  jpgBuf[6],  jpgBuf[7],
                                              jpgBuf[8],  jpgBuf[9],  jpgBuf[10], jpgBuf[11]);
                            }
                            artLogMem("pre-decode");  // DMA before HW JPEG alloc (expect ~90KB)
                            DecodeResult dec = decodeToRGB565(jpgBuf, (size_t)read,
                                                              isJPEG, isPNG);
                            artLogMem("post-decode");  // DMA after decode — delta shows JPEG DMA cost
                            if (dec.ok) {
                                displayArt(dec, url);
                                heap_caps_free(dec.pixels);
                                consecutive_failures = 0;
                                last_failed_url[0] = '\0';
                            } else {
                                if (strcmp(url, last_failed_url) == 0) {
                                    consecutive_failures++;
                                } else {
                                    strncpy(last_failed_url, url, sizeof(last_failed_url) - 1);
                                    last_failed_url[sizeof(last_failed_url) - 1] = '\0';
                                    consecutive_failures = 1;
                                }
                                if (consecutive_failures > 1)
                                    vTaskDelay(pdMS_TO_TICKS(consecutive_failures * 200));
                                if (consecutive_failures >= ART_DECODE_MAX_FAILURES) {
                                    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                        last_art_url        = url;
                                        art_show_placeholder = true;
                                        xSemaphoreGive(art_mutex);
                                    }
                                    consecutive_failures = 0;
                                    last_failed_url[0] = '\0';
                                }
                            }
                        }
                        jpgBuf = nullptr;  // static buffer — don't free
                    } else {
                        Serial.printf("[ART] art_jpgbuf unavailable — cannot download %d bytes\n", len);
                        // Mark as done - memory issue, retry won't help
                        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                            last_art_url = url;
                            art_show_placeholder = true;
                            xSemaphoreGive(art_mutex);
                        }
                    }
                    } // end: dma_dl_start >= ART_TCP_RCVBUF_DL_SAFETY
                } else if (len >= (int)max_art_size) {
                    Serial.printf("[ART] Album art too large: %d bytes (max %dKB)\n", len, (int)(max_art_size/1000));
                    // Force close - don't drain (overwhelms SDIO buffer)
                    WiFiClient* stream = http.getStreamPtr();
                    stream->stop();
                    Serial.println("[ART] Connection closed (oversized image)");
                    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                        last_art_url = url;
                        art_show_placeholder = true;
                        xSemaphoreGive(art_mutex);
                    }
                    // CRITICAL: Free TLS/DMA resources before releasing mutex
                    http.end();
                    if (use_https) secure_client.stop();
                    // Wait for in-flight packets to flush (HTTP: 300ms, HTTPS: 1000ms)
                    vTaskDelay(pdMS_TO_TICKS(use_https ? 1000 : 300));
                    last_network_end_ms      = millis();
                    last_art_download_end_ms = millis();   // gate inter-download cooldown after oversized abort
                    if (use_https) last_https_end_ms = millis();
                    jpgBuf = nullptr;  // static buffer — don't free
                    xSemaphoreGive(network_mutex);
                    mutex_acquired = false;
                    continue;
                    } else {
                        Serial.printf("[ART] Invalid album art size: %d bytes\n", len);
                    }
                    } else {
                        // Translate HTTP error codes to human-readable messages
                        const char* error_msg = "Unknown error";
                        switch (code) {
                            case -1: error_msg = "Connection failed"; break;
                            case -2: error_msg = "Send header failed"; break;
                            case -3: error_msg = "Send payload failed"; break;
                            case -4: error_msg = "Not connected"; break;
                            case -5: error_msg = "Connection lost/timeout"; break;
                            case -6: error_msg = "No stream"; break;
                            case -8: error_msg = "Too less RAM"; break;
                            case -11: error_msg = "Read timeout"; break;
                            default: break;
                        }
                        Serial.printf("[ART] HTTP %d: %s\n", code, error_msg);

                        // Track consecutive failures to prevent infinite retry loop
                        if (strcmp(url, last_failed_url) == 0) {
                            consecutive_failures++;
                        } else {
                            strncpy(last_failed_url, url, sizeof(last_failed_url) - 1);
                            last_failed_url[sizeof(last_failed_url) - 1] = '\0';
                            consecutive_failures = 1;
                        }

                        // Exponential backoff: 200ms, 400ms, 600ms, 800ms, 1000ms (prevents rapid retry hammering)
                        if (consecutive_failures > 1) {
                            vTaskDelay(pdMS_TO_TICKS(consecutive_failures * 200));
                        }

                        // After 5 consecutive failures for same URL, mark as done to stop retrying
                        if (consecutive_failures >= 5) {
                            Serial.printf("[ART] Failed %d times, giving up on this URL\n", consecutive_failures);
                            if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                last_art_url = url;  // Mark as done
                                art_show_placeholder = true;
                                xSemaphoreGive(art_mutex);
                            }
                            consecutive_failures = 0;  // Reset for next URL
                            last_failed_url[0] = '\0';
                        }
                    }

                    if (!http_early_closed) {
                        // Normal TCP close — always reached since early-close was removed.
                        // By this point the server's FIN has arrived during decode/scale (~200-400ms).
                        // lwIP auto-ACKed it → we're in CLOSE_WAIT. http.end() sends our FIN →
                        // LAST_ACK → CLOSED. Server enters TIME_WAIT (not us) → zero DMA cost per download.
                        // For HTTPS: also stop secure_client to free TLS DMA session buffers.
                        http.end();
                        if (use_https) secure_client.stop();
                        artLogMem("post-close");

                        // Drain delay: local HTTP needs 200ms for final ACK exchange, HTTPS needs 500ms.
                        vTaskDelay(pdMS_TO_TICKS(use_https ? SDIO_HTTPS_TCP_CLOSE_MS : SDIO_TCP_CLOSE_MS));

                        // Update timestamps before releasing mutex
                        last_network_end_ms      = millis();
                        last_art_download_end_ms = millis();   // gate lyrics + queue cooldowns + inter-download
                        if (use_https) last_https_end_ms = millis();
                    }

                    // jpgBuf points to static art_jpgbuf — never freed between downloads.
                    jpgBuf = nullptr;
                    // Release network_mutex after ALL network activity including TLS cleanup
                    if (mutex_acquired) {
                        xSemaphoreGive(network_mutex);
                    }
                } else {
                    // Mutex not acquired - clean up HTTP setup (no active connection)
                    http.end();
                }

            } // http and secure_client destructors - no-op since already stopped
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Check for new URLs
    }
}

// Invalidate LRU art cache — call on track change so stale art never flashes.
// Safe to call from any thread: valid flag is volatile-written then the art task
// reads it; worst case the task serves one stale frame before seeing the clear.
void clearAlbumArtCache() {
    art_cache[0].valid = false;
    art_cache[1].valid = false;
    Serial.println("[ART] LRU cache cleared");
}

// URL encode helper for proxying HTTPS URLs through Sonos
// Optimized: Uses fixed buffer to avoid String reallocation fragmentation
String urlEncode(const char* url) {
    static char encoded[1024];  // Static buffer, URLs rarely exceed 512 chars
    int out_idx = 0;

    for (int i = 0; url[i] && out_idx < sizeof(encoded) - 4; i++) {
        char c = url[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ':' || c == '/') {
            encoded[out_idx++] = c;
        } else {
            // Encode as %XX (3 chars + null terminator)
            int written = snprintf(&encoded[out_idx], 4, "%%%02X", (unsigned char)c);
            if (written > 0) out_idx += written;
        }
    }
    encoded[out_idx] = '\0';
    return String(encoded);
}

// H-4: thread-safe replacement for the cross-task `pending_art_url != last_art_url`
// String compare in the polling task. Those globals are reassigned under art_mutex by
// the UI and art tasks; the polling task (Core 1, prio 3) read them lock-free → torn /
// dangling String pointer (same race class as CR-1). art_mutex is only ever held for
// microsecond-long String writes (never during a download), so this brief take is cheap.
// Returns true when a requested art URL has not yet been displayed. On the (near-
// impossible) mutex miss we default to "pending" — biases toward skipping a SOAP cycle,
// the SDIO-safe choice; the next cycle re-evaluates.
bool isAlbumArtPending() {
    bool pending = true;
    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(10))) {
        pending = (pending_art_url != last_art_url);
        xSemaphoreGive(art_mutex);
    }
    return pending;
}

void requestAlbumArt(const String& url) {
    if (url.length() == 0) return;
    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(10))) {
        pending_art_url = url;
        xSemaphoreGive(art_mutex);
    }
    // Do NOT set art_download_in_progress here. Flag is set AFTER sdioPreWait in the
    // art task loop, where flag=false allows polling to keep SDIO warm during the
    // storm-gate wait. Setting it here blocks polling → SDIO idle → DMA clock-gate.
    last_track_change_ms = millis();
    // Track change from any controller (ESP32 or Sonos app) counts as activity.
    // Prevents the clock screensaver firing just because the user controlled Sonos
    // from their phone/computer without touching the ESP32.
    resetScreenTimeout();
}
