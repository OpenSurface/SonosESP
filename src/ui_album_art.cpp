/**
 * UI Album Art Handling
 * Album art loading with ESP32-P4 hardware JPEG decoder + PNGdec + bilinear scaling
 */

#include "ui_common.h"
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

// Apply dominant color instantly to both panels and update button feedback colors
void setBackgroundColor(uint32_t hex_color) {
    lv_color_t color = lv_color_hex(hex_color);
    if (panel_art) {
        lv_obj_set_style_bg_color(panel_art, color, LV_PART_MAIN);
    }
    if (panel_right) {
        lv_obj_set_style_bg_color(panel_right, color, LV_PART_MAIN);
    }

    // Make progress bar indicator AND knob match dominant color (but brighter - 2x)
    uint8_t r = ((hex_color >> 16) & 0xFF);
    uint8_t g = ((hex_color >> 8) & 0xFF);
    uint8_t b = (hex_color & 0xFF);

    // Brighten by 2x (capped at 255)
    r = min(r * 2, 255);
    g = min(g * 2, 255);
    b = min(b * 2, 255);

    lv_color_t bright_color = lv_color_make(r, g, b);

    if (slider_progress) {
        lv_obj_set_style_bg_color(slider_progress, bright_color, LV_PART_INDICATOR);  // Bar
        lv_obj_set_style_bg_color(slider_progress, bright_color, LV_PART_KNOB);  // Circle/dot
    }

    // Update all button pressed states to use dominant color (same brightness as progress bar)
    if (btn_play) lv_obj_set_style_bg_color(btn_play, bright_color, LV_STATE_PRESSED);
    if (btn_prev) {
        lv_obj_set_style_bg_color(btn_prev, bright_color, LV_STATE_PRESSED);
        lv_obj_t* ico = lv_obj_get_child(btn_prev, 0);
        if (ico) lv_obj_set_style_text_color(ico, bright_color, LV_STATE_PRESSED);
    }
    if (btn_next) {
        lv_obj_set_style_bg_color(btn_next, bright_color, LV_STATE_PRESSED);
        lv_obj_t* ico = lv_obj_get_child(btn_next, 0);
        if (ico) lv_obj_set_style_text_color(ico, bright_color, LV_STATE_PRESSED);
    }
    if (btn_mute) lv_obj_set_style_bg_color(btn_mute, bright_color, LV_STATE_PRESSED);
    if (btn_shuffle) lv_obj_set_style_bg_color(btn_shuffle, bright_color, LV_STATE_PRESSED);
    if (btn_repeat) lv_obj_set_style_bg_color(btn_repeat, bright_color, LV_STATE_PRESSED);
    if (btn_queue) lv_obj_set_style_bg_color(btn_queue, bright_color, LV_STATE_PRESSED);
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
            // Sample only edges (50px margin) and every 15th pixel
            if (((x | y) % 15 == 0) && (y < 50 || y > height - 50 || x < 50 || x > width - 50)) {
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
    // Use 16.16 fixed-point for integer math (faster than float)
    int x_ratio = ((src_w - 1) << 16) / dst_w;
    int y_ratio = ((src_h - 1) << 16) / dst_h;

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
        Serial.println("[ART] Deezer - reduced to 400x400");
    }
    // TuneIn (cdn-profiles.tunein.com): keep original size
    // Note: logoq is 145x145, logog is 600x600 (too big for PNG decode)
    if (fetchUrl.indexOf("cdn-profiles.tunein.com") != -1 && fetchUrl.indexOf("?d=") != -1) {
        fetchUrl.replace("?d=1024", "?d=400");
        fetchUrl.replace("?d=600", "?d=400");
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

    // HTTPClient for album art
    HTTPClient http;
    WiFiClientSecure secure_client;
    secure_client.setInsecure();  // Skip certificate validation for album art hosts
    static char url[512];

    // Temporary buffer for decoded full-size image
    uint16_t* decoded_buffer = nullptr;

    while (1) {
        // Check if shutdown requested (for OTA update)
        if (art_shutdown_requested) {
            Serial.println("[ART] Shutdown requested - cleaning up SSL client");

            // CRITICAL: Explicitly stop WiFiClientSecure to free ALL SSL session cache
            // Just letting destructor run doesn't always free cached session tickets
            secure_client.stop();  // Close connection and free SSL buffers
            http.end();             // End HTTP client

            Serial.printf("[ART] SSL cleanup complete - Free DMA: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_DMA));

            albumArtTaskHandle = NULL;  // Clear handle before deleting
            vTaskDelete(NULL);  // Delete self
            return;
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
                }
            }
            xSemaphoreGive(art_mutex);
        }
        if (url[0] != '\0') {
            Serial.printf("[ART] URL: %s\n", url);

            // Simple WiFi check - don't try to download if not connected
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[ART] WiFi not connected, skipping");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            // Detect if URL is from Sonos device itself (e.g., /getaa for YouTube Music)
            // These don't need per-chunk mutex since Sonos HTTP server serializes requests anyway
            bool isFromSonosDevice = (strstr(url, ":1400/") != nullptr);

            bool use_https = (strncmp(url, "https://", 8) == 0);
            if (use_https) {
                http.begin(secure_client, url);
            } else {
                http.begin(url);
            }
            http.setTimeout(10000);

            // REVERT TO v1.1.1: Hold network_mutex for ENTIRE download (no per-chunk)
            // This prevents SDIO crashes but blocks SOAP during art download
            if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_ART_MS))) {
                Serial.println("[ART] Failed to acquire network mutex - skipping download");
                http.end();
                continue;
            }

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

                        // Chunked reading to avoid WiFi buffer issues
                        const size_t chunkSize = ART_CHUNK_SIZE;
                        size_t bytesRead = 0;
                        bool readSuccess = true;

                        while (stream->connected() && bytesRead < alloc_len) {
                            // Check if source changed - abort download immediately
                            if (art_abort_download) {
                                Serial.println("[ART] Source changed - aborting current download");
                                art_abort_download = false;  // Clear flag
                                readSuccess = false;
                                break;
                            }

                            size_t available = stream->available();
                            if (available == 0) {
                                vTaskDelay(pdMS_TO_TICKS(1));
                                if (!stream->connected()) break;
                                continue;
                            }

                            size_t remaining = len_known ? ((size_t)len - bytesRead) : (alloc_len - bytesRead);
                            size_t toRead = min(chunkSize, remaining);
                            toRead = min(toRead, available);

                            // Mutex held for entire download - no per-chunk locking
                            size_t actualRead = stream->readBytes(jpgBuf + bytesRead, toRead);

                            if (actualRead == 0) {
                                if (len_known) {
                                    Serial.printf("[ART] Read timeout at %d/%d bytes\n", (int)bytesRead, len);
                                    readSuccess = false;
                                }
                                break;
                            }

                            bytesRead += actualRead;
                            // Yield to WiFi/SDIO task - 5ms prevents RX buffer overflow on large HTTPS downloads
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }

                        if (!len_known && bytesRead >= max_art_size) {
                            Serial.println("[ART] Album art too large (max 280KB)");
                            readSuccess = false;
                        }

                        // CRITICAL: Drain connection if aborted to prevent WiFi SDIO buffer overflow
                        if (!readSuccess && len_known && bytesRead < len) {
                            Serial.printf("[ART] Draining aborted connection: %d/%d bytes remaining\n", len - bytesRead, len);
                            uint8_t drainBuf[512];
                            size_t remaining = len - bytesRead;
                            size_t drained = 0;
                            unsigned long startDrain = millis();

                            while (stream->connected() && drained < remaining) {
                                size_t available = stream->available();
                                if (available > 0) {
                                    size_t toRead = min((size_t)512, available);
                                    toRead = min(toRead, remaining - drained);
                                    size_t read = stream->readBytes(drainBuf, toRead);
                                    drained += read;
                                } else {
                                    vTaskDelay(pdMS_TO_TICKS(10));
                                }
                                // Abort drain if taking too long (max 2 seconds)
                                if (millis() - startDrain > 2000) {
                                    Serial.println("[ART] Drain timeout - closing connection");
                                    break;
                                }
                            }
                            Serial.printf("[ART] Drained %d bytes - waiting for HTTPS cleanup\n", (int)drained);
                            vTaskDelay(pdMS_TO_TICKS(500));  // Reduced from 2s - SSL cleanup is now explicit
                        }

                        Serial.printf("[ART] Album art read: %d bytes (len_known=%d)\n", (int)bytesRead, len_known ? 1 : 0);

                        int read = bytesRead;
                        if ((len_known ? (read == len) : (read > 0)) && readSuccess) {
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
                                    if (w == 0 || h == 0 || w > 1000 || h > 1000) {
                                        Serial.printf("[ART] Invalid PNG dimensions: %dx%d (must be 1-1000)\n", w, h);
                                        png.close();
                                        heap_caps_free(jpgBuf);
                                        jpgBuf = nullptr;
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
                            } else if (isJPEG && hw_jpeg_decoder) {
                                // ESP32-P4 Hardware JPEG Decoder - fast and stable!
                                Serial.printf("[ART] HW JPEG decode: %d bytes\n", read);

                                // Get image dimensions from header (no hardware needed)
                                jpeg_decode_picture_info_t pic_info;
                                esp_err_t ret = jpeg_decoder_get_info(jpgBuf, read, &pic_info);
                                if (ret == ESP_OK) {
                                    int w = pic_info.width;
                                    int h = pic_info.height;
                                    // Hardware outputs dimensions rounded to 16-pixel boundary
                                    int out_w = ((w + 15) / 16) * 16;
                                    int out_h = ((h + 15) / 16) * 16;
                                    Serial.printf("[ART] JPEG: %dx%d (output: %dx%d)\n", w, h, out_w, out_h);

                                    // Allocate output buffer for RGB565 - needs to be DMA capable
                                    size_t decoded_size = out_w * out_h * 2;
                                    jpeg_decode_memory_alloc_cfg_t rx_mem_cfg = {
                                        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
                                    };
                                    size_t rx_buffer_size = 0;
                                    uint8_t* hw_out_buf = (uint8_t*)jpeg_alloc_decoder_mem(decoded_size, &rx_mem_cfg, &rx_buffer_size);

                                    if (hw_out_buf) {
                                        // Configure hardware decoder for RGB565 output
                                        jpeg_decode_cfg_t decode_cfg = {
                                            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                                            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,  // Little endian
                                            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
                                        };

                                        uint32_t out_size = 0;
                                        ret = jpeg_decoder_process(hw_jpeg_decoder, &decode_cfg, jpgBuf, read, hw_out_buf, rx_buffer_size, &out_size);

                                        if (ret == ESP_OK) {
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
                                        } else {
                                            Serial.printf("[ART] HW JPEG decode failed: %d\n", ret);
                                            heap_caps_free(hw_out_buf);
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
                            } else {
                                Serial.println("[ART] Unknown image format (not JPEG or PNG)");
                            }
                        }
                        heap_caps_free(jpgBuf);
                    } else {
                        Serial.printf("[ART] Failed to allocate %d bytes for album art\n", len);
                    }
                } else if (len >= (int)max_art_size) {
                    Serial.printf("[ART] Album art too large: %d bytes (max %dKB)\n", len, (int)(max_art_size/1000));
                    // Must drain the connection to prevent WiFi RX buffer overflow
                    // Server is already sending data even though we're rejecting it
                    WiFiClient* stream = http.getStreamPtr();
                    uint8_t drainBuf[512];
                    int drained = 0;
                    unsigned long startDrain = millis();
                    while (stream->connected() && drained < len) {
                        size_t available = stream->available();
                        if (available > 0) {
                            size_t toRead = min((size_t)512, available);
                            toRead = min(toRead, (size_t)(len - drained));
                            size_t read = stream->readBytes(drainBuf, toRead);
                            drained += read;
                        } else {
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                        // Abort drain if taking too long (max 3 seconds)
                        if (millis() - startDrain > 3000) {
                            Serial.println("[ART] Drain timeout - closing connection");
                            break;
                        }
                    }
                    Serial.printf("[ART] Drained %d/%d bytes from connection\n", drained, len);
                    // Mark as done to prevent retry loop
                    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                        last_art_url = url;
                        xSemaphoreGive(art_mutex);
                    }
                } else {
                    Serial.printf("[ART] Invalid album art size: %d bytes\n", len);
                }
            } else {
                Serial.printf("[ART] HTTP error %d fetching album art\n", code);
            }
            http.end();

            // Release network_mutex after entire download completes
            xSemaphoreGive(network_mutex);

            // Wait for WiFi buffers to stabilize after download
            // Reduced from 1000ms to 500ms - mutex + SSL cleanup handle most stability issues
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Check for new URLs
    }
}

// URL encode helper for proxying HTTPS URLs through Sonos
String urlEncode(const char* url) {
    String encoded = "";
    char c;
    char code[4];
    for (int i = 0; url[i]; i++) {
        c = url[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ':' || c == '/') {
            encoded += c;
        } else {
            snprintf(code, sizeof(code), "%%%02X", (unsigned char)c);
            encoded += code;
        }
    }
    return encoded;
}

void requestAlbumArt(const String& url) {
    if (url.length() == 0) return;
    if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(10))) {
        pending_art_url = url;
        xSemaphoreGive(art_mutex);
    }
}
