/**
 * ESP32-S3 Sonos Controller
 * 800x480 RGB Display with Touch
 * Modern UI matching reference design
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "lvgl.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "sonos_controller.h"
#include "esp_heap_caps.h"

// Default WiFi credentials (empty = force WiFi setup via UI)
#define DEFAULT_WIFI_SSID     ""
#define DEFAULT_WIFI_PASSWORD ""

// Firmware version
#define FIRMWARE_VERSION "1.0.13"
#define GITHUB_REPO "OpenSurface/SonosESP"
#define GITHUB_API_URL "https://api.github.com/repos/" GITHUB_REPO "/releases/latest"

// Sonos logo
LV_IMG_DECLARE(Sonos_idnu60bqes_1);

// Fixed Color Theme (Professional Default)
static lv_color_t COL_BG = lv_color_hex(0x1A1A1A);
static lv_color_t COL_CARD = lv_color_hex(0x2A2A2A);
static lv_color_t COL_BTN = lv_color_hex(0x3A3A3A);
static lv_color_t COL_BTN_PRESSED = lv_color_hex(0x4A4A4A);
static lv_color_t COL_TEXT = lv_color_hex(0xFFFFFF);
static lv_color_t COL_TEXT2 = lv_color_hex(0x888888);
static lv_color_t COL_ACCENT = lv_color_hex(0xD4A84B);
static lv_color_t COL_HEART = lv_color_hex(0xE85D5D);
static lv_color_t COL_SELECTED = lv_color_hex(0x333333);

// Forward declarations for panels and controls (defined later in globals)
static lv_obj_t *panel_right;
static lv_obj_t *panel_art;
static lv_obj_t *slider_progress;

// Forward declaration - defined after all globals
void setBackgroundColor(uint32_t hex_color);

// Globals
SonosController sonos;
JPEGDEC jpeg;
Preferences wifiPrefs;

// Display brightness settings
static int brightness_level = 100;        // 0-100% for ESP32-P4
static int brightness_dimmed = 20;        // Dimmed brightness level (percentage)
static int autodim_timeout = 30;          // Seconds before dimming (0 = disabled)
static uint32_t last_touch_time = 0;
static bool screen_dimmed = false;

static lv_obj_t *scr_main, *scr_devices, *scr_queue, *scr_settings, *scr_wifi, *scr_sources, *scr_browse, *scr_display, *scr_ota;
static lv_obj_t *img_album, *lbl_title, *lbl_artist, *lbl_album, *lbl_time, *lbl_time_remaining;
static lv_obj_t *btn_play, *btn_prev, *btn_next, *btn_mute, *btn_shuffle, *btn_repeat, *btn_queue;  // slider_progress declared above
static lv_obj_t *img_next_album, *lbl_next_title, *lbl_next_artist, *lbl_next_header;
static lv_obj_t *lbl_wifi_icon, *lbl_device_name, *slider_vol;
static lv_obj_t *list_devices, *list_queue, *lbl_status, *lbl_queue_status;
static lv_obj_t *art_placeholder, *list_wifi, *lbl_wifi_status, *ta_password, *kb, *btn_wifi_scan, *btn_wifi_connect, *lbl_scan_text, *btn_sonos_scan;
// panel_right, panel_art, and slider_progress already declared above (before color function)

#define ART_SIZE 420
static lv_img_dsc_t art_dsc;
static uint16_t* art_buffer = NULL;
static uint16_t* art_temp_buffer = NULL;  // Temporary buffer for decoding
static String last_art_url = "", pending_art_url = "";
static volatile bool art_ready = false;
static SemaphoreHandle_t art_mutex = NULL;
static uint32_t dominant_color = 0x1a1a1a;
static volatile bool color_ready = false;

// Forward declarations
String urlEncode(const char* url);

static String ui_title, ui_artist, ui_repeat;
static int ui_vol = -1;
static bool ui_playing = false, ui_shuffle = false, ui_muted = false;
static bool dragging_vol = false, dragging_prog = false;
static String selectedSSID = "";
static int kb_mode = 0;
static String wifiNetworks[20];
static int wifiNetworkCount = 0;
static int art_offset_x = 0, art_offset_y = 0;
static bool is_sonos_radio_art = false;  // Flag for Sonos Radio art quality optimization
static String current_browse_id = "";
static String current_browse_title = "";

// Built-in LVGL keyboard - no custom maps needed

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

// Forward declarations
static void refreshQueueList();
static void refreshDeviceList();

// Event handlers
static void ev_play(lv_event_t* e) { SonosDevice* d = sonos.getCurrentDevice(); if (d) d->isPlaying ? sonos.pause() : sonos.play(); }
static void ev_prev(lv_event_t* e) { sonos.previous(); }
static void ev_next(lv_event_t* e) { sonos.next(); }
static void ev_shuffle(lv_event_t* e) { SonosDevice* d = sonos.getCurrentDevice(); if (d) sonos.setShuffle(!d->shuffleMode); }
static void ev_repeat(lv_event_t* e) {
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) return;
    if (d->repeatMode == "NONE") sonos.setRepeat("ALL");
    else if (d->repeatMode == "ALL") sonos.setRepeat("ONE");
    else sonos.setRepeat("NONE");
}
static void ev_sources(lv_event_t* e) {
    Serial.println("[UI] Music sources pressed");
    lv_screen_load(scr_sources);
}
static void ev_progress(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSING) dragging_prog = true;
    else if (code == LV_EVENT_RELEASED) {
        SonosDevice* d = sonos.getCurrentDevice();
        if (d && d->durationSeconds > 0) sonos.seek((lv_slider_get_value(slider_progress) * d->durationSeconds) / 100);
        dragging_prog = false;
    }
}

static void ev_vol_slider(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSING) dragging_vol = true;
    else if (code == LV_EVENT_RELEASED) {
        sonos.setVolume(lv_slider_get_value(slider_vol));
        dragging_vol = false;
    }
}

static void ev_mute(lv_event_t* e) {
    SonosDevice* d = sonos.getCurrentDevice();
    if (d) sonos.setMute(!d->isMuted);
}
// Brightness control functions
void setBrightness(int level) {
    brightness_level = constrain(level, 10, 100);  // 10-100% range
    display_set_brightness(brightness_level);
    wifiPrefs.putInt("brightness", brightness_level);
    Serial.printf("[DISPLAY] Brightness set to: %d%%\n", brightness_level);
}

void resetScreenTimeout() {
    last_touch_time = millis();
    if (screen_dimmed) {
        // Instant wake-up - no animation
        display_set_brightness(brightness_level);
        screen_dimmed = false;
        Serial.printf("[DISPLAY] Screen woken up - restored brightness to %d%%\n", brightness_level);
    }
}

// Brightness animation callback for smooth dimming
static void brightness_anim_cb(void* var, int32_t v) {
    display_set_brightness(v);
}

void checkAutoDim() {
    if (autodim_timeout == 0) return;  // Auto-dim disabled
    if (screen_dimmed) return;  // Already dimmed

    if ((millis() - last_touch_time) > (autodim_timeout * 1000)) {
        int dimmed = constrain(brightness_dimmed, 5, 100);

        // Smooth fade to dimmed brightness (1 second fade)
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, NULL);
        lv_anim_set_values(&anim, brightness_level, dimmed);
        lv_anim_set_duration(&anim, 1000);  // 1 second smooth fade
        lv_anim_set_exec_cb(&anim, brightness_anim_cb);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
        lv_anim_start(&anim);

        screen_dimmed = true;
        Serial.printf("[DISPLAY] Screen dimming smoothly to %d%%\n", dimmed);
    }
}

static void ev_devices(lv_event_t* e) { lv_screen_load(scr_devices); }
static void ev_queue(lv_event_t* e) { sonos.updateQueue(); refreshQueueList(); lv_screen_load(scr_queue); }
static void ev_settings(lv_event_t* e) { lv_screen_load(scr_settings); }
static void ev_display_settings(lv_event_t* e) { lv_screen_load(scr_display); }
static void ev_back_main(lv_event_t* e) { lv_screen_load(scr_main); }
static void ev_back_settings(lv_event_t* e) { lv_screen_load(scr_settings); }

void createBrowseScreen();
static void ev_discover(lv_event_t* e) {
    // Disable scan button during discovery
    if (btn_sonos_scan) {
        lv_obj_add_state(btn_sonos_scan, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_sonos_scan, lv_color_hex(0x555555), LV_STATE_DISABLED);
    }

    lv_label_set_text(lbl_status, LV_SYMBOL_REFRESH " Discovering Sonos devices...");
    lv_obj_set_style_text_color(lbl_status, COL_ACCENT, 0);
    lv_obj_clean(list_devices);
    lv_timer_handler();  // Update UI immediately

    int cnt = sonos.discoverDevices();

    // Re-enable scan button
    if (btn_sonos_scan) {
        lv_obj_clear_state(btn_sonos_scan, LV_STATE_DISABLED);
    }

    if (cnt == 0) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WARNING " No Sonos devices found on network");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFF6B6B), 0);
        return;
    }

    if (cnt < 0) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WARNING " Discovery failed - check network");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFF6B6B), 0);
        return;
    }

    lv_label_set_text_fmt(lbl_status, LV_SYMBOL_OK " Found %d Sonos device%s", cnt, cnt == 1 ? "" : "s");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x4ECB71), 0);
    refreshDeviceList();
}
static void ev_queue_item(lv_event_t* e) {
    int trackNum = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    sonos.playQueueItem(trackNum);
    lv_screen_load(scr_main);
}

static void refreshDeviceList() {
    lv_obj_clean(list_devices);
    int cnt = sonos.getDeviceCount();
    SonosDevice* current = sonos.getCurrentDevice();
    for (int i = 0; i < cnt; i++) {
        SonosDevice* dev = sonos.getDevice(i);
        if (!dev) continue;

        lv_obj_t* btn = lv_btn_create(list_devices);
        lv_obj_set_size(btn, 720, 60);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 15, 0);

        bool isSelected = (current && dev->ip == current->ip);
        lv_obj_set_style_bg_color(btn, isSelected ? COL_SELECTED : COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);

        if (isSelected) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, COL_ACCENT, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, isSelected ? COL_ACCENT : COL_TEXT2, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, dev->roomName.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 40, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            sonos.selectDevice(idx);
            sonos.startTasks();
            lv_screen_load(scr_main);
        }, LV_EVENT_CLICKED, NULL);
    }
}

static void refreshQueueList() {
    lv_obj_clean(list_queue);
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) { lv_label_set_text(lbl_queue_status, "No device"); return; }
    if (d->queueSize == 0) { lv_label_set_text(lbl_queue_status, "Queue is empty"); return; }
    lv_label_set_text_fmt(lbl_queue_status, "%d %s in queue", d->queueSize, d->queueSize == 1 ? "track" : "tracks");

    for (int i = 0; i < d->queueSize; i++) {
        QueueItem* item = &d->queue[i];
        int trackNum = i + 1;
        bool isPlaying = (trackNum == d->currentTrackNumber);

        lv_obj_t* btn = lv_btn_create(list_queue);
        lv_obj_set_size(btn, 727, 60);  // Full width, uniform height
        lv_obj_set_style_bg_color(btn, isPlaying ? lv_color_hex(0x252525) : lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 0, 0);  // No rounded corners - clean list
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 12, 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)trackNum);
        lv_obj_add_event_cb(btn, ev_queue_item, LV_EVENT_CLICKED, NULL);

        // Subtle left border for currently playing
        if (isPlaying) {
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_width(btn, 3, 0);
            lv_obj_set_style_border_color(btn, COL_ACCENT, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        // Play icon for currently playing track OR track number
        lv_obj_t* num = lv_label_create(btn);
        if (isPlaying) {
            lv_label_set_text(num, LV_SYMBOL_PLAY);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_18, 0);
        } else {
            lv_label_set_text_fmt(num, "%d", trackNum);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_14, 0);
        }
        lv_obj_set_style_text_color(num, isPlaying ? COL_ACCENT : COL_TEXT2, 0);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 5, 0);

        // Title - highlight when playing
        lv_obj_t* title = lv_label_create(btn);
        lv_label_set_text(title, item->title.c_str());
        lv_obj_set_style_text_color(title, isPlaying ? COL_ACCENT : COL_TEXT, 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_width(title, 610);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 45, -11);

        // Artist - subtle gray
        lv_obj_t* artist = lv_label_create(btn);
        lv_label_set_text(artist, item->artist.c_str());
        lv_obj_set_style_text_color(artist, COL_TEXT2, 0);
        lv_obj_set_style_text_font(artist, &lv_font_montserrat_12, 0);
        lv_obj_set_width(artist, 610);
        lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
        lv_obj_align(artist, LV_ALIGN_LEFT_MID, 45, 11);
    }
}

// WiFi functions
static void ev_wifi_scan(lv_event_t* e) {
    // Disable button and show loading state
    if (btn_wifi_scan) {
        lv_obj_add_state(btn_wifi_scan, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_wifi_scan, lv_color_hex(0x555555), LV_STATE_DISABLED);
    }
    if (lbl_scan_text) {
        lv_label_set_text(lbl_scan_text, LV_SYMBOL_REFRESH "  Scanning...");
    }

    lv_label_set_text(lbl_wifi_status, LV_SYMBOL_REFRESH " Scanning for networks...");
    lv_obj_set_style_text_color(lbl_wifi_status, COL_ACCENT, 0);
    lv_obj_clean(list_wifi);
    lv_timer_handler();  // Update UI immediately

    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    int n = WiFi.scanNetworks();
    wifiNetworkCount = min(n, 20);

    // Re-enable button
    if (btn_wifi_scan) {
        lv_obj_clear_state(btn_wifi_scan, LV_STATE_DISABLED);
    }
    if (lbl_scan_text) {
        lv_label_set_text(lbl_scan_text, LV_SYMBOL_REFRESH "  Scan");
    }

    if (n == 0) {
        lv_label_set_text(lbl_wifi_status, LV_SYMBOL_WARNING " No networks found");
        lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0xFF6B6B), 0);
        return;
    }

    if (n < 0) {
        lv_label_set_text(lbl_wifi_status, LV_SYMBOL_WARNING " Scan failed - try again");
        lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0xFF6B6B), 0);
        return;
    }

    lv_label_set_text_fmt(lbl_wifi_status, LV_SYMBOL_OK " Found %d network%s", n, n == 1 ? "" : "s");
    lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x4ECB71), 0);

    for (int i = 0; i < wifiNetworkCount; i++) {
        wifiNetworks[i] = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);

        lv_obj_t* btn = lv_btn_create(list_wifi);
        lv_obj_set_size(btn, 340, 50);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            selectedSSID = wifiNetworks[idx];
            lv_label_set_text_fmt(lbl_wifi_status, LV_SYMBOL_WIFI " Selected: %s", selectedSSID.c_str());
            lv_obj_set_style_text_color(lbl_wifi_status, COL_TEXT, 0);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, NULL);

        lv_obj_t* icon = lv_label_create(btn);
        // Signal strength icon
        if (rssi > -50) lv_label_set_text(icon, LV_SYMBOL_WIFI);
        else if (rssi > -70) lv_label_set_text(icon, LV_SYMBOL_WIFI);
        else lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t* ssid = lv_label_create(btn);
        lv_label_set_text(ssid, wifiNetworks[i].c_str());
        lv_obj_set_style_text_color(ssid, COL_TEXT, 0);
        lv_obj_set_width(ssid, 260);
        lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
        lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 40, 0);
    }
    WiFi.scanDelete();
}

static void ev_wifi_connect(lv_event_t* e) {
    if (selectedSSID.length() == 0) {
        lv_label_set_text(lbl_wifi_status, LV_SYMBOL_WARNING " Please select a network first");
        lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0xFF6B6B), 0);
        return;
    }

    const char* pwd = lv_textarea_get_text(ta_password);

    // Disable connect button during connection
    if (btn_wifi_connect) {
        lv_obj_add_state(btn_wifi_connect, LV_STATE_DISABLED);
    }

    lv_label_set_text_fmt(lbl_wifi_status, LV_SYMBOL_REFRESH " Connecting to %s...", selectedSSID.c_str());
    lv_obj_set_style_text_color(lbl_wifi_status, COL_ACCENT, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_timer_handler();  // Update UI

    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.begin(selectedSSID.c_str(), pwd);

    // Non-blocking connection with visual feedback (max 15 seconds)
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        lv_timer_handler();  // Keep UI responsive
        lv_label_set_text_fmt(lbl_wifi_status, LV_SYMBOL_REFRESH " Connecting to %s%s",
            selectedSSID.c_str(),
            tries % 4 == 0 ? "..." : tries % 4 == 1 ? ".  " : tries % 4 == 2 ? ".. " : " ..");
    }

    // Re-enable button
    if (btn_wifi_connect) {
        lv_obj_clear_state(btn_wifi_connect, LV_STATE_DISABLED);
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiPrefs.putString("ssid", selectedSSID);
        wifiPrefs.putString("pass", pwd);

        String ip = WiFi.localIP().toString();
        lv_label_set_text_fmt(lbl_wifi_status,
            LV_SYMBOL_OK " Connected!\n"
            LV_SYMBOL_WIFI " Network: %s\n"
            LV_SYMBOL_SETTINGS " IP: %s",
            selectedSSID.c_str(), ip.c_str());
        lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x4ECB71), 0);

        // Clear password field for security
        lv_textarea_set_text(ta_password, "");
    } else {
        // Determine failure reason
        wl_status_t status = WiFi.status();
        const char* reason = "Unknown error";

        if (status == WL_CONNECT_FAILED) {
            reason = "Authentication failed - check password";
        } else if (status == WL_NO_SSID_AVAIL) {
            reason = "Network not found";
        } else if (status == WL_CONNECTION_LOST) {
            reason = "Connection lost";
        } else if (status == WL_DISCONNECTED) {
            reason = "Connection timeout - check password";
        }

        lv_label_set_text_fmt(lbl_wifi_status, LV_SYMBOL_WARNING " Failed: %s", reason);
        lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0xFF6B6B), 0);
    }
}

static void ev_theme_select(lv_event_t* e);

// ==================== OTA UPDATE FUNCTIONS ====================
static lv_obj_t* lbl_ota_status = nullptr;
static lv_obj_t* lbl_ota_progress = nullptr;
static lv_obj_t* lbl_current_version = nullptr;
static lv_obj_t* lbl_latest_version = nullptr;
static lv_obj_t* btn_check_update = nullptr;
static lv_obj_t* btn_install_update = nullptr;
static lv_obj_t* bar_ota_progress = nullptr;  // Visual progress bar
static String latest_version = "";
static String download_url = "";

// Check for updates from GitHub releases
static void checkForUpdates() {
    if (WiFi.status() != WL_CONNECTED) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No WiFi connection");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        return;
    }

    // Disable check button during check
    if (btn_check_update) lv_obj_add_state(btn_check_update, LV_STATE_DISABLED);

    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, LV_SYMBOL_REFRESH " Checking for updates...");
        lv_obj_set_style_text_color(lbl_ota_status, COL_ACCENT, 0);
    }
    lv_timer_handler();

    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation

    HTTPClient http;
    http.begin(client, "https://api.github.com/repos/" GITHUB_REPO "/releases/latest");
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.setTimeout(15000);

    int httpCode = http.GET();

    if (btn_check_update) lv_obj_clear_state(btn_check_update, LV_STATE_DISABLED);

    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            latest_version = doc["tag_name"].as<String>();
            latest_version.replace("v", "");  // Remove 'v' prefix

            if (lbl_latest_version) {
                lv_label_set_text_fmt(lbl_latest_version, "Latest: v%s", latest_version.c_str());
            }

            // Find firmware.bin asset
            JsonArray assets = doc["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();
                if (name.indexOf("firmware.bin") >= 0) {
                    download_url = asset["browser_download_url"].as<String>();
                    // Use HTTPS directly - ESP32-P4 supports it with WiFiClientSecure
                    break;
                }
            }

            // Compare versions
            if (latest_version != FIRMWARE_VERSION) {
                if (lbl_ota_status) {
                    lv_label_set_text_fmt(lbl_ota_status, LV_SYMBOL_DOWNLOAD " Update available: v%s", latest_version.c_str());
                    lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0x4ECB71), 0);
                }
                if (btn_install_update) {
                    lv_obj_clear_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, LV_SYMBOL_OK " You're on the latest version!");
                    lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0x4ECB71), 0);
                }
                if (btn_install_update) {
                    lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
                }
            }
        } else {
            if (lbl_ota_status) {
                lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Failed to parse response");
                lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
            }
        }
    } else {
        if (lbl_ota_status) {
            lv_label_set_text_fmt(lbl_ota_status, LV_SYMBOL_WARNING " Check failed (HTTP %d)", httpCode);
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
    }

    http.end();
}

// Perform OTA update
static void performOTAUpdate() {
    if (download_url.length() == 0) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No update URL found");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        return;
    }

    // Disable buttons during update
    if (btn_check_update) lv_obj_add_state(btn_check_update, LV_STATE_DISABLED);
    if (btn_install_update) lv_obj_add_state(btn_install_update, LV_STATE_DISABLED);

    // Show and reset progress bar
    if (bar_ota_progress) {
        lv_obj_clear_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
    }

    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, LV_SYMBOL_DOWNLOAD " Connecting to server...");
        lv_obj_set_style_text_color(lbl_ota_status, COL_ACCENT, 0);
    }
    if (lbl_ota_progress) {
        lv_label_set_text(lbl_ota_progress, "0%");
    }

    // Force immediate display refresh to show progress bar
    lv_tick_inc(10);
    lv_refr_now(NULL);  // Force immediate refresh
    vTaskDelay(pdMS_TO_TICKS(200));

    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation

    HTTPClient http;
    http.begin(client, download_url);
    http.setTimeout(60000);  // 60 second timeout for large files
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = http.GET();

    if (httpCode == 200) {
        int contentLength = http.getSize();
        bool canBegin = Update.begin(contentLength);

        if (canBegin) {
            if (lbl_ota_status) {
                lv_label_set_text(lbl_ota_status, LV_SYMBOL_DOWNLOAD " Downloading firmware...");
            }
            lv_tick_inc(10);
            lv_refr_now(NULL);

            WiFiClient* stream = http.getStreamPtr();
            size_t written = 0;
            uint8_t buff[1024];  // Larger buffer for faster transfer
            int lastPercent = -1;
            uint32_t lastUIUpdate = millis();

            while (http.connected() && (written < contentLength)) {
                size_t available = stream->available();
                if (available) {
                    size_t toRead = available < sizeof(buff) ? available : sizeof(buff);
                    int c = stream->readBytes(buff, toRead);
                    written += Update.write(buff, c);

                    int percent = (written * 100) / contentLength;
                    uint32_t now = millis();

                    // Update UI every 1000ms (1 second) to reduce flicker
                    if (now - lastUIUpdate >= 1000 && percent != lastPercent) {
                        if (lbl_ota_progress) {
                            lv_label_set_text_fmt(lbl_ota_progress, "%d%%", percent);
                        }
                        if (bar_ota_progress) {
                            lv_bar_set_value(bar_ota_progress, percent, LV_ANIM_OFF);
                        }
                        if (lbl_ota_status) {
                            lv_label_set_text(lbl_ota_status, LV_SYMBOL_DOWNLOAD " Downloading firmware...");
                        }
                        lastPercent = percent;

                        // Force display refresh
                        lv_tick_inc(now - lastUIUpdate);
                        lv_refr_now(NULL);
                        lastUIUpdate = now;
                    }
                }
                // Small yield to prevent watchdog and allow display updates
                vTaskDelay(pdMS_TO_TICKS(1));
            }

            if (written == contentLength) {
                // DOWNLOAD COMPLETE - Show 100%
                if (bar_ota_progress) {
                    lv_bar_set_value(bar_ota_progress, 100, LV_ANIM_OFF);
                }
                if (lbl_ota_progress) {
                    lv_label_set_text(lbl_ota_progress, "100%");
                }
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, LV_SYMBOL_OK " Download complete!");
                }
                lv_tick_inc(10);
                lv_refr_now(NULL);
                vTaskDelay(pdMS_TO_TICKS(500));

                // START INSTALL - Reset progress bar and animate
                if (bar_ota_progress) {
                    lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
                }
                if (lbl_ota_progress) {
                    lv_label_set_text(lbl_ota_progress, "");
                }
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, LV_SYMBOL_REFRESH " Installing & verifying...");
                }
                lv_tick_inc(10);
                lv_refr_now(NULL);

                // Animate install progress (0-100% smoothly)
                for (int i = 0; i <= 100; i += 10) {
                    if (bar_ota_progress) {
                        lv_bar_set_value(bar_ota_progress, i, LV_ANIM_OFF);
                    }
                    lv_tick_inc(50);
                    lv_refr_now(NULL);
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }

            if (Update.end()) {
                if (Update.isFinished()) {
                    // INSTALL COMPLETE
                    if (bar_ota_progress) {
                        lv_bar_set_value(bar_ota_progress, 100, LV_ANIM_OFF);
                    }
                    if (lbl_ota_progress) {
                        lv_label_set_text(lbl_ota_progress, "Done!");
                    }
                    if (lbl_ota_status) {
                        lv_label_set_text(lbl_ota_status, LV_SYMBOL_OK " Update complete! Rebooting...");
                        lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0x4ECB71), 0);
                    }
                    lv_tick_inc(10);
                    lv_refr_now(NULL);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    ESP.restart();
                } else {
                    if (lbl_ota_status) {
                        lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Update failed: Not finished");
                        lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
                    }
                }
            } else {
                if (lbl_ota_status) {
                    lv_label_set_text_fmt(lbl_ota_status, LV_SYMBOL_WARNING " Update failed: %s", Update.errorString());
                    lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
                }
            }
        } else {
            if (lbl_ota_status) {
                lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Not enough space for OTA");
                lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
            }
        }
    } else {
        if (lbl_ota_status) {
            lv_label_set_text_fmt(lbl_ota_status, LV_SYMBOL_WARNING " Download failed (HTTP %d)", httpCode);
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
    }

    http.end();

    // Hide progress bar and re-enable buttons
    if (bar_ota_progress) {
        lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_check_update) lv_obj_clear_state(btn_check_update, LV_STATE_DISABLED);
    if (btn_install_update) lv_obj_clear_state(btn_install_update, LV_STATE_DISABLED);
}

static void ev_check_update(lv_event_t* e) {
    checkForUpdates();
}

static void ev_install_update(lv_event_t* e) {
    performOTAUpdate();
}

static void ev_ota_settings(lv_event_t* e) {
    lv_screen_load(scr_ota);
}

// ==================== MAIN SCREEN - CLEAN SIMPLE DESIGN ====================
void createMainScreen() {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, COL_BG, 0);
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);
    
    // LEFT: Album Art Area (420px) - ambient color background
    panel_art = lv_obj_create(scr_main);
    lv_obj_set_size(panel_art, 420, 480);
    lv_obj_set_pos(panel_art, 0, 0);
    lv_obj_set_style_bg_color(panel_art, lv_color_hex(0x1a1a1a), 0);  // Start with dark gray
    lv_obj_set_style_radius(panel_art, 0, 0);
    lv_obj_set_style_border_width(panel_art, 0, 0);
    lv_obj_set_style_pad_all(panel_art, 0, 0);
    lv_obj_clear_flag(panel_art, LV_OBJ_FLAG_SCROLLABLE);

    // Album art image - centered (JPEG decoder handles aspect ratio centering in buffer)
    img_album = lv_img_create(panel_art);
    lv_obj_set_size(img_album, ART_SIZE, ART_SIZE);
    lv_obj_center(img_album);

    // Placeholder when no art
    art_placeholder = lv_label_create(panel_art);
    lv_label_set_text(art_placeholder, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(art_placeholder, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(art_placeholder, COL_TEXT2, 0);
    lv_obj_center(art_placeholder);

    // Album name ON the album art (at bottom, overlaid on image)
    lbl_album = lv_label_create(panel_art);
    lv_obj_set_width(lbl_album, 400);
    lv_label_set_long_mode(lbl_album, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_album, "");
    lv_obj_set_style_text_color(lbl_album, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_album, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(lbl_album, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_album, LV_ALIGN_BOTTOM_MID, 0, -5);  // Moved closer to bottom (-5 instead of -20)
    
    // RIGHT: Control Panel (380px) - use global for ambient color changes
    panel_right = lv_obj_create(scr_main);
    lv_obj_set_size(panel_right, 380, 480);
    lv_obj_set_pos(panel_right, 420, 0);
    lv_obj_set_style_bg_color(panel_right, COL_BG, 0);
    lv_obj_set_style_radius(panel_right, 0, 0);
    lv_obj_set_style_border_width(panel_right, 0, 0);
    lv_obj_set_style_pad_all(panel_right, 0, 0);
    lv_obj_clear_flag(panel_right, LV_OBJ_FLAG_SCROLLABLE);
    
    // ===== TOP ROW: Back | Now Playing - Device | WiFi Queue Settings =====
    // Setup smooth scale transition for all buttons (110% on press)
    static lv_style_transition_dsc_t trans_btn;
    static lv_style_prop_t trans_props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_PROP_INV};
    lv_style_transition_dsc_init(&trans_btn, trans_props, lv_anim_path_ease_out, 150, 0, NULL);

    // Back button - scale effect
    lv_obj_t* btn_back = lv_btn_create(panel_right);
    lv_obj_set_size(btn_back, 40, 40);
    lv_obj_set_pos(btn_back, 15, 15);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_back, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_back, &trans_btn, 0);
    lv_obj_add_event_cb(btn_back, ev_devices, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_back = lv_label_create(btn_back);
    lv_label_set_text(ico_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(ico_back, COL_TEXT, 0);
    lv_obj_center(ico_back);

    // "Now Playing - Device" label - positioned after back button
    lbl_device_name = lv_label_create(panel_right);
    lv_label_set_text(lbl_device_name, "Now Playing");
    lv_obj_set_style_text_color(lbl_device_name, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_device_name, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_device_name, 60, 25);

    // Music Sources button - scale effect
    lv_obj_t* btn_sources = lv_btn_create(panel_right);
    lv_obj_set_size(btn_sources, 36, 36);
    lv_obj_set_pos(btn_sources, 285, 18);
    lv_obj_set_style_bg_opa(btn_sources, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_sources, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_sources, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_sources, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_sources, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_sources, &trans_btn, 0);
    lv_obj_add_event_cb(btn_sources, [](lv_event_t* e) { lv_screen_load(scr_sources); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_src = lv_label_create(btn_sources);
    lv_label_set_text(ico_src, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(ico_src, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_src, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_src);

    // Settings button
    lv_obj_t* btn_settings = lv_btn_create(panel_right);
    lv_obj_set_size(btn_settings, 36, 36);
    lv_obj_set_pos(btn_settings, 335, 18);
    lv_obj_set_style_bg_opa(btn_settings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_settings, 0, 0);
    lv_obj_add_event_cb(btn_settings, ev_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_set = lv_label_create(btn_settings);
    lv_label_set_text(ico_set, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(ico_set, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_set, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_set);

    // ===== TRACK INFO =====
    // Artist (gray, smaller)
    lbl_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_artist, 20, 75);
    lv_obj_set_width(lbl_artist, 300);
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_color(lbl_artist, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_artist, &lv_font_montserrat_16, 0);

    // Title (white, large)
    lbl_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_title, 20, 100);
    lv_obj_set_width(lbl_title, 300);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_title, "Not Playing");
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_28, 0);

    // Queue/Playlist button (on same line as track info) - scale effect
    btn_queue = lv_btn_create(panel_right);
    lv_obj_set_size(btn_queue, 48, 48);  // Increased from 35x35 for better touch target
    lv_obj_set_pos(btn_queue, 323, 88);  // Adjusted position
    lv_obj_set_style_bg_opa(btn_queue, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_queue, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_queue, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_queue, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_queue, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_queue, &trans_btn, 0);
    lv_obj_set_ext_click_area(btn_queue, 8);  // Add 8px clickable area around button
    lv_obj_add_event_cb(btn_queue, ev_queue, LV_EVENT_CLICKED, NULL);  // Go to Queue/Playlist
    lv_obj_t* ico_fav = lv_label_create(btn_queue);
    lv_label_set_text(ico_fav, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(ico_fav, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_fav, &lv_font_montserrat_18, 0);
    lv_obj_center(ico_fav);
    
    // ===== PROGRESS BAR =====
    slider_progress = lv_slider_create(panel_right);
    lv_obj_set_pos(slider_progress, 20, 160);
    lv_obj_set_size(slider_progress, 340, 5);
    lv_slider_set_range(slider_progress, 0, 100);
    lv_obj_set_style_bg_color(slider_progress, COL_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_progress, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_progress, ev_progress, LV_EVENT_ALL, NULL);

    // Time elapsed
    lbl_time = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time, 20, 175);
    lv_label_set_text(lbl_time, "00:00");
    lv_obj_set_style_text_color(lbl_time, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_14, 0);

    // Time remaining
    lbl_time_remaining = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time_remaining, 315, 175);
    lv_label_set_text(lbl_time_remaining, "0:00");
    lv_obj_set_style_text_color(lbl_time_remaining, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_time_remaining, &lv_font_montserrat_14, 0);

    // ===== PLAYBACK CONTROLS - PERFECTLY CENTERED =====
    // Layout: [shuffle] [prev] [PLAY] [next] [repeat]
    // Center of 380px panel = 190
    // Play button at center, others symmetrically placed

    int ctrl_y = 260;
    int center_x = 190;
    
    // PLAY button (center) - big white circle with scale effect
    btn_play = lv_btn_create(panel_right);
    lv_obj_set_size(btn_play, 80, 80);
    lv_obj_set_pos(btn_play, center_x - 40, ctrl_y - 40);
    lv_obj_set_style_bg_color(btn_play, COL_TEXT, 0);
    lv_obj_set_style_radius(btn_play, 40, 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_play, 280, LV_STATE_PRESSED);  // Scale to 110%
    lv_obj_set_style_transform_scale_y(btn_play, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_play, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_play, &trans_btn, 0);

    lv_obj_add_event_cb(btn_play, ev_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_play = lv_label_create(btn_play);
    lv_label_set_text(ico_play, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_color(ico_play, COL_BG, 0);
    lv_obj_set_style_text_font(ico_play, &lv_font_montserrat_32, 0);
    lv_obj_center(ico_play);
    
    // PREV button (left of play) - scale effect
    btn_prev = lv_btn_create(panel_right);
    lv_obj_set_size(btn_prev, 50, 50);
    lv_obj_set_pos(btn_prev, center_x - 100, ctrl_y - 25);
    lv_obj_set_style_bg_opa(btn_prev, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_prev, 25, 0);
    lv_obj_set_style_shadow_width(btn_prev, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_prev, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_prev, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_prev, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_prev, &trans_btn, 0);
    lv_obj_add_event_cb(btn_prev, ev_prev, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_prev = lv_label_create(btn_prev);
    lv_label_set_text(ico_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_color(ico_prev, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_prev, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_prev);

    // NEXT button (right of play) - scale effect
    btn_next = lv_btn_create(panel_right);
    lv_obj_set_size(btn_next, 50, 50);
    lv_obj_set_pos(btn_next, center_x + 50, ctrl_y - 25);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_next, 25, 0);
    lv_obj_set_style_shadow_width(btn_next, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_next, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_next, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_next, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_next, &trans_btn, 0);
    lv_obj_add_event_cb(btn_next, ev_next, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_next = lv_label_create(btn_next);
    lv_label_set_text(ico_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_color(ico_next, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_next, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_next);

    // SHUFFLE button (far left) - scale effect
    btn_shuffle = lv_btn_create(panel_right);
    lv_obj_set_size(btn_shuffle, 45, 45);
    lv_obj_set_pos(btn_shuffle, center_x - 160, ctrl_y - 22);
    lv_obj_set_style_bg_opa(btn_shuffle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_shuffle, 22, 0);
    lv_obj_set_style_shadow_width(btn_shuffle, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_shuffle, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_shuffle, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_shuffle, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_shuffle, &trans_btn, 0);
    lv_obj_add_event_cb(btn_shuffle, ev_shuffle, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_shuf = lv_label_create(btn_shuffle);
    lv_label_set_text(ico_shuf, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_color(ico_shuf, COL_TEXT2, 0);
    lv_obj_set_style_text_font(ico_shuf, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_shuf);

    // REPEAT button (far right) - scale effect
    btn_repeat = lv_btn_create(panel_right);
    lv_obj_set_size(btn_repeat, 45, 45);
    lv_obj_set_pos(btn_repeat, center_x + 115, ctrl_y - 22);
    lv_obj_set_style_bg_opa(btn_repeat, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_repeat, 22, 0);
    lv_obj_set_style_shadow_width(btn_repeat, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_repeat, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_repeat, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_repeat, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_repeat, &trans_btn, 0);
    lv_obj_add_event_cb(btn_repeat, ev_repeat, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_rpt = lv_label_create(btn_repeat);
    lv_label_set_text(ico_rpt, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_color(ico_rpt, COL_TEXT2, 0);
    lv_obj_set_style_text_font(ico_rpt, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_rpt);
    
    // ===== VOLUME SLIDER =====
    int vol_y = 340;
    
    // Mute button (left) - scale effect
    btn_mute = lv_btn_create(panel_right);
    lv_obj_set_size(btn_mute, 40, 40);
    lv_obj_set_pos(btn_mute, 20, vol_y);
    lv_obj_set_style_bg_opa(btn_mute, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_mute, 20, 0);
    lv_obj_set_style_transform_scale_y(btn_mute, 280, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn_mute, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_mute, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_mute, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_mute, &trans_btn, 0);
    lv_obj_add_event_cb(btn_mute, ev_mute, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_mute = lv_label_create(btn_mute);
    lv_label_set_text(ico_mute, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_color(ico_mute, COL_TEXT2, 0);
    lv_obj_set_style_text_font(ico_mute, &lv_font_montserrat_18, 0);
    lv_obj_center(ico_mute);
    
    // Volume slider
    slider_vol = lv_slider_create(panel_right);
    lv_obj_set_size(slider_vol, 260, 6);
    lv_obj_set_pos(slider_vol, 70, vol_y + 17);
    lv_slider_set_range(slider_vol, 0, 100);
    lv_obj_set_style_bg_color(slider_vol, COL_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_vol, ev_vol_slider, LV_EVENT_ALL, NULL);

    // ===== PLAY NEXT SECTION (below volume) =====
    int next_y = 440;

    // Small album art for next track (hidden for now)
    img_next_album = lv_img_create(panel_right);
    lv_obj_set_pos(img_next_album, 20, next_y);
    lv_obj_set_size(img_next_album, 40, 40);
    lv_obj_set_style_radius(img_next_album, 4, 0);
    lv_obj_set_style_clip_corner(img_next_album, true, 0);
    lv_obj_add_flag(img_next_album, LV_OBJ_FLAG_HIDDEN); // Hide thumbnail for now

    // "Next:" label
    lbl_next_header = lv_label_create(panel_right);  // Use GLOBAL, not local!
    lv_obj_set_pos(lbl_next_header, 20, next_y);
    lv_label_set_text(lbl_next_header, "Next:");
    lv_obj_set_style_text_color(lbl_next_header, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_next_header, &lv_font_montserrat_12, 0);

    // Next track title - clickable to play next
    lbl_next_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_next_title, 60, next_y);
    lv_label_set_text(lbl_next_title, "");
    lv_obj_set_style_text_color(lbl_next_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_next_title, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_next_title, 300);
    lv_label_set_long_mode(lbl_next_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // Make it clickable to play next track
    lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_next_title, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            sonos.next();
        }
    }, LV_EVENT_ALL, NULL);

    // Next track artist - also clickable
    lbl_next_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_next_artist, 60, next_y + 18);
    lv_label_set_text(lbl_next_artist, "");
    lv_obj_set_style_text_color(lbl_next_artist, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_next_artist, &lv_font_montserrat_12, 0);
    lv_obj_set_width(lbl_next_artist, 300);
    lv_label_set_long_mode(lbl_next_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // Make it clickable to play next track
    lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_next_artist, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            sonos.next();
        }
    }, LV_EVENT_ALL, NULL);
}

void createSourcesScreen() {
    scr_sources = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sources, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_sources);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Music Sources");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Close button in header (back to Main screen)
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Scrollable list
    lv_obj_t* list = lv_obj_create(scr_sources);
    lv_obj_set_pos(list, 20, 85);
    lv_obj_set_size(list, 760, 380);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 10, 0);

    // Music source items
    struct MusicSource {
        const char* name;
        const char* icon;
        const char* objectID;
    };

    MusicSource sources[] = {
        {"Sonos Favorites", LV_SYMBOL_DIRECTORY, "FV:2"},
        {"Sonos Playlists", LV_SYMBOL_LIST, "SQ:"}
    };

    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, 720, 50);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 15, 0);
        lv_obj_set_user_data(btn, (void*)sources[i].objectID);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, sources[i].icon);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, sources[i].name);
        lv_obj_set_style_text_color(name, COL_TEXT, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn_target = (lv_obj_t*)lv_event_get_target(e);
            const char* objID = (const char*)lv_obj_get_user_data(btn_target);
            lv_obj_t* label = lv_obj_get_child(btn_target, 1);
            const char* title = lv_label_get_text(label);

            current_browse_id = String(objID);
            current_browse_title = String(title);

            createBrowseScreen();
            lv_screen_load(scr_browse);
        }, LV_EVENT_CLICKED, NULL);
    }
}

void cleanupBrowseData(lv_obj_t* list) {
    if (!list) return;
    uint32_t child_count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(list, i);
        if (child) {
            void* data = lv_obj_get_user_data(child);
            if (data) {
                heap_caps_free(data);
                lv_obj_set_user_data(child, NULL);
            }
        }
    }
}

void createBrowseScreen() {
    if (scr_browse) {
        lv_obj_t* list = lv_obj_get_child(scr_browse, -1);
        cleanupBrowseData(list);
        lv_obj_del(scr_browse);
    }

    scr_browse = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_browse, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_browse);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, current_browse_title.c_str());
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Close button in header (back to Music Sources)
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, [](lv_event_t* e) {
        lv_obj_t* list = lv_obj_get_child(scr_browse, -1);
        cleanupBrowseData(list);
        lv_screen_load(scr_sources);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Content list
    lv_obj_t* list = lv_obj_create(scr_browse);
    lv_obj_set_pos(list, 20, 85);
    lv_obj_set_size(list, 760, 380);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 10, 0);

    String didl = sonos.browseContent(current_browse_id.c_str());

    Serial.printf("[BROWSE] ID=%s, DIDL length=%d\n", current_browse_id.c_str(), didl.length());

    if (didl.length() == 0) {
        lv_obj_t* lbl_empty = lv_label_create(list);
        lv_label_set_text(lbl_empty, "No items found");
        lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
        return;
    }

    int searchPos = 0;
    int itemCount = 0;

    while (searchPos < (int)didl.length()) {
        int containerPos = didl.indexOf("<container", searchPos);
        int itemPos = didl.indexOf("<item", searchPos);

        if (containerPos < 0 && itemPos < 0) break;

        bool isContainer = false;
        if (containerPos >= 0 && (itemPos < 0 || containerPos < itemPos)) {
            searchPos = containerPos;
            isContainer = true;
        } else if (itemPos >= 0) {
            searchPos = itemPos;
            isContainer = false;
        } else {
            break;
        }

        int endPos = isContainer ? didl.indexOf("</container>", searchPos) : didl.indexOf("</item>", searchPos);
        if (endPos < 0) break;

        String itemXML = didl.substring(searchPos, endPos + (isContainer ? 12 : 7));
        String title = sonos.extractXML(itemXML, "dc:title");

        int idStart = itemXML.indexOf("id=\"") + 4;
        int idEnd = itemXML.indexOf("\"", idStart);
        String id = itemXML.substring(idStart, idEnd);

        Serial.printf("[BROWSE] Item #%d: %s (container=%d, id=%s)\n",
                      itemCount, title.c_str(), isContainer, id.c_str());

        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, 720, 60);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 15, 0);

        struct ItemData {
            char id[128];
            char itemXML[2048];  // Increased for full DIDL-Lite metadata with r:resMD
            bool isContainer;
        };
        ItemData* data = (ItemData*)heap_caps_malloc(sizeof(ItemData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!data) {
            Serial.println("[BROWSE] PSRAM malloc failed, trying regular heap...");
            data = (ItemData*)malloc(sizeof(ItemData));
            if (!data) {
                Serial.println("[BROWSE] Regular malloc also failed!");
                break;
            }
        }
        strncpy(data->id, id.c_str(), sizeof(data->id) - 1);
        data->id[sizeof(data->id) - 1] = '\0';

        if (itemXML.length() >= sizeof(data->itemXML)) {
            itemXML = itemXML.substring(0, sizeof(data->itemXML) - 1);
        }
        strncpy(data->itemXML, itemXML.c_str(), sizeof(data->itemXML) - 1);
        data->itemXML[sizeof(data->itemXML) - 1] = '\0';
        data->isContainer = isContainer;
        lv_obj_set_user_data(btn, data);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, isContainer ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, title.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 40, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lbl, 640);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            ItemData* data = (ItemData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            String itemXML = String(data->itemXML);
            String id = String(data->id);

            String uri = sonos.extractXML(itemXML, "res");
            uri = sonos.decodeHTML(uri);

            if (data->isContainer) {
                if (id.startsWith("SQ:") && id.indexOf("/") < 0) {
                    String title = sonos.extractXML(itemXML, "dc:title");
                    Serial.printf("[BROWSE] Playing playlist: %s (ID: %s)\n", title.c_str(), id.c_str());
                    sonos.playPlaylist(id.c_str());
                    lv_screen_load(scr_main);
                } else {
                    current_browse_id = id;
                    current_browse_title = sonos.extractXML(itemXML, "dc:title");
                    createBrowseScreen();
                    lv_screen_load(scr_browse);
                }
            } else {

                if (uri.length() == 0) {
                    String resMD = sonos.extractXML(itemXML, "r:resMD");
                    if (resMD.length() > 0) {
                        resMD = sonos.decodeHTML(resMD);

                        if (resMD.indexOf("<upnp:class>object.container</upnp:class>") >= 0) {
                            int idStart = resMD.indexOf("id=\"") + 4;
                            int idEnd = resMD.indexOf("\"", idStart);
                            String containerID = resMD.substring(idStart, idEnd);
                            current_browse_id = containerID;
                            current_browse_title = sonos.extractXML(resMD, "dc:title");
                            Serial.printf("[BROWSE] Shortcut to container: %s\n", containerID.c_str());
                            createBrowseScreen();
                            lv_screen_load(scr_browse);
                            return;
                        }

                        uri = sonos.extractXML(resMD, "res");
                    }
                }

                if (uri.startsWith("x-rincon-cpcontainer:")) {
                    String title = sonos.extractXML(itemXML, "dc:title");
                    Serial.printf("[BROWSE] Playing container: %s\n", title.c_str());

                    // Extract inner DIDL-Lite from r:resMD tag
                    String resMD = sonos.extractXML(itemXML, "r:resMD");
                    if (resMD.length() > 0) {
                        resMD = sonos.decodeHTML(resMD);

                        // Extract <res> tag from outer item and inject into inner DIDL
                        String resTag = sonos.extractXML(itemXML, "res");
                        String protocolInfo = "";
                        int protoStart = itemXML.indexOf("protocolInfo=\"");
                        if (protoStart > 0) {
                            int protoEnd = itemXML.indexOf("\"", protoStart + 14);
                            if (protoEnd > protoStart) {
                                protocolInfo = itemXML.substring(protoStart + 14, protoEnd);
                            }
                        }

                        // Build complete <res> element
                        String resElement = "<res protocolInfo=\"" + protocolInfo + "\">" + uri + "</res>";

                        // Insert <res> into the inner DIDL's <item> (after <upnp:class>)
                        int insertPos = resMD.indexOf("</upnp:class>") + 13;
                        if (insertPos > 13) {
                            resMD = resMD.substring(0, insertPos) + resElement + resMD.substring(insertPos);
                        }

                        Serial.printf("[BROWSE] Enhanced inner DIDL with <res> tag (%d bytes)\n", resMD.length());
                        sonos.playContainer(uri.c_str(), resMD.c_str());
                    } else {
                        Serial.println("[BROWSE] No r:resMD found, using full itemXML");
                        sonos.playContainer(uri.c_str(), itemXML.c_str());
                    }
                    lv_screen_load(scr_main);
                } else if (uri.length() > 0) {
                    Serial.printf("[BROWSE] Playing URI: %s\n", uri.c_str());
                    sonos.playURI(uri.c_str(), itemXML.c_str());
                    lv_screen_load(scr_main);
                } else {
                    Serial.println("[BROWSE] No URI found!");
                }
            }
        }, LV_EVENT_CLICKED, NULL);

        searchPos = endPos + (isContainer ? 12 : 7);
        itemCount++;
        if (itemCount >= 20) {
            Serial.printf("[BROWSE] Reached 20 item limit, stopping\n");
            break;
        }
    }

    if (itemCount == 0) {
        lv_obj_t* lbl_empty = lv_label_create(list);
        lv_label_set_text(lbl_empty, "No items found");
        lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
    }

    Serial.printf("[BROWSE] Created %d items, free heap: %d bytes\n", itemCount, esp_get_free_heap_size());
}

void createDevicesScreen() {
    scr_devices = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_devices, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_devices);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Sonos Speakers");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Scan button in header (gold accent) - store globally for feedback
    btn_sonos_scan = lv_button_create(header);
    lv_obj_set_size(btn_sonos_scan, 110, 50);
    lv_obj_align(btn_sonos_scan, LV_ALIGN_RIGHT_MID, -80, 0);
    lv_obj_set_style_bg_color(btn_sonos_scan, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_sonos_scan, 25, 0);
    lv_obj_set_style_shadow_width(btn_sonos_scan, 0, 0);
    lv_obj_add_event_cb(btn_sonos_scan, ev_discover, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_scan = lv_label_create(btn_sonos_scan);
    lv_label_set_text(lbl_scan, LV_SYMBOL_REFRESH "  Scan");
    lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_scan, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_scan);

    // Close button in header (back to Main screen)
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Status label
    lbl_status = lv_label_create(scr_devices);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_LEFT, 40, 85);
    lv_label_set_text(lbl_status, "Tap Scan to find speakers");
    lv_obj_set_style_text_color(lbl_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);

    // Devices list
    list_devices = lv_list_create(scr_devices);
    lv_obj_set_size(list_devices, 720, 360);
    lv_obj_set_pos(list_devices, 40, 115);
    lv_obj_set_style_bg_color(list_devices, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_devices, 0, 0);
    lv_obj_set_style_radius(list_devices, 0, 0);
    lv_obj_set_style_pad_all(list_devices, 0, 0);
    lv_obj_set_style_pad_row(list_devices, 8, 0);

    // Professional scrollbar styling
    lv_obj_set_style_pad_right(list_devices, 8, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_devices, LV_OPA_30, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_devices, COL_TEXT2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_devices, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_devices, 3, LV_PART_SCROLLBAR);
}

void createQueueScreen() {
    scr_queue = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_queue, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_queue);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Playlist");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Refresh button in header
    lv_obj_t* btn_refresh = lv_button_create(header);
    lv_obj_set_size(btn_refresh, 50, 50);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, -80, 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_refresh, 25, 0);
    lv_obj_set_style_shadow_width(btn_refresh, 0, 0);
    lv_obj_add_event_cb(btn_refresh, [](lv_event_t* e) { sonos.updateQueue(); refreshQueueList(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(ico_refresh, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(ico_refresh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_refresh, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_refresh);

    // Close button in header
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Status label below header
    lbl_queue_status = lv_label_create(scr_queue);
    lv_obj_align(lbl_queue_status, LV_ALIGN_TOP_LEFT, 40, 85);
    lv_label_set_text(lbl_queue_status, "Loading...");
    lv_obj_set_style_text_color(lbl_queue_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_queue_status, &lv_font_montserrat_14, 0);

    // Queue list - modern clean design
    list_queue = lv_list_create(scr_queue);
    lv_obj_set_size(list_queue, 730, 360);
    lv_obj_set_pos(list_queue, 35, 115);
    lv_obj_set_style_bg_color(list_queue, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_queue, 0, 0);
    lv_obj_set_style_radius(list_queue, 0, 0);
    lv_obj_set_style_pad_all(list_queue, 0, 0);
    lv_obj_set_style_pad_row(list_queue, 0, 0);  // No spacing between items

    // Modern thin scrollbar on the right edge
    lv_obj_set_style_pad_right(list_queue, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_queue, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_queue, COL_ACCENT, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_queue, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_queue, 0, LV_PART_SCROLLBAR);
}

void createSettingsScreen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_settings);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Close button in header
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Display Settings Button (modern card style)
    lv_obj_t* btn_display = lv_button_create(scr_settings);
    lv_obj_set_size(btn_display, 720, 80);
    lv_obj_set_pos(btn_display, 40, 100);
    lv_obj_set_style_bg_color(btn_display, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(btn_display, 15, 0);
    lv_obj_set_style_shadow_width(btn_display, 0, 0);
    lv_obj_add_event_cb(btn_display, ev_display_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_display = lv_label_create(btn_display);
    lv_label_set_text(lbl_display, LV_SYMBOL_EYE_OPEN "  Display Settings");
    lv_obj_set_style_text_color(lbl_display, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_display, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_display, LV_ALIGN_LEFT_MID, 30, 0);

    // WiFi Settings Button
    lv_obj_t* btn_wifi = lv_button_create(scr_settings);
    lv_obj_set_size(btn_wifi, 720, 80);
    lv_obj_set_pos(btn_wifi, 40, 195);
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(btn_wifi, 15, 0);
    lv_obj_set_style_shadow_width(btn_wifi, 0, 0);
    lv_obj_add_event_cb(btn_wifi, [](lv_event_t* e) { lv_screen_load(scr_wifi); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  WiFi Settings");
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, 30, 0);

    // OTA Update Button
    lv_obj_t* btn_ota = lv_button_create(scr_settings);
    lv_obj_set_size(btn_ota, 720, 80);
    lv_obj_set_pos(btn_ota, 40, 290);
    lv_obj_set_style_bg_color(btn_ota, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(btn_ota, 15, 0);
    lv_obj_set_style_shadow_width(btn_ota, 0, 0);
    lv_obj_add_event_cb(btn_ota, ev_ota_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_ota = lv_label_create(btn_ota);
    lv_label_set_text(lbl_ota, LV_SYMBOL_DOWNLOAD "  Firmware Update");
    lv_obj_set_style_text_color(lbl_ota, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_ota, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_ota, LV_ALIGN_LEFT_MID, 30, 0);

    // Speakers Button
    lv_obj_t* btn_speakers = lv_button_create(scr_settings);
    lv_obj_set_size(btn_speakers, 720, 80);
    lv_obj_set_pos(btn_speakers, 40, 385);
    lv_obj_set_style_bg_color(btn_speakers, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(btn_speakers, 15, 0);
    lv_obj_set_style_shadow_width(btn_speakers, 0, 0);
    lv_obj_add_event_cb(btn_speakers, ev_devices, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_speakers = lv_label_create(btn_speakers);
    lv_label_set_text(lbl_speakers, LV_SYMBOL_AUDIO "  Sonos Speakers");
    lv_obj_set_style_text_color(lbl_speakers, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_speakers, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_speakers, LV_ALIGN_LEFT_MID, 30, 0);

    // Music Sources Button (moved down to make room for OTA)
    lv_obj_t* btn_sources = lv_button_create(scr_settings);
    lv_obj_set_size(btn_sources, 720, 80);
    lv_obj_set_pos(btn_sources, 40, 480);
    lv_obj_set_style_bg_color(btn_sources, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(btn_sources, 15, 0);
    lv_obj_set_style_shadow_width(btn_sources, 0, 0);
    lv_obj_add_event_cb(btn_sources, [](lv_event_t* e) { lv_screen_load(scr_sources); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_sources = lv_label_create(btn_sources);
    lv_label_set_text(lbl_sources, LV_SYMBOL_LIST "  Music Sources");
    lv_obj_set_style_text_color(lbl_sources, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_sources, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_sources, LV_ALIGN_LEFT_MID, 30, 0);
}

void createDisplaySettingsScreen() {
    scr_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_display, lv_color_hex(0x1A1A1A), 0);

    // Professional header with gradient
    lv_obj_t* header = lv_obj_create(scr_display);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Display Settings");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Back button in header (modern circular style)
    lv_obj_t* btn_back_header = lv_button_create(header);
    lv_obj_set_size(btn_back_header, 50, 50);
    lv_obj_align(btn_back_header, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_back_header, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_back_header, 25, 0);
    lv_obj_set_style_shadow_width(btn_back_header, 0, 0);
    lv_obj_add_event_cb(btn_back_header, ev_back_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_back = lv_label_create(btn_back_header);
    lv_label_set_text(ico_back, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_back, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_back, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_back);

    // Brightness
    lv_obj_t* lbl_brightness = lv_label_create(scr_display);
    lv_label_set_text(lbl_brightness, "Brightness:");
    lv_obj_set_style_text_color(lbl_brightness, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_brightness, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_brightness, LV_ALIGN_TOP_LEFT, 40, 100);

    static lv_obj_t* lbl_brightness_val;
    lbl_brightness_val = lv_label_create(scr_display);
    lv_label_set_text_fmt(lbl_brightness_val, "%d%%", brightness_level);
    lv_obj_set_style_text_color(lbl_brightness_val, lv_color_hex(0xD4A84B), 0);  // Accent color
    lv_obj_set_style_text_font(lbl_brightness_val, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_brightness_val, LV_ALIGN_TOP_RIGHT, -40, 100);

    lv_obj_t* slider_brightness = lv_slider_create(scr_display);
    lv_obj_set_size(slider_brightness, 720, 20);
    lv_obj_align(slider_brightness, LV_ALIGN_TOP_MID, 0, 135);
    lv_slider_set_range(slider_brightness, 10, 100);
    lv_slider_set_value(slider_brightness, brightness_level, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_brightness, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_brightness, lv_color_hex(0xD4A84B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_brightness, lv_color_hex(0xD4A84B), LV_PART_KNOB);
    lv_obj_set_style_radius(slider_brightness, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_brightness, 10, LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_brightness, [](lv_event_t* e) {
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        int val = lv_slider_get_value(slider);
        setBrightness(val);
        lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", val);
    }, LV_EVENT_VALUE_CHANGED, lbl_brightness_val);

    // Dim timeout
    lv_obj_t* lbl_dim_timeout = lv_label_create(scr_display);
    lv_label_set_text(lbl_dim_timeout, "Auto-dim after:");
    lv_obj_set_style_text_color(lbl_dim_timeout, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_dim_timeout, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_dim_timeout, LV_ALIGN_TOP_LEFT, 40, 185);

    static lv_obj_t* lbl_dim_timeout_val;
    lbl_dim_timeout_val = lv_label_create(scr_display);
    lv_label_set_text_fmt(lbl_dim_timeout_val, "%d sec", autodim_timeout);
    lv_obj_set_style_text_color(lbl_dim_timeout_val, lv_color_hex(0xD4A84B), 0);  // Accent color
    lv_obj_set_style_text_font(lbl_dim_timeout_val, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_dim_timeout_val, LV_ALIGN_TOP_RIGHT, -40, 185);

    lv_obj_t* slider_dim_timeout = lv_slider_create(scr_display);
    lv_obj_set_size(slider_dim_timeout, 720, 20);
    lv_obj_align(slider_dim_timeout, LV_ALIGN_TOP_MID, 0, 220);
    lv_slider_set_range(slider_dim_timeout, 0, 300);
    lv_slider_set_value(slider_dim_timeout, autodim_timeout, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_dim_timeout, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_dim_timeout, lv_color_hex(0xD4A84B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_dim_timeout, lv_color_hex(0xD4A84B), LV_PART_KNOB);
    lv_obj_set_style_radius(slider_dim_timeout, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_dim_timeout, 10, LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_dim_timeout, [](lv_event_t* e) {
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        autodim_timeout = lv_slider_get_value(slider);
        lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d sec", autodim_timeout);
        wifiPrefs.putInt("autodim_sec", autodim_timeout);
    }, LV_EVENT_VALUE_CHANGED, lbl_dim_timeout_val);

    // Dimmed brightness
    lv_obj_t* lbl_dimmed = lv_label_create(scr_display);
    lv_label_set_text(lbl_dimmed, "Dimmed brightness:");
    lv_obj_set_style_text_color(lbl_dimmed, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_dimmed, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_dimmed, LV_ALIGN_TOP_LEFT, 40, 270);

    static lv_obj_t* lbl_dimmed_brightness_val;
    lbl_dimmed_brightness_val = lv_label_create(scr_display);
    lv_label_set_text_fmt(lbl_dimmed_brightness_val, "%d%%", brightness_dimmed);
    lv_obj_set_style_text_color(lbl_dimmed_brightness_val, lv_color_hex(0xD4A84B), 0);  // Accent color
    lv_obj_set_style_text_font(lbl_dimmed_brightness_val, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_dimmed_brightness_val, LV_ALIGN_TOP_RIGHT, -40, 270);

    lv_obj_t* slider_dimmed_brightness = lv_slider_create(scr_display);
    lv_obj_set_size(slider_dimmed_brightness, 720, 20);
    lv_obj_align(slider_dimmed_brightness, LV_ALIGN_TOP_MID, 0, 305);
    lv_slider_set_range(slider_dimmed_brightness, 5, 50);
    lv_slider_set_value(slider_dimmed_brightness, brightness_dimmed, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_dimmed_brightness, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_dimmed_brightness, lv_color_hex(0xD4A84B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_dimmed_brightness, lv_color_hex(0xD4A84B), LV_PART_KNOB);
    lv_obj_set_style_radius(slider_dimmed_brightness, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_dimmed_brightness, 10, LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_dimmed_brightness, [](lv_event_t* e) {
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        brightness_dimmed = lv_slider_get_value(slider);
        lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", brightness_dimmed);
        wifiPrefs.putInt("brightness_dimmed", brightness_dimmed);
        if (screen_dimmed) setBrightness(brightness_dimmed);
    }, LV_EVENT_VALUE_CHANGED, lbl_dimmed_brightness_val);
}

void createWiFiScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_wifi);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "WiFi Settings");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Scan button in header (gold accent) - store reference globally
    btn_wifi_scan = lv_button_create(header);
    lv_obj_set_size(btn_wifi_scan, 110, 50);
    lv_obj_align(btn_wifi_scan, LV_ALIGN_RIGHT_MID, -80, 0);
    lv_obj_set_style_bg_color(btn_wifi_scan, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_scan, 25, 0);
    lv_obj_set_style_shadow_width(btn_wifi_scan, 0, 0);
    lv_obj_add_event_cb(btn_wifi_scan, ev_wifi_scan, LV_EVENT_CLICKED, NULL);
    lbl_scan_text = lv_label_create(btn_wifi_scan);
    lv_label_set_text(lbl_scan_text, LV_SYMBOL_REFRESH "  Scan");
    lv_obj_set_style_text_color(lbl_scan_text, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_scan_text, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_scan_text);

    // Close button in header (back to Settings)
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Status label
    lbl_wifi_status = lv_label_create(scr_wifi);
    lv_obj_align(lbl_wifi_status, LV_ALIGN_TOP_LEFT, 40, 85);
    lv_label_set_text(lbl_wifi_status, "Tap Scan to find networks");
    lv_obj_set_style_text_color(lbl_wifi_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_montserrat_14, 0);

    // WiFi list
    list_wifi = lv_list_create(scr_wifi);
    lv_obj_set_size(list_wifi, 360, 400);
    lv_obj_set_pos(list_wifi, 40, 115);
    lv_obj_set_style_bg_color(list_wifi, COL_BG, 0);
    lv_obj_set_style_border_width(list_wifi, 0, 0);
    lv_obj_set_style_radius(list_wifi, 0, 0);
    lv_obj_set_style_pad_all(list_wifi, 0, 0);
    lv_obj_set_style_pad_row(list_wifi, 8, 0);
    
    lv_obj_t* pl = lv_label_create(scr_wifi);
    lv_obj_set_pos(pl, 410, 105);
    lv_label_set_text(pl, "Password:");
    lv_obj_set_style_text_color(pl, COL_TEXT, 0);
    
    ta_password = lv_textarea_create(scr_wifi);
    lv_obj_set_size(ta_password, 350, 45);
    lv_obj_set_pos(ta_password, 410, 130);
    lv_textarea_set_password_mode(ta_password, true);
    lv_textarea_set_placeholder_text(ta_password, "Enter password");
    lv_obj_set_style_bg_color(ta_password, COL_CARD, 0);
    lv_obj_set_style_text_color(ta_password, COL_TEXT, 0);
    lv_obj_set_style_border_color(ta_password, COL_BTN, 0);
    lv_obj_add_event_cb(ta_password, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_FOCUSED) lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_ALL, NULL);
    
    btn_wifi_connect = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_wifi_connect, 350, 50);
    lv_obj_set_pos(btn_wifi_connect, 410, 185);
    lv_obj_set_style_bg_color(btn_wifi_connect, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_connect, 12, 0);
    lv_obj_add_event_cb(btn_wifi_connect, ev_wifi_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cl = lv_label_create(btn_wifi_connect);
    lv_label_set_text(cl, "Connect");
    lv_obj_set_style_text_color(cl, lv_color_hex(0x000000), 0);
    lv_obj_center(cl);
    
    // Built-in LVGL keyboard with modern professional design
    kb = lv_keyboard_create(scr_wifi);
    lv_keyboard_set_textarea(kb, ta_password);  // Auto-link to password field
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);  // Start in lowercase
    lv_obj_set_size(kb, 780, 175);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // Apply custom styling to match theme
    lv_obj_set_style_bg_color(kb, COL_CARD, 0);
    lv_obj_set_style_pad_all(kb, 5, 0);
    lv_obj_set_style_radius(kb, 10, 0);
    lv_obj_set_style_bg_color(kb, COL_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);

    // Hide keyboard when OK is pressed (auto-handled by built-in keyboard)
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY) {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_ALL, NULL);
}

void createOTAScreen() {
    scr_ota = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_ota, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_ota);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Firmware Update");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Close button in header (back to Settings)
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Version info card
    lv_obj_t* card_version = lv_obj_create(scr_ota);
    lv_obj_set_size(card_version, 720, 120);
    lv_obj_set_pos(card_version, 40, 90);
    lv_obj_set_style_bg_color(card_version, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(card_version, 15, 0);
    lv_obj_set_style_border_width(card_version, 0, 0);
    lv_obj_clear_flag(card_version, LV_OBJ_FLAG_SCROLLABLE);

    lbl_current_version = lv_label_create(card_version);
    lv_label_set_text_fmt(lbl_current_version, "Current: v" FIRMWARE_VERSION);
    lv_obj_set_style_text_font(lbl_current_version, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_current_version, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_current_version, LV_ALIGN_TOP_LEFT, 20, 20);

    lbl_latest_version = lv_label_create(card_version);
    lv_label_set_text(lbl_latest_version, "Latest: Checking...");
    lv_obj_set_style_text_font(lbl_latest_version, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_latest_version, COL_TEXT2, 0);
    lv_obj_align(lbl_latest_version, LV_ALIGN_TOP_LEFT, 20, 55);

    // Status label
    lbl_ota_status = lv_label_create(scr_ota);
    lv_obj_align(lbl_ota_status, LV_ALIGN_TOP_LEFT, 40, 230);
    lv_label_set_text(lbl_ota_status, "Tap 'Check for Updates' to begin");
    lv_obj_set_style_text_color(lbl_ota_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_ota_status, &lv_font_montserrat_16, 0);
    lv_obj_set_width(lbl_ota_status, 720);
    lv_label_set_long_mode(lbl_ota_status, LV_LABEL_LONG_WRAP);

    // Progress label
    lbl_ota_progress = lv_label_create(scr_ota);
    lv_obj_align(lbl_ota_progress, LV_ALIGN_TOP_RIGHT, -40, 230);
    lv_label_set_text(lbl_ota_progress, "");
    lv_obj_set_style_text_color(lbl_ota_progress, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_ota_progress, &lv_font_montserrat_20, 0);

    // Visual progress bar (hidden by default)
    bar_ota_progress = lv_bar_create(scr_ota);
    lv_obj_set_size(bar_ota_progress, 720, 20);
    lv_obj_set_pos(bar_ota_progress, 40, 260);
    lv_bar_set_range(bar_ota_progress, 0, 100);
    lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_ota_progress, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_ota_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_ota_progress, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_ota_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_ota_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_ota_progress, 10, LV_PART_INDICATOR);
    lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);  // Hidden until update starts

    // Check for Updates button
    btn_check_update = lv_btn_create(scr_ota);
    lv_obj_set_size(btn_check_update, 340, 60);
    lv_obj_set_pos(btn_check_update, 40, 310);
    lv_obj_set_style_bg_color(btn_check_update, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_check_update, 12, 0);
    lv_obj_add_event_cb(btn_check_update, ev_check_update, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_check = lv_label_create(btn_check_update);
    lv_label_set_text(lbl_check, LV_SYMBOL_REFRESH "  Check for Updates");
    lv_obj_set_style_text_color(lbl_check, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_check, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_check);

    // Install Update button (hidden by default)
    btn_install_update = lv_btn_create(scr_ota);
    lv_obj_set_size(btn_install_update, 340, 60);
    lv_obj_set_pos(btn_install_update, 420, 310);
    lv_obj_set_style_bg_color(btn_install_update, lv_color_hex(0x4ECB71), 0);
    lv_obj_set_style_radius(btn_install_update, 12, 0);
    lv_obj_add_event_cb(btn_install_update, ev_install_update, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_install = lv_label_create(btn_install_update);
    lv_label_set_text(lbl_install, LV_SYMBOL_DOWNLOAD "  Install Update");
    lv_obj_set_style_text_color(lbl_install, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_install, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_install);
    lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);  // Hidden until update available

    // Info text
    lv_obj_t* lbl_info = lv_label_create(scr_ota);
    lv_label_set_text(lbl_info,
        LV_SYMBOL_WARNING "  Do not disconnect power during update!\n"
        "Updates are fetched from GitHub releases automatically.");
    lv_obj_set_style_text_color(lbl_info, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_info, 720);
    lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl_info, LV_ALIGN_BOTTOM_LEFT, 40, -20);
}

// Theme screen removed - using fixed professional theme only

void updateUI() {
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) return;

    // Track connection state - start as disconnected to avoid false triggers
    static bool was_connected = false;
    static bool ui_cleared = false;
    static bool last_conn_state = false;

    // Debug: Log connection state changes
    if (d->connected != last_conn_state) {
        Serial.printf("[UI] Connection state changed: %s (errorCount=%d)\n",
                     d->connected ? "CONNECTED" : "DISCONNECTED", d->errorCount);
        last_conn_state = d->connected;
    }

    // Handle disconnection - only clear UI once
    if (!d->connected) {
        if (was_connected || !ui_cleared) {
            // Device just disconnected or first time showing disconnect state
            lv_label_set_text(lbl_title, "Device Not Connected");
            lv_label_set_text(lbl_artist, "");
            lv_label_set_text(lbl_album, "");
            lv_label_set_text(lbl_time, "0:00");
            lv_label_set_text(lbl_time_remaining, "0:00");
            lv_slider_set_value(slider_progress, 0, LV_ANIM_OFF);

            // Hide album art, show placeholder
            lv_obj_add_flag(img_album, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);

            // Hide next track info
            lv_obj_add_flag(img_next_album, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);

            // Reset background color to default dark
            if (panel_art) lv_obj_set_style_bg_color(panel_art, lv_color_hex(0x1a1a1a), 0);
            if (panel_right) lv_obj_set_style_bg_color(panel_right, COL_BG, 0);

            // Reset play button to pause icon
            lv_obj_t* lbl = lv_obj_get_child(btn_play, 0);
            lv_label_set_text(lbl, LV_SYMBOL_PAUSE);
            lv_obj_center(lbl);

            ui_title = "";
            ui_artist = "";
            was_connected = false;
            ui_cleared = true;
            Serial.println("[UI] Device disconnected - UI cleared");
        }
        return;  // Don't update UI when disconnected
    }

    // Handle reconnection - force UI refresh
    if (d->connected && !was_connected) {
        was_connected = true;
        ui_cleared = false;
        // Force refresh of all UI elements by clearing cached values
        ui_title = "";
        ui_artist = "";
        Serial.println("[UI] Device reconnected - forcing UI refresh");
    }

    // Device is connected - update UI normally

    // Title
    if (d->currentTrack != ui_title) {
        lv_label_set_text(lbl_title, d->currentTrack.length() > 0 ? d->currentTrack.c_str() : "Not Playing");
        ui_title = d->currentTrack;
    }
    
    // Artist
    if (d->currentArtist != ui_artist) {
        lv_label_set_text(lbl_artist, d->currentArtist.c_str());
        ui_artist = d->currentArtist;
    }
    
    // Album name (below album art)
    static String ui_album_name = "";
    if (d->currentAlbum != ui_album_name) {
        lv_label_set_text(lbl_album, d->currentAlbum.c_str());
        ui_album_name = d->currentAlbum;
    }
    
    // Device name in header
    static String ui_device_name = "";
    if (d->roomName != ui_device_name) {
        String np = "Now Playing - " + d->roomName;
        lv_label_set_text(lbl_device_name, np.c_str());
        ui_device_name = d->roomName;
    }

    // Time display
    String t = d->relTime;
    if (t.startsWith("0:")) t = t.substring(2);
    lv_label_set_text(lbl_time, t.c_str());
    
    // Total duration
    if (d->durationSeconds > 0) {
        int dm = d->durationSeconds / 60;
        int ds = d->durationSeconds % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", dm, ds);
        lv_label_set_text(lbl_time_remaining, buf);
    }
    
    // Progress slider
    if (!dragging_prog && d->durationSeconds > 0)
        lv_slider_set_value(slider_progress, (d->relTimeSeconds * 100) / d->durationSeconds, LV_ANIM_OFF);
    
    // Play/Pause button
    if (d->isPlaying != ui_playing) {
        lv_obj_t* lbl = lv_obj_get_child(btn_play, 0);
        lv_label_set_text(lbl, d->isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

        // Center the icon properly - play triangle needs offset to look centered
        if (d->isPlaying) {
            lv_obj_center(lbl);  // Pause is centered
        } else {
            lv_obj_align(lbl, LV_ALIGN_CENTER, 2, 0);  // Play needs 2px right offset
        }

        ui_playing = d->isPlaying;
    }

    // Volume slider update
    if (!dragging_vol && d->volume != ui_vol && slider_vol) {
        lv_slider_set_value(slider_vol, d->volume, LV_ANIM_OFF);
        ui_vol = d->volume;
    }
    
    // Mute button
    if (d->isMuted != ui_muted && btn_mute) {
        lv_obj_t* lbl = lv_obj_get_child(btn_mute, 0);
        lv_label_set_text(lbl, d->isMuted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
        ui_muted = d->isMuted;
    }
    
    // Shuffle
    if (d->shuffleMode != ui_shuffle) {
        lv_obj_t* lbl = lv_obj_get_child(btn_shuffle, 0);
        lv_obj_set_style_text_color(lbl, d->shuffleMode ? COL_ACCENT : COL_TEXT2, 0);
        ui_shuffle = d->shuffleMode;
    }
    
    // Next track info - find next track in queue
    static String last_next_title = "";
    if (d->queueSize > 0 && d->currentTrackNumber > 0) {
        int nextIdx = -1;

        // Find next track after current
        for (int i = 0; i < d->queueSize; i++) {
            if (d->queue[i].trackNumber == d->currentTrackNumber + 1) {
                nextIdx = i;
                break;
            }
        }

        // If we're on last track and repeat is on, show first track
        if (nextIdx < 0 && (d->repeatMode == "ALL" || d->repeatMode == "ONE")) {
            for (int i = 0; i < d->queueSize; i++) {
                if (d->queue[i].trackNumber == 1) {
                    nextIdx = i;
                    break;
                }
            }
        }

        if (nextIdx >= 0 && d->queue[nextIdx].title.length() > 0) {
            String nextTitle = d->queue[nextIdx].title;
            if (nextTitle != last_next_title) {
                lv_label_set_text(lbl_next_title, d->queue[nextIdx].title.c_str());
                lv_label_set_text(lbl_next_artist, d->queue[nextIdx].artist.c_str());
                lv_obj_clear_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
                last_next_title = nextTitle;
            }
        } else if (nextIdx < 0) {
            // Only hide if next track is truly unavailable (not just temporarily)
            if (last_next_title != "") {
                lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
                last_next_title = "";
            }
        }
    } else {
        if (last_next_title != "") {
            lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
            last_next_title = "";
        }
    }

    // Repeat
    if (d->repeatMode != ui_repeat) {
        lv_obj_t* lbl = lv_obj_get_child(btn_repeat, 0);
        if (d->repeatMode == "ONE") {
            lv_label_set_text(lbl, "1");
            lv_obj_set_style_text_color(lbl, COL_ACCENT, 0);
        } else if (d->repeatMode == "ALL") {
            lv_label_set_text(lbl, LV_SYMBOL_LOOP);
            lv_obj_set_style_text_color(lbl, COL_ACCENT, 0);
        } else {
            lv_label_set_text(lbl, LV_SYMBOL_LOOP);
            lv_obj_set_style_text_color(lbl, COL_TEXT2, 0);
        }
        ui_repeat = d->repeatMode;
    }
    
    // WiFi indicator (disabled - icon removed)
    // if (WiFi.status() == WL_CONNECTED) {
    //     lv_obj_set_style_text_color(lbl_wifi_icon, COL_TEXT2, 0);
    // } else {
    //     lv_obj_set_style_text_color(lbl_wifi_icon, COL_HEART, 0);
    // }
    
    // Album art - fast instant update
    if (d->albumArtURL.length() > 0) requestAlbumArt(d->albumArtURL);
    if (xSemaphoreTake(art_mutex, 0)) {
        if (art_ready) {
            lv_img_set_src(img_album, &art_dsc);
            lv_obj_remove_flag(img_album, LV_OBJ_FLAG_HIDDEN);  // Show album art
            lv_obj_add_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);  // Hide placeholder
            art_ready = false;
        }
        if (color_ready && panel_art && panel_right) {
            setBackgroundColor(dominant_color);
            color_ready = false;
        }
        xSemaphoreGive(art_mutex);
    }
}

static uint32_t lastUpdate = 0;
void processUpdates() {
    UIUpdate_t upd;
    bool need = false;
    while (xQueueReceive(sonos.getUIUpdateQueue(), &upd, 0)) need = true;
    if (need && (millis() - lastUpdate > 200)) { updateUI(); lastUpdate = millis(); }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== SONOS CONTROLLER ===");
    Serial.printf("Free heap: %d, PSRAM: %d\n", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    wifiPrefs.begin("sonos_wifi", false);

    // Brightness will be set after display_init() is called
    Serial.println("[DISPLAY] ESP32-P4 uses ST7701 backlight control (no PWM needed)");

    String ssid = wifiPrefs.getString("ssid", DEFAULT_WIFI_SSID);
    String pass = wifiPrefs.getString("pass", DEFAULT_WIFI_PASSWORD);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WIFI] Connected - IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WIFI] Connection failed - will retry from settings");
    }

    lv_init();
    if (!display_init()) { Serial.println("Display FAIL"); while(1) delay(1000); }
    if (!touch_init()) { Serial.println("Touch FAIL"); while(1) delay(1000); }

    // Set initial brightness
    setBrightness(brightness_level);
    Serial.printf("[DISPLAY] Initial brightness: %d%%\n", brightness_level);

    // Show boot screen with Sonos logo
    lv_obj_t* boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_scr, lv_color_hex(0x000000), 0);
    lv_screen_load(boot_scr);

    // Sonos logo (scale down significantly)
    lv_obj_t* img_logo = lv_image_create(boot_scr);
    lv_image_set_src(img_logo, &Sonos_idnu60bqes_1);
    lv_obj_align(img_logo, LV_ALIGN_CENTER, 0, -30);
    // Scale down significantly (256 = 100%, so 80 = ~31% size, 100 = ~39% size)
    lv_image_set_scale(img_logo, 130);  // Smaller - about 25% of original size

    // Create animated progress bar below logo
    lv_obj_t* boot_bar = lv_bar_create(boot_scr);
    lv_obj_set_size(boot_bar, 300, 8);
    lv_obj_align(boot_bar, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_color(boot_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(boot_bar, lv_color_hex(0xD4A84B), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(boot_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(boot_bar, 0, 100);
    lv_bar_set_value(boot_bar, 0, LV_ANIM_OFF);

    // Helper to update boot progress
    auto updateBootProgress = [&](int percent) {
        lv_bar_set_value(boot_bar, percent, LV_ANIM_ON);
        lv_refr_now(NULL);
        lv_tick_inc(10);
        lv_timer_handler();
    };

    updateBootProgress(10);  // Initial display

    // Add global touch callback for screen wake
    lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
            resetScreenTimeout();
        }
    }, LV_EVENT_PRESSED, NULL);

    updateBootProgress(20);  // Callbacks ready

    createMainScreen();
    updateBootProgress(35);

    createDevicesScreen();
    updateBootProgress(45);

    createQueueScreen();
    updateBootProgress(55);

    createSettingsScreen();
    updateBootProgress(65);

    createDisplaySettingsScreen();
    updateBootProgress(70);

    createWiFiScreen();
    updateBootProgress(75);

    createOTAScreen();
    updateBootProgress(80);

    createSourcesScreen();
    updateBootProgress(85);

    art_mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(albumArtTask, "Art", 8192, NULL, 1, NULL, 0);  // Core 0, Priority 1 (low)
    updateBootProgress(90);

    sonos.begin();
    updateBootProgress(95);

    int cnt = sonos.discoverDevices();
    if (cnt > 0) {
        sonos.selectDevice(0);
        sonos.startTasks();
    }

    updateBootProgress(100);  // Complete!
    delay(300);  // Show 100% briefly

    lv_screen_load(scr_main);  // Now load main screen
    Serial.println("Ready!");
}

void loop() {
    lv_tick_inc(3);
    lv_timer_handler();
    processUpdates();
    checkAutoDim();  // Check if screen should be dimmed
    vTaskDelay(pdMS_TO_TICKS(3));  // More efficient than delay() - allows other tasks to run
}