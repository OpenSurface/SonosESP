/**
 * UI Album Art Handling
 * Album art loading, JPEG decoding, and dominant color extraction
 */

#include "ui_common.h"

// Album Art Functions
static uint32_t color_r_sum = 0, color_g_sum = 0, color_b_sum = 0;
static int color_sample_count = 0;

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

// Callback - write JPEG MCUs to temp buffer with centering offset
static int jpegDraw(JPEGDRAW* pDraw) {
    if (!art_temp_buffer) return 0;

    uint16_t* src = pDraw->pPixels;
    int src_x = pDraw->x;
    int src_y = pDraw->y;
    int w = pDraw->iWidth;
    int h = pDraw->iHeight;

    // Calculate destination with offset (for centering/cropping)
    int dest_x = src_x + art_offset_x;
    int dest_y = src_y + art_offset_y;

    // Copy pixels with bounds checking (clips large images, centers small ones)
    for (int row = 0; row < h; row++) {
        int dy = dest_y + row;
        if (dy < 0 || dy >= ART_SIZE) continue;

        for (int col = 0; col < w; col++) {
            int dx = dest_x + col;
            if (dx < 0 || dx >= ART_SIZE) continue;

            uint16_t pixel = src[row * w + col];
            art_temp_buffer[dy * ART_SIZE + dx] = pixel;

            // Sample edge pixels for dominant color extraction
            if (((dx | dy) % 15 == 0) && (dy < 50 || dy > ART_SIZE - 50 || dx < 50 || dx > ART_SIZE - 50)) {
                color_r_sum += ((pixel >> 8) & 0xF8);
                color_g_sum += ((pixel >> 3) & 0xFC);
                color_b_sum += ((pixel << 3) & 0xF8);
                color_sample_count++;
            }
        }
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
                    int markStart = fetchUrl.indexOf("mark=http") + 5;
                    int markEnd = fetchUrl.indexOf("&", markStart);
                    if (markEnd == -1) markEnd = fetchUrl.length();

                    fetchUrl = fetchUrl.substring(markStart, markEnd);
                    is_sonos_radio_art = true;
                }

                strncpy(url, fetchUrl.c_str(), sizeof(url) - 1);
                url[sizeof(url) - 1] = '\0';
            }
            xSemaphoreGive(art_mutex);
        }
        if (url[0] != '\0') {
            http.begin(url);
            http.setTimeout(5000);
            int code = http.GET();
            if (code == 200) {
                int len = http.getSize();
                if (len > 0 && len < 400000) {
                    uint8_t* jpgBuf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
                    if (jpgBuf) {
                        WiFiClient* stream = http.getStreamPtr();
                        int read = stream->readBytes(jpgBuf, len);
                        if (read == len) {
                            if (jpeg.openRAM(jpgBuf, len, jpegDraw)) {
                                jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
                                int w = jpeg.getWidth();
                                int h = jpeg.getHeight();

                                memset(art_temp_buffer, 0, ART_SIZE * ART_SIZE * 2);

                                // Reset dominant color sampling
                                color_r_sum = 0;
                                color_g_sum = 0;
                                color_b_sum = 0;
                                color_sample_count = 0;

                                // Smart scaling: decode at optimal size for 420x420 display
                                int scale_option = 0;
                                int max_dim = (w > h) ? w : h;

                                if (max_dim > 1680) {
                                    scale_option = 8;
                                    w /= 8;
                                    h /= 8;
                                } else if (max_dim > 840) {
                                    // Sonos Radio: use half-scale (500x500) for better quality
                                    scale_option = is_sonos_radio_art ? 2 : 4;
                                    w /= is_sonos_radio_art ? 2 : 4;
                                    h /= is_sonos_radio_art ? 2 : 4;
                                }

                                // Calculate offsets for centering (negative = crop edges)
                                art_offset_x = (420 - w) / 2;
                                art_offset_y = (420 - h) / 2;

                                // Decode and render
                                jpeg.decode(0, 0, scale_option);
                                jpeg.close();

                                // Copy completed image from temp to display buffer atomically
                                memcpy(art_buffer, art_temp_buffer, ART_SIZE * ART_SIZE * 2);

                                art_dsc.header.w = ART_SIZE;
                                art_dsc.header.h = ART_SIZE;
                                art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
                                art_dsc.data_size = ART_SIZE * ART_SIZE * 2;
                                art_dsc.data = (const uint8_t*)art_buffer;

                                // Calculate dominant color from sampled pixels
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

                                // Update all shared variables atomically under mutex
                                if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(100))) {
                                    last_art_url = url;
                                    dominant_color = new_color;
                                    art_ready = true;
                                    color_ready = true;
                                    xSemaphoreGive(art_mutex);
                                }
                            }
                        }
                        heap_caps_free(jpgBuf);
                    }
                }
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
