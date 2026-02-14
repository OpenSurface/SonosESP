/**
 * UI Album Art Handling
 * Album art loading with ESP32-P4 hardware JPEG decoder + PNGdec + bilinear scaling
 */

#include "ui_common.h"
#include "config.h"
#include <PNGdec.h>

// ESP32-P4 Hardware JPEG Decoder
#include "driver/jpeg_decode.h"
static jpeg_decoder_handle_t hw_jpeg_decoder = nullptr;

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

// Fast bilinear scaling using fixed-point math - solves JPEGDEC's 1/2/4/8 limitation!
void scaleImageBilinear(uint16_t* src, int src_w, int src_h, uint16_t* dst, int dst_w, int dst_h) {
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
        uint16_t* src_row0 = &src[y0 * src_w];
        uint16_t* src_row1 = &src[y1 * src_w];

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

// PNGdec callback - decode to temporary buffer
static int pngDraw(PNGDRAW* pDraw) {
    if (!jpeg_decode_buffer) return 0;

    // Get RGB565 pixels from PNG decoder
    uint16_t lineBuffer[512];  // Max width we support
    int w = pDraw->iWidth;
    if (w > 512) w = 512;

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

    // CRITICAL FIX: Downgrade HTTPS → HTTP for public CDN URLs
    // Removes ALL TLS overhead (handshake, encryption, DMA memory)
    // This is the KEY to SDIO stability - no TLS = no crashes!
    // Public album art doesn't need encryption
    if (fetchUrl.startsWith("https://")) {
        // Spotify CDN
        if (fetchUrl.indexOf("i.scdn.co") != -1 ||
            fetchUrl.indexOf("mosaic.scdn.co") != -1) {
            fetchUrl.replace("https://", "http://");
        }
        // Deezer CDN
        else if (fetchUrl.indexOf("dzcdn.net") != -1) {
            fetchUrl.replace("https://", "http://");
        }
        // TuneIn CDN
        else if (fetchUrl.indexOf("cdn-profiles.tunein.com") != -1 ||
                 fetchUrl.indexOf("cdn-radiotime-logos.tunein.com") != -1) {
            fetchUrl.replace("https://", "http://");
        }
        // Add other public CDNs as needed
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
    }

    return fetchUrl;
}

void albumArtTask(void* param) {
    art_buffer = (uint16_t*)heap_caps_malloc(ART_SIZE * ART_SIZE * 2, MALLOC_CAP_SPIRAM);
    art_temp_buffer = (uint16_t*)heap_caps_malloc(ART_SIZE * ART_SIZE * 2, MALLOC_CAP_SPIRAM);
    if (!art_buffer || !art_temp_buffer) { vTaskDelete(NULL); return; }

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

    // Temporary buffer for decoded full-size image
    uint16_t* decoded_buffer = nullptr;

    while (1) {
        // Check if shutdown requested (for OTA update)
        if (art_shutdown_requested) {
            Serial.println("[ART] Shutdown requested");
            Serial.printf("[ART] Shutdown complete - Free DMA: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
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
                    // New URL detected - reset failure tracking for clean start
                    consecutive_failures = 0;
                    last_failed_url[0] = '\0';
                }
            }
            xSemaphoreGive(art_mutex);
        }
        if (url[0] != '\0') {
            Serial.printf("[ART] URL: %s\n", url);

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
            bool use_https = (strncmp(url, "https://", 8) == 0);

            // Scoped HTTP/HTTPS download - ensures TLS session is freed after each download
            {
                HTTPClient http;
                WiFiClientSecure secure_client;
                bool mutex_acquired = false;

                // PRE-WAIT: Wait for cooldowns BEFORE acquiring mutex
                // This prevents blocking SOAP commands (Next/Prev/Play) during cooldown waits
                // Local Sonos HTTP (port 1400) skips cooldowns - local network, no TLS
                if (!isFromSonosDevice) {
                    // General SDIO cooldown (200ms since last network op)
                    unsigned long now = millis();
                    unsigned long elapsed = now - last_network_end_ms;
                    if (last_network_end_ms > 0 && elapsed < 200) {
                        vTaskDelay(pdMS_TO_TICKS(200 - elapsed));
                    }

                    // HTTPS-specific cooldown (2000ms since last HTTPS)
                    if (use_https) {
                        now = millis();
                        elapsed = now - last_https_end_ms;
                        if (last_https_end_ms > 0 && elapsed < 2000) {
                            unsigned long wait_ms = 2000 - elapsed;
                            Serial.printf("[ART] HTTPS cooldown: waiting %lums (elapsed: %lums)\n", wait_ms, elapsed);
                            vTaskDelay(pdMS_TO_TICKS(wait_ms));
                        }
                    }
                }

                // ABORT CHECK: If track changed during cooldown, bail out before acquiring mutex
                if (art_abort_download) {
                    art_abort_download = false;
                    continue;
                }

                // Acquire network mutex (all network activity serialized)
                mutex_acquired = xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_ART_MS));
                if (!mutex_acquired) {
                    Serial.println("[ART] Failed to acquire network mutex - skipping download");
                }

                if (mutex_acquired) {
                    // ABORT CHECK: If track changed while waiting for mutex, bail out immediately
                    if (art_abort_download) {
                        Serial.println("[ART] Track changed while waiting for mutex - skipping");
                        art_abort_download = false;
                        xSemaphoreGive(network_mutex);
                        mutex_acquired = false;
                        continue;
                    }

                    // Re-check cooldowns under mutex (another task may have used network while we waited)
                    // Only for non-local URLs; local Sonos HTTP doesn't need cooldowns
                    if (!isFromSonosDevice) {
                        unsigned long now = millis();
                        unsigned long elapsed = now - last_network_end_ms;
                        if (last_network_end_ms > 0 && elapsed < 200) {
                            vTaskDelay(pdMS_TO_TICKS(200 - elapsed));
                        }
                        if (use_https) {
                            now = millis();
                            elapsed = now - last_https_end_ms;
                            if (last_https_end_ms > 0 && elapsed < 2000) {
                                vTaskDelay(pdMS_TO_TICKS(2000 - elapsed));
                            }
                        }
                    }

                    // Set up HTTP connection (inside mutex - all network activity serialized)
                    if (use_https) {
                        secure_client.setInsecure();
                        http.begin(secure_client, url);
                    } else {
                        http.begin(url);
                    }
                    // Local Sonos: 3s timeout (LAN responds in <1s, 3s catches slow devices)
                    // Internet: 10s timeout (CDN/remote servers can be slow)
                    http.setTimeout(isFromSonosDevice ? 3000 : 10000);

                    int code = http.GET();
                    // Keep mutex locked for entire download

                    if (code == 200) {
                int len = http.getSize();
                const size_t max_art_size = MAX_ART_SIZE;
                const bool len_known = (len > 0);
                if ((len_known && len < (int)max_art_size) || !len_known) {
                    if (len_known) {
                        Serial.printf("[ART] Downloading album art: %d bytes\n", len);
                    } else {
                        Serial.println("[ART] Downloading album art: unknown length");
                    }
                    size_t alloc_len = len_known ? (size_t)len : max_art_size;
                    uint8_t* jpgBuf = (uint8_t*)heap_caps_malloc(alloc_len, MALLOC_CAP_SPIRAM);
                    if (jpgBuf) {
                        WiFiClient* stream = http.getStreamPtr();

                        // For local Sonos HTTP: release mutex during download
                        // SOAP commands (Next/Prev/Play) can run while art downloads
                        // Local HTTP has no TLS, safe without mutex
                        if (isFromSonosDevice && mutex_acquired) {
                            xSemaphoreGive(network_mutex);
                            mutex_acquired = false;
                        }

                        // Chunked reading to avoid WiFi buffer issues
                        const size_t chunkSize = ART_CHUNK_SIZE;
                        size_t bytesRead = 0;
                        bool readSuccess = true;

                        // Read loop: keep going while connected OR data still buffered
                        // Server may close connection before we read all buffered bytes
                        while ((stream->connected() || stream->available()) && bytesRead < alloc_len) {
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
                            // Yield to WiFi/SDIO task
                            // Local Sonos: minimal yield (no TLS, fast local network)
                            // Internet HTTP: 5ms (no TLS overhead)
                            // Internet HTTPS: 15ms (TLS encryption overhead)
                            if (isFromSonosDevice) {
                                taskYIELD();
                            } else {
                                vTaskDelay(pdMS_TO_TICKS(use_https ? 15 : 5));
                            }
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

                        Serial.printf("[ART] Album art read: %d bytes (len_known=%d)\n", (int)bytesRead, len_known ? 1 : 0);

                        // If download failed/aborted, close connection and free TLS/DMA resources
                        if (!readSuccess) {
                            Serial.println("[ART] Download failed/aborted - closing connection");
                            stream->stop();  // TCP RST - kills connection immediately
                            heap_caps_free(jpgBuf);
                            // CRITICAL: http.end() + secure_client.stop() free TLS/DMA memory
                            // After TCP RST, these won't send SDIO traffic (socket is dead)
                            // but they WILL release DMA buffers used by esp-aes
                            http.end();
                            if (use_https) secure_client.stop();
                            // Wait for in-flight packets to flush
                            // Local Sonos: 50ms (minimal, no TLS)
                            // Internet HTTP: 300ms (simple TCP cleanup)
                            // Internet HTTPS: 1000ms (TLS session + TCP cleanup)
                            vTaskDelay(pdMS_TO_TICKS(isFromSonosDevice ? 50 : (use_https ? 1000 : 300)));
                            last_network_end_ms = millis();
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
                                    xSemaphoreGive(art_mutex);
                                }
                                consecutive_failures = 0;
                                last_failed_url[0] = '\0';
                            }
                        }
                        if (sizeOk && readSuccess) {
                            // Detect image format by magic bytes
                            bool isJPEG = (read >= 3 && jpgBuf[0] == 0xFF && jpgBuf[1] == 0xD8 && jpgBuf[2] == 0xFF);
                            bool isPNG = (read >= 4 && jpgBuf[0] == 0x89 && jpgBuf[1] == 0x50 && jpgBuf[2] == 0x4E && jpgBuf[3] == 0x47);

                            // Only decode PNG for radio station logos (not regular album art)
                            if (isPNG && isStationLogo) {
                                Serial.printf("[ART] Opening PNG with %d bytes\n", read);
                                int pngResult = png.openRAM(jpgBuf, read, pngDraw);
                                if (pngResult == 0) {  // PNG_SUCCESS = 0 (different from JPEG!)
                                    Serial.println("[ART] PNG openRAM success");
                                    int w = png.getWidth();
                                    int h = png.getHeight();

                                    // Validate PNG dimensions to prevent crashes from malformed files
                                    // Also check for integer overflow: w*h*2 must fit in size_t and be reasonable (< 10MB)
                                    if (w == 0 || h == 0 || w > 2048 || h > 2048 ||
                                        (size_t)w * (size_t)h * 2 > 10*1024*1024) {
                                        Serial.printf("[ART] Invalid PNG dimensions: %dx%d (max 2048x2048, 10MB)\n", w, h);
                                        png.close();
                                        if (decoded_buffer) { heap_caps_free(decoded_buffer); decoded_buffer = nullptr; }
                                        heap_caps_free(jpgBuf);
                                        jpgBuf = nullptr;
                                        // Cleanup HTTP and release mutex before continue
                                        http.end();
                                        if (use_https) secure_client.stop();
                                        vTaskDelay(pdMS_TO_TICKS(200));
                                        last_network_end_ms = millis();
                                        if (use_https) last_https_end_ms = millis();
                                        xSemaphoreGive(network_mutex);
                                        mutex_acquired = false;
                                        continue;
                                    }

                                    jpeg_image_width = w;   // Reuse for PNG
                                    jpeg_image_height = h;  // Reuse for PNG
                                    jpeg_output_width = 0;
                                    jpeg_output_height = 0;
                                    Serial.printf("[ART] PNG: %dx%d\n", w, h);

                                    // Allocate buffer for full decoded image
                                    size_t decoded_size = w * h * 2;
                                    if (decoded_buffer) {
                                        heap_caps_free(decoded_buffer);
                                    }
                                    decoded_buffer = (uint16_t*)heap_caps_malloc(decoded_size, MALLOC_CAP_SPIRAM);

                                    if (decoded_buffer) {
                                        jpeg_decode_buffer = decoded_buffer;
                                        memset(decoded_buffer, 0, decoded_size);

                                        // Decode PNG
                                        png.decode(NULL, 0);
                                        png.close();

                                        Serial.printf("[ART] Decoded %dx%d\n", w, h);
                                        jpeg_decode_buffer = nullptr;

                                        if (art_temp_buffer) {
                                            int out_w = jpeg_output_width > 0 ? jpeg_output_width : w;
                                            int out_h = jpeg_output_height > 0 ? jpeg_output_height : h;
                                            uint16_t* src_buffer = decoded_buffer;
                                            bool needs_compact = (out_w != w) || (out_h != h);

                                            if (needs_compact) {
                                                Serial.printf("[ART] Output size: %dx%d (scaled)\n", out_w, out_h);
                                                size_t compact_size = (size_t)out_w * (size_t)out_h * 2;
                                                uint16_t* compact_buffer = (uint16_t*)heap_caps_malloc(compact_size, MALLOC_CAP_SPIRAM);
                                                if (compact_buffer) {
                                                    for (int y = 0; y < out_h; y++) {
                                                        memcpy(&compact_buffer[y * out_w],
                                                               &decoded_buffer[y * w],
                                                               (size_t)out_w * 2);
                                                    }
                                                    src_buffer = compact_buffer;
                                                } else {
                                                    Serial.println("[ART] Failed to allocate compact buffer");
                                                    out_w = w;
                                                    out_h = h;
                                                }
                                            }

                                            // Clear output buffer
                                            memset(art_temp_buffer, 0, ART_SIZE * ART_SIZE * 2);

                                            // Scale to exact 420x420 using bilinear interpolation
                                            Serial.printf("[ART] Bilinear scaling %dx%d -> 420x420\n", out_w, out_h);
                                            scaleImageBilinear(src_buffer, out_w, out_h, art_temp_buffer, ART_SIZE, ART_SIZE);
                                            Serial.println("[ART] Scaling complete");

                                            if (src_buffer != decoded_buffer) {
                                                heap_caps_free(src_buffer);
                                            }
                                            // Free decoded buffer immediately - don't hold 800KB until next image
                                            heap_caps_free(decoded_buffer);
                                            decoded_buffer = nullptr;

                                            // Sample dominant color from scaled image
                                            sampleDominantColor(art_temp_buffer, ART_SIZE, ART_SIZE);

                                            // Calculate dominant color
                                            uint32_t new_color = 0x1a1a1a;  // Default dark color
                                            if (color_sample_count > 0) {
                                                uint8_t avg_r = color_r_sum / color_sample_count;
                                                uint8_t avg_g = color_g_sum / color_sample_count;
                                                uint8_t avg_b = color_b_sum / color_sample_count;

                                                // Darken for background (multiply by 0.4)
                                                avg_r = (avg_r * 4) / 10;
                                                avg_g = (avg_g * 4) / 10;
                                                avg_b = (avg_b * 4) / 10;

                                                new_color = (avg_r << 16) | (avg_g << 8) | avg_b;
                                            }

                                            // Copy completed image from temp to display buffer atomically
                                            memcpy(art_buffer, art_temp_buffer, ART_SIZE * ART_SIZE * 2);

                                            memset(&art_dsc, 0, sizeof(art_dsc));
                                            art_dsc.header.w = ART_SIZE;
                                            art_dsc.header.h = ART_SIZE;
                                            art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
                                            art_dsc.data_size = ART_SIZE * ART_SIZE * 2;
                                            art_dsc.data = (const uint8_t*)art_buffer;

                                            // Update all shared variables atomically under mutex
                                            if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                                last_art_url = url;
                                                dominant_color = new_color;
                                                art_ready = true;
                                                color_ready = true;
                                                xSemaphoreGive(art_mutex);
                                            }
                                            // Reset failure counter on success
                                            consecutive_failures = 0;
                                            last_failed_url[0] = '\0';
                                        }
                                    } else {
                                        Serial.printf("[ART] Failed to allocate %d bytes for decoded image\n", (int)decoded_size);
                                    }
                                } else {
                                    Serial.printf("[ART] PNG openRAM failed - error code: %d\n", pngResult);
                                }
                            } else if (isPNG && !isStationLogo) {
                                // PNG detected but not a station logo - skip (only JPEG for normal album art)
                                Serial.println("[ART] PNG detected but not station logo - skipping");
                                // Mark as done to prevent infinite retry loop
                                if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                    last_art_url = url;
                                    xSemaphoreGive(art_mutex);
                                }
                            } else if (isJPEG && hw_jpeg_decoder) {
                                // ESP32-P4 Hardware JPEG Decoder - fast and stable!
                                Serial.printf("[ART] HW JPEG decode: %d bytes\n", read);

                                // CRITICAL: ESP32-P4 HW decoder fails on COM markers (error 258)
                                // Strip COM markers (0xFFFE) to prevent "COM marker data underflow" errors
                                size_t cleaned_size = read;
                                for (size_t i = 0; i < read - 1; ) {
                                    if (jpgBuf[i] == 0xFF && jpgBuf[i+1] == 0xFE) {
                                        // Found COM marker - get length
                                        if (i + 3 < read) {
                                            uint16_t len = (jpgBuf[i+2] << 8) | jpgBuf[i+3];
                                            // Remove marker + length + data
                                            size_t marker_total = 2 + len;
                                            if (i + marker_total <= read) {
                                                memmove(&jpgBuf[i], &jpgBuf[i + marker_total], read - i - marker_total);
                                                cleaned_size -= marker_total;
                                                Serial.printf("[ART] Stripped COM marker (%d bytes)\n", marker_total);
                                                continue;  // Don't increment i, check same position again
                                            }
                                        }
                                    }
                                    i++;
                                }

                                // Get image dimensions from header (no hardware needed)
                                jpeg_decode_picture_info_t pic_info;
                                esp_err_t ret = jpeg_decoder_get_info(jpgBuf, cleaned_size, &pic_info);
                                if (ret == ESP_OK) {
                                    int w = pic_info.width;
                                    int h = pic_info.height;

                                    // Validate JPEG dimensions to prevent crashes/overflow from malformed files
                                    if (w == 0 || h == 0 || w > 2048 || h > 2048) {
                                        Serial.printf("[ART] Invalid JPEG dimensions: %dx%d (max 2048x2048)\n", w, h);
                                        heap_caps_free(jpgBuf);
                                        jpgBuf = nullptr;
                                        http.end();
                                        if (use_https) secure_client.stop();
                                        vTaskDelay(pdMS_TO_TICKS(200));
                                        last_network_end_ms = millis();
                                        if (use_https) last_https_end_ms = millis();
                                        xSemaphoreGive(network_mutex);
                                        mutex_acquired = false;
                                        continue;
                                    }

                                    // Hardware outputs dimensions rounded to 16-pixel boundary
                                    int out_w = ((w + 15) / 16) * 16;
                                    int out_h = ((h + 15) / 16) * 16;
                                    bool is_grayscale = (pic_info.sample_method == JPEG_DOWN_SAMPLING_GRAY);
                                    Serial.printf("[ART] JPEG: %dx%d (output: %dx%d)%s\n", w, h, out_w, out_h,
                                                  is_grayscale ? " [GRAYSCALE]" : "");

                                    // Allocate DMA output buffer
                                    // Grayscale: 1 byte/pixel, Color: 2 bytes/pixel (RGB565)
                                    size_t bytes_per_pixel = is_grayscale ? 1 : 2;
                                    size_t decoded_size = out_w * out_h * bytes_per_pixel;
                                    jpeg_decode_memory_alloc_cfg_t rx_mem_cfg = {
                                        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
                                    };
                                    size_t rx_buffer_size = 0;
                                    uint8_t* hw_out_buf = (uint8_t*)jpeg_alloc_decoder_mem(decoded_size, &rx_mem_cfg, &rx_buffer_size);

                                    if (hw_out_buf) {
                                        // Configure hardware decoder output format
                                        jpeg_decode_cfg_t decode_cfg = {
                                            .output_format = is_grayscale ? JPEG_DECODE_OUT_FORMAT_GRAY : JPEG_DECODE_OUT_FORMAT_RGB565,
                                            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,  // Little endian
                                            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
                                        };

                                        uint32_t out_size = 0;
                                        ret = jpeg_decoder_process(hw_jpeg_decoder, &decode_cfg, jpgBuf, cleaned_size, hw_out_buf, rx_buffer_size, &out_size);

                                        // For grayscale: convert GRAY8 to RGB565
                                        if (ret == ESP_OK && is_grayscale) {
                                            Serial.println("[ART] Converting grayscale to RGB565");
                                            uint16_t* rgb_buf = (uint16_t*)heap_caps_malloc(out_w * out_h * 2, MALLOC_CAP_SPIRAM);
                                            if (rgb_buf) {
                                                int total_pixels = out_w * out_h;
                                                for (int i = 0; i < total_pixels; i++) {
                                                    uint8_t g = hw_out_buf[i];
                                                    rgb_buf[i] = ((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3);
                                                }
                                                heap_caps_free(hw_out_buf);  // Free DMA gray buffer
                                                hw_out_buf = (uint8_t*)rgb_buf;  // Now points to RGB565
                                            } else {
                                                Serial.println("[ART] Grayscale conversion alloc failed");
                                                heap_caps_free(hw_out_buf);
                                                hw_out_buf = nullptr;
                                                ret = ESP_FAIL;
                                            }
                                        }

                                        if (ret == ESP_OK && hw_out_buf) {
                                            Serial.printf("[ART] HW decoded: %d bytes\n", out_size);

                                            // Scale to 420x420 using bilinear interpolation
                                            memset(art_temp_buffer, 0, ART_SIZE * ART_SIZE * 2);
                                            Serial.printf("[ART] Bilinear scaling %dx%d -> 420x420\n", w, h);
                                            // Use actual image dimensions for scaling (not padded)
                                            scaleImageBilinear((uint16_t*)hw_out_buf, out_w, out_h, art_temp_buffer, ART_SIZE, ART_SIZE);
                                            Serial.println("[ART] Scaling complete");

                                            // Free hardware buffer immediately
                                            heap_caps_free(hw_out_buf);
                                            hw_out_buf = nullptr;

                                            // Sample dominant color from scaled image
                                            sampleDominantColor(art_temp_buffer, ART_SIZE, ART_SIZE);

                                            // Calculate dominant color
                                            uint32_t new_color = 0x1a1a1a;  // Default dark color
                                            if (color_sample_count > 0) {
                                                uint8_t avg_r = color_r_sum / color_sample_count;
                                                uint8_t avg_g = color_g_sum / color_sample_count;
                                                uint8_t avg_b = color_b_sum / color_sample_count;

                                                // Darken for background (multiply by 0.4)
                                                avg_r = (avg_r * 4) / 10;
                                                avg_g = (avg_g * 4) / 10;
                                                avg_b = (avg_b * 4) / 10;

                                                new_color = (avg_r << 16) | (avg_g << 8) | avg_b;
                                            }

                                            // Copy completed image from temp to display buffer atomically
                                            memcpy(art_buffer, art_temp_buffer, ART_SIZE * ART_SIZE * 2);

                                            memset(&art_dsc, 0, sizeof(art_dsc));
                                            art_dsc.header.w = ART_SIZE;
                                            art_dsc.header.h = ART_SIZE;
                                            art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
                                            art_dsc.data_size = ART_SIZE * ART_SIZE * 2;
                                            art_dsc.data = (const uint8_t*)art_buffer;

                                            // Update all shared variables atomically under mutex
                                            if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                                last_art_url = url;
                                                dominant_color = new_color;
                                                art_ready = true;
                                                color_ready = true;
                                                xSemaphoreGive(art_mutex);
                                            }
                                            // Reset failure counter on success
                                            consecutive_failures = 0;
                                            last_failed_url[0] = '\0';
                                        } else {
                                            Serial.printf("[ART] HW JPEG decode failed: %d\n", ret);
                                            if (hw_out_buf) heap_caps_free(hw_out_buf);
                                            // Track decode failures to prevent infinite retry
                                            if (strcmp(url, last_failed_url) == 0) {
                                                consecutive_failures++;
                                            } else {
                                                strncpy(last_failed_url, url, sizeof(last_failed_url) - 1);
                                                last_failed_url[sizeof(last_failed_url) - 1] = '\0';
                                                consecutive_failures = 1;
                                            }
                                            // Exponential backoff: 200ms, 400ms, 600ms... (prevents rapid retry hammering)
                                            if (consecutive_failures > 1) {
                                                vTaskDelay(pdMS_TO_TICKS(consecutive_failures * 200));
                                            }
                                            if (consecutive_failures >= 3) {
                                                Serial.printf("[ART] Decode failed %d times, skipping URL\n", consecutive_failures);
                                                if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                                    last_art_url = url;
                                                    xSemaphoreGive(art_mutex);
                                                }
                                                consecutive_failures = 0;
                                                last_failed_url[0] = '\0';
                                            }
                                        }
                                    } else {
                                        Serial.printf("[ART] Failed to allocate %d bytes for HW decode\n", (int)decoded_size);
                                    }
                                } else {
                                    Serial.printf("[ART] JPEG header parse failed: %d\n", ret);
                                }
                            } else if (isJPEG) {
                                // Fallback: Software JPEG decode (if hardware not available)
                                Serial.println("[ART] HW JPEG unavailable, skipping");
                                // Mark as done to prevent retry
                                if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                    last_art_url = url;
                                    xSemaphoreGive(art_mutex);
                                }
                            } else {
                                Serial.println("[ART] Unknown image format (not JPEG or PNG)");
                                // Mark as done to prevent retry loop
                                if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                    last_art_url = url;
                                    xSemaphoreGive(art_mutex);
                                }
                            }
                        }
                        heap_caps_free(jpgBuf);
                    } else {
                        Serial.printf("[ART] Failed to allocate %d bytes for album art\n", len);
                        // Mark as done - memory issue, retry won't help
                        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                            last_art_url = url;
                            xSemaphoreGive(art_mutex);
                        }
                    }
                } else if (len >= (int)max_art_size) {
                    Serial.printf("[ART] Album art too large: %d bytes (max %dKB)\n", len, (int)(max_art_size/1000));
                    // Force close - don't drain (overwhelms SDIO buffer)
                    WiFiClient* stream = http.getStreamPtr();
                    stream->stop();
                    Serial.println("[ART] Connection closed (oversized image)");
                    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                        last_art_url = url;
                        xSemaphoreGive(art_mutex);
                    }
                    // CRITICAL: Free TLS/DMA resources before releasing mutex
                    http.end();
                    if (use_https) secure_client.stop();
                    // Wait for in-flight packets to flush (HTTP: 300ms, HTTPS: 1000ms)
                    vTaskDelay(pdMS_TO_TICKS(use_https ? 1000 : 300));
                    last_network_end_ms = millis();
                    if (use_https) last_https_end_ms = millis();
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
                                xSemaphoreGive(art_mutex);
                            }
                            consecutive_failures = 0;  // Reset for next URL
                            last_failed_url[0] = '\0';
                        }
                    }

                    // End HTTP and close TLS BEFORE releasing mutex
                    http.end();
                    if (use_https) secure_client.stop();

                    // Wait for cleanup and SDIO buffer stabilization
                    // Local Sonos: 10ms (minimal, no TLS)
                    // Internet HTTP: 50ms (fast cleanup)
                    // Internet HTTPS: 200ms (TLS cleanup)
                    vTaskDelay(pdMS_TO_TICKS(isFromSonosDevice ? 10 : (use_https ? 200 : 50)));

                    // Update timestamps before releasing mutex
                    last_network_end_ms = millis();
                    if (use_https) last_https_end_ms = millis();

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

void requestAlbumArt(const String& url) {
    if (url.length() == 0) return;
    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(10))) {
        pending_art_url = url;
        xSemaphoreGive(art_mutex);
    }
}
