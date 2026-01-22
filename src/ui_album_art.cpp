/**
 * UI Album Art Handling
 * Album art loading with JPEGDEC + bilinear scaling for arbitrary sizes
 */

#include "ui_common.h"

// Album Art Functions
static uint32_t color_r_sum = 0, color_g_sum = 0, color_b_sum = 0;
static int color_sample_count = 0;
static int jpeg_image_width = 0;  // Store full image width for callback

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

// JPEGDEC callback - decode to temporary buffer with any source dimensions
static int jpegDraw(JPEGDRAW* pDraw) {
    if (!art_temp_buffer) return 0;

    uint16_t* src = pDraw->pPixels;
    int src_x = pDraw->x;
    int src_y = pDraw->y;
    int w = pDraw->iWidth;
    int h = pDraw->iHeight;

    // Copy MCU block to temp buffer using full image width
    for (int row = 0; row < h; row++) {
        int dy = src_y + row;
        if (dy < 0 || dy >= jpeg_image_width) continue;

        memcpy(&art_temp_buffer[dy * jpeg_image_width + src_x], &src[row * w], w * 2);
    }

    return 1;
}

void albumArtTask(void* param) {
    art_buffer = (uint16_t*)heap_caps_malloc(ART_SIZE * ART_SIZE * 2, MALLOC_CAP_SPIRAM);
    art_temp_buffer = (uint16_t*)heap_caps_malloc(ART_SIZE * ART_SIZE * 2, MALLOC_CAP_SPIRAM);
    if (!art_buffer || !art_temp_buffer) { vTaskDelete(NULL); return; }

    // HTTPClient for album art
    HTTPClient http;
    static char url[512];

    // Temporary buffer for decoded full-size image
    uint16_t* decoded_buffer = nullptr;

    while (1) {
        url[0] = '\0';  // Clear URL
        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(10))) {
            if (pending_art_url.length() > 0 && pending_art_url != last_art_url) {
                String fetchUrl = pending_art_url;

                // Decode HTML entities (&amp; -> &)
                fetchUrl.replace("&amp;", "&");

                // Convert HTTPS to HTTP (ESP32-P4 doesn't support HTTPS natively)
                if (fetchUrl.startsWith("https://")) {
                    fetchUrl.replace("https://", "http://");
                }

                // Sonos Radio fix: extract high-quality art from embedded mark parameter
                is_sonos_radio_art = false;
                if (fetchUrl.indexOf("sonosradio.imgix.net") != -1 && fetchUrl.indexOf("mark=http") != -1) {
                    ESP_LOGI("ART", "Sonos Radio art detected");
                    int markStart = fetchUrl.indexOf("mark=http") + 5;
                    int markEnd = fetchUrl.indexOf("&", markStart);
                    if (markEnd == -1) markEnd = fetchUrl.length();

                    String originalUrl = fetchUrl;
                    fetchUrl = fetchUrl.substring(markStart, markEnd);
                    is_sonos_radio_art = true;
                    ESP_LOGI("ART", "Extracted: %s", fetchUrl.c_str());
                }

                strncpy(url, fetchUrl.c_str(), sizeof(url) - 1);
                url[sizeof(url) - 1] = '\0';
            }
            xSemaphoreGive(art_mutex);
        }
        if (url[0] != '\0') {
            http.begin(url);
            http.setTimeout(10000);  // Increased timeout for chunked reading
            int code = http.GET();
            if (code == 200) {
                int len = http.getSize();
                // Reduced from 400KB to 200KB to avoid WiFi buffer exhaustion (Issue #7)
                if (len > 0 && len < 200000) {
                    ESP_LOGI("ART", "Downloading album art: %d bytes", len);
                    uint8_t* jpgBuf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
                    if (jpgBuf) {
                        WiFiClient* stream = http.getStreamPtr();

                        // Chunked reading to avoid WiFi buffer exhaustion (Issue #7)
                        // Read in 4KB chunks with yields to let WiFi driver process
                        const size_t chunkSize = 4096;
                        size_t bytesRead = 0;
                        bool readSuccess = true;

                        while (bytesRead < len && stream->connected()) {
                            size_t toRead = min(chunkSize, len - bytesRead);
                            size_t actualRead = stream->readBytes(jpgBuf + bytesRead, toRead);

                            if (actualRead == 0) {
                                ESP_LOGW("ART", "Read timeout at %d/%d bytes", bytesRead, len);
                                readSuccess = false;
                                break;
                            }

                            bytesRead += actualRead;
                            vTaskDelay(pdMS_TO_TICKS(1));  // Yield to WiFi task
                        }

                        int read = bytesRead;
                        if (read == len && readSuccess) {
                            if (jpeg.openRAM(jpgBuf, len, jpegDraw)) {
                                jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
                                int w = jpeg.getWidth();
                                int h = jpeg.getHeight();
                                jpeg_image_width = w;  // Store for callback
                                ESP_LOGI("ART", "JPEG: %dx%d", w, h);

                                // Allocate buffer for full decoded image
                                size_t decoded_size = w * h * 2;
                                if (decoded_buffer) {
                                    heap_caps_free(decoded_buffer);
                                }
                                decoded_buffer = (uint16_t*)heap_caps_malloc(decoded_size, MALLOC_CAP_SPIRAM);

                                if (decoded_buffer) {
                                    // Use decoded_buffer as temp buffer for full image
                                    art_temp_buffer = decoded_buffer;

                                    // Decode full image at original size (no scaling)
                                    jpeg.decode(0, 0, 0);
                                    jpeg.close();

                                    ESP_LOGI("ART", "Decoded %dx%d", w, h);

                                    // Restore art_temp_buffer pointer
                                    art_temp_buffer = (uint16_t*)heap_caps_malloc(ART_SIZE * ART_SIZE * 2, MALLOC_CAP_SPIRAM);

                                    if (art_temp_buffer) {
                                        // Clear output buffer
                                        memset(art_temp_buffer, 0, ART_SIZE * ART_SIZE * 2);

                                        // Scale to exact 420x420 using bilinear interpolation
                                        ESP_LOGI("ART", "Bilinear scaling %dx%d -> 420x420", w, h);
                                        scaleImageBilinear(decoded_buffer, w, h, art_temp_buffer, ART_SIZE, ART_SIZE);
                                        ESP_LOGI("ART", "Scaling complete");

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
                                    ESP_LOGW("ART", "Failed to allocate %d bytes for decoded image", decoded_size);
                                }
                            }
                        }
                        heap_caps_free(jpgBuf);
                    } else {
                        ESP_LOGW("ART", "Failed to allocate %d bytes for album art", len);
                    }
                } else if (len >= 200000) {
                    ESP_LOGW("ART", "Album art too large: %d bytes (max 200KB)", len);
                } else {
                    ESP_LOGW("ART", "Invalid album art size: %d bytes", len);
                }
            } else {
                ESP_LOGW("ART", "HTTP error %d fetching album art", code);
            }
            http.end();
        }
        vTaskDelay(pdMS_TO_TICKS(200));  // Reduced from 500ms for faster art updates
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
