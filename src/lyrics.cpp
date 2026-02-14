/**
 * Synced Lyrics Display
 * Fetches time-synced lyrics from LRCLIB and displays them overlaid on album art
 */

#include "lyrics.h"
#include "ui_common.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// Lyrics data - CRITICAL: Store in PSRAM to avoid RAM exhaustion
// 100 lines × 104 bytes = ~10.4KB - must be in PSRAM not DRAM
static LyricLine* lyric_lines = nullptr;  // Dynamically allocated in PSRAM
int lyric_count = 0;
volatile bool lyrics_ready = false;
volatile bool lyrics_fetching = false;
volatile bool lyrics_abort_requested = false;  // Abort flag for rapid track changes
int current_lyric_index = -1;

// Pending fetch parameters (use fixed buffers to avoid String allocation overhead)
static char pending_artist[128];
static char pending_title[128];
static int pending_duration = 0;
static int lyrics_retry_count = 0;  // Track retry attempts for failed fetches

// UI overlay objects
static lv_obj_t* lyrics_container = nullptr;
static lv_obj_t* lbl_lyric_prev = nullptr;
static lv_obj_t* lbl_lyric_current = nullptr;
static lv_obj_t* lbl_lyric_next = nullptr;

// Parse LRC timestamp "[MM:SS.CC]" → milliseconds
static int parseLrcTime(const char* s) {
    // Expected format: [MM:SS.CC] or [MM:SS.cc]
    if (s[0] != '[') return -1;
    int mm = 0, ss = 0, cc = 0;
    int n = sscanf(s, "[%d:%d.%d]", &mm, &ss, &cc);
    if (n < 2) return -1;
    return mm * 60000 + ss * 1000 + cc * 10;
}

// Parse synced LRC text into lyric_lines array
static void parseLRC(const String& lrc) {
    if (!lyric_lines) return;  // Buffer not allocated
    lyric_count = 0;
    int pos = 0;
    int len = lrc.length();

    while (pos < len && lyric_count < MAX_LYRIC_LINES) {
        // Find start of line
        int lineEnd = lrc.indexOf('\n', pos);
        if (lineEnd == -1) lineEnd = len;

        String line = lrc.substring(pos, lineEnd);
        line.trim();
        pos = lineEnd + 1;

        if (line.length() < 5 || line[0] != '[') continue;

        // Parse timestamp
        int bracketEnd = line.indexOf(']');
        if (bracketEnd == -1) continue;

        int time_ms = parseLrcTime(line.c_str());
        if (time_ms < 0) continue;

        // Extract text after "]"
        String text = line.substring(bracketEnd + 1);
        text.trim();

        // Skip empty lyric lines (instrumental breaks)
        if (text.length() == 0) continue;

        // Sanitize Unicode characters (reuses decodeHTML from SonosController)
        // Replaces curly quotes, accents, smart punctuation with ASCII equivalents
        text = sonos.decodeHTML(text);

        lyric_lines[lyric_count].time_ms = time_ms;
        strncpy(lyric_lines[lyric_count].text, text.c_str(), MAX_LYRIC_TEXT - 1);
        lyric_lines[lyric_count].text[MAX_LYRIC_TEXT - 1] = '\0';
        lyric_count++;
    }

    Serial.printf("[LYRICS] Parsed %d synced lines\n", lyric_count);
}

// URL-encode a string for query parameters
// Optimized: Uses fixed buffer to avoid String reallocation fragmentation
static String lyricsUrlEncode(const String& input) {
    static char encoded[512];  // Static buffer, artist/title rarely exceed 128 chars
    int out_idx = 0;

    for (int i = 0; i < (int)input.length() && out_idx < (int)sizeof(encoded) - 4; i++) {
        char c = input[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded[out_idx++] = c;
        } else if (c == ' ') {
            encoded[out_idx++] = '+';
        } else {
            int written = snprintf(&encoded[out_idx], 4, "%%%02X", (unsigned char)c);
            if (written > 0) out_idx += written;
        }
    }
    encoded[out_idx] = '\0';
    return String(encoded);
}

void initLyrics() {
    if (lyric_lines == nullptr) {
        // Allocate lyrics buffer in PSRAM to save precious DRAM
        lyric_lines = (LyricLine*)heap_caps_malloc(
            MAX_LYRIC_LINES * sizeof(LyricLine),
            MALLOC_CAP_SPIRAM
        );
        if (lyric_lines) {
            Serial.printf("[LYRICS] Allocated %d bytes in PSRAM for %d lines\n",
                MAX_LYRIC_LINES * sizeof(LyricLine), MAX_LYRIC_LINES);
        } else {
            Serial.println("[LYRICS] ERROR: Failed to allocate PSRAM for lyrics!");
        }
    }
}

// Background task: fetch lyrics from LRCLIB
static void lyricsTaskFunc(void* param) {
    // Delay to let album art finish first - reduces SDIO contention (optimized: was 1500ms, now 1000ms since HTTP is faster)
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Check abort flag (track changed during initial delay)
    if (lyrics_abort_requested) {
        Serial.println("[LYRICS] Abort requested (track changed), stopping fetch");
        lyrics_fetching = false;
        lyrics_abort_requested = false;
        lyricsTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Early exit if OTA is preparing (check own flag, not art's flag)
    if (lyrics_shutdown_requested) {
        Serial.println("[LYRICS] Shutdown requested (OTA), aborting");
        lyrics_fetching = false;
        lyricsTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    Serial.printf("[LYRICS] Fetching: %s - %s\n", pending_artist, pending_title);

    // Build URL (HTTPS required by lrclib.net)
    static char url[512];
    String artist_enc = lyricsUrlEncode(pending_artist);
    String title_enc = lyricsUrlEncode(pending_title);
    if (pending_duration > 0) {
        snprintf(url, sizeof(url), "https://lrclib.net/api/get?artist_name=%s&track_name=%s&duration=%d",
            artist_enc.c_str(), title_enc.c_str(), pending_duration);
    } else {
        snprintf(url, sizeof(url), "https://lrclib.net/api/get?artist_name=%s&track_name=%s",
            artist_enc.c_str(), title_enc.c_str());
    }

    // PRE-WAIT: Wait for cooldowns BEFORE acquiring mutex
    // This prevents blocking SOAP commands (Next/Prev/Play) during cooldown waits
    {
        unsigned long now = millis();
        unsigned long elapsed = now - last_network_end_ms;
        if (last_network_end_ms > 0 && elapsed < 200) {
            vTaskDelay(pdMS_TO_TICKS(200 - elapsed));
        }
        now = millis();
        elapsed = now - last_https_end_ms;
        if (last_https_end_ms > 0 && elapsed < 2000) {
            unsigned long wait_ms = 2000 - elapsed;
            Serial.printf("[LYRICS] HTTPS cooldown: waiting %lums (elapsed: %lums)\n", wait_ms, elapsed);
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        }
    }

    // Check abort/shutdown after cooldown (track may have changed or OTA starting)
    if (lyrics_shutdown_requested) {
        Serial.println("[LYRICS] Shutdown requested after cooldown, stopping");
        lyrics_fetching = false;
        lyricsTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (lyrics_abort_requested) {
        Serial.println("[LYRICS] Abort requested after cooldown, stopping");
        lyrics_fetching = false;
        lyrics_abort_requested = false;
        lyricsTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Acquire network mutex (shorter timeout to not block SOAP requests)
    if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(3000))) {
        Serial.println("[LYRICS] Network busy, skipping fetch");
        lyrics_fetching = false;
        // Don't call updateLyricsStatus() here - we're in a background task,
        // LVGL functions are NOT thread-safe (causes lv_inv_area assertion)
        // The main UI loop will pick up lyrics_fetching=false and update status
        lyricsTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Re-check cooldowns under mutex (another task may have used network while we waited)
    {
        unsigned long now = millis();
        unsigned long elapsed = now - last_network_end_ms;
        if (last_network_end_ms > 0 && elapsed < 200) {
            vTaskDelay(pdMS_TO_TICKS(200 - elapsed));
        }
        now = millis();
        elapsed = now - last_https_end_ms;
        if (last_https_end_ms > 0 && elapsed < 2000) {
            vTaskDelay(pdMS_TO_TICKS(2000 - elapsed));
        }
    }

    // HTTPS fetch - use scoped block to ensure WiFiClientSecure is destroyed immediately
    String payload = "";
    {
        WiFiClientSecure client;
        client.setInsecure();  // Skip certificate validation to save memory
        HTTPClient http;
        http.begin(client, url);
        http.setTimeout(10000);  // 10s timeout (lrclib.net can be slow)
        http.addHeader("User-Agent", "SonosESP/1.0");

        int code = http.GET();
        if (code == 200) {
            payload = http.getString();
            lyrics_retry_count = 0;  // Reset on success
        } else {
            // Translate HTTP client error codes to readable messages
            const char* error_msg;
            switch(code) {
                case -1: error_msg = "Connection failed"; break;
                case -2: error_msg = "Send header failed"; break;
                case -3: error_msg = "Send payload failed"; break;
                case -4: error_msg = "Not connected"; break;
                case -5: error_msg = "Connection lost/timeout"; break;
                case -6: error_msg = "No stream"; break;
                case -7: error_msg = "No HTTP server"; break;
                case -8: error_msg = "Too less RAM"; break;
                case -9: error_msg = "Encoding error"; break;
                case -10: error_msg = "Stream write error"; break;
                case -11: error_msg = "Read timeout"; break;
                default: error_msg = "Unknown error"; break;
            }
            Serial.printf("[LYRICS] HTTP %d (%s)\n", code, error_msg);
            Serial.flush();  // CRITICAL: Flush serial buffer to prevent output corruption

            // Retry logic: 2 retries with short delays (total 3 attempts)
            // Each HTTPS retry stresses SDIO and blocks art downloads via network_mutex
            lyrics_retry_count++;
            if (lyrics_retry_count < 2) {
                Serial.printf("[LYRICS] Retry %d/2 in 2s...\n", lyrics_retry_count);
            } else {
                Serial.println("[LYRICS] Max retries reached, giving up");
                lyrics_retry_count = 0;  // Reset for next track
            }
        }

        http.end();
        client.stop();
        // client and http destroyed here when leaving scope - frees TLS session
    }

    // Wait for TLS cleanup and SDIO buffer stabilization
    vTaskDelay(pdMS_TO_TICKS(200));

    // Update timestamps before releasing mutex
    last_network_end_ms = millis();
    last_https_end_ms = millis();

    xSemaphoreGive(network_mutex);

    // Check abort flag before retrying (track changed)
    if (lyrics_abort_requested) {
        Serial.println("[LYRICS] Abort requested (track changed), stopping retries");
        lyrics_fetching = false;
        lyrics_abort_requested = false;
        lyrics_retry_count = 0;  // Reset retry counter
        lyricsTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // If failed and retries remaining, spawn retry task after short delay
    if (payload.length() == 0 && lyrics_retry_count > 0 && lyrics_retry_count < 2) {
        // Fixed 2s delay (short to minimize art blocking)
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Check abort flag again after delay
        if (lyrics_abort_requested) {
            Serial.println("[LYRICS] Abort requested during retry delay, stopping");
            lyrics_fetching = false;
            lyrics_abort_requested = false;
            lyrics_retry_count = 0;
            lyricsTaskHandle = NULL;
            vTaskDelete(NULL);
            return;
        }

        // Spawn new retry task BEFORE deleting self (keep lyrics_fetching = true)
        xTaskCreatePinnedToCore(lyricsTaskFunc, "lyrics", 4096, NULL, 1, &lyricsTaskHandle, 0);
        vTaskDelete(NULL);  // Delete self, new task continues with retry
        return;
    }

    // Parse JSON response (use fixed 4KB buffer to save DRAM)
    if (payload.length() > 0) {
        DynamicJsonDocument doc(2048);  // Heap-allocated: StaticJsonDocument<4096> would overflow 4096-byte task stack
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            const char* synced = doc["syncedLyrics"];
            if (synced && strlen(synced) > 0) {
                parseLRC(String(synced));
                if (lyric_count > 0) {
                    current_lyric_index = -1;
                    lyrics_ready = true;
                    Serial.printf("[LYRICS] Ready: %d lines\n", lyric_count);
                    // Status will be updated by main UI loop
                }
            } else {
                Serial.println("[LYRICS] No synced lyrics available");
            }
        } else {
            Serial.printf("[LYRICS] JSON parse error: %s\n", err.c_str());
        }
    }

    lyrics_fetching = false;
    // Status will be updated by main UI loop
    lyricsTaskHandle = NULL;  // Clear handle before self-deletion
    vTaskDelete(NULL);
}

void requestLyrics(const String& artist, const String& title, int durationSec) {
    if (artist.length() == 0 || title.length() == 0) return;
    if (!lyric_lines) {
        Serial.println("[LYRICS] Buffer not initialized - call initLyrics() first");
        return;
    }

    // If already fetching, abort the previous task (track changed)
    if (lyrics_fetching) {
        Serial.println("[LYRICS] Track changed, aborting previous fetch");
        lyrics_abort_requested = true;
        // Wait a bit for previous task to abort (it checks the flag every delay/loop)
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Clear previous lyrics
    clearLyrics();

    // Reset abort flag and retry counter for new fetch
    lyrics_abort_requested = false;
    lyrics_retry_count = 0;

    // Store parameters for the task (copy to fixed buffers)
    strncpy(pending_artist, artist.c_str(), sizeof(pending_artist) - 1);
    pending_artist[sizeof(pending_artist) - 1] = '\0';
    strncpy(pending_title, title.c_str(), sizeof(pending_title) - 1);
    pending_title[sizeof(pending_title) - 1] = '\0';
    pending_duration = durationSec;
    lyrics_fetching = true;
    updateLyricsStatus();  // Show "fetching" status

    // Spawn one-shot background task (reduced stack: lyrics task is lightweight)
    xTaskCreatePinnedToCore(lyricsTaskFunc, "lyrics", 4096, NULL, 1, &lyricsTaskHandle, 0);
}

void clearLyrics() {
    lyrics_ready = false;
    lyric_count = 0;
    current_lyric_index = -1;
    setLyricsVisible(false);
    updateLyricsStatus();  // Clear status indicator
}

void createLyricsOverlay(lv_obj_t* parent) {
    // Semi-transparent container at bottom of album art
    lyrics_container = lv_obj_create(parent);
    lv_obj_set_size(lyrics_container, 420, 140);
    lv_obj_align(lyrics_container, LV_ALIGN_BOTTOM_MID, 0, -24);  // Adjusted to prevent overlap
    lv_obj_set_style_bg_color(lyrics_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lyrics_container, 180, 0);
    lv_obj_set_style_border_width(lyrics_container, 0, 0);
    lv_obj_set_style_border_opa(lyrics_container, 0, 0);      // Force no border
    lv_obj_set_style_outline_width(lyrics_container, 0, 0);
    lv_obj_set_style_outline_opa(lyrics_container, 0, 0);     // Force no outline
    lv_obj_set_style_shadow_width(lyrics_container, 0, 0);    // No shadow
    lv_obj_set_style_shadow_opa(lyrics_container, 0, 0);      // Force no shadow
    lv_obj_set_style_radius(lyrics_container, 0, 0);
    lv_obj_set_style_pad_top(lyrics_container, 8, 0);
    lv_obj_set_style_pad_bottom(lyrics_container, 4, 0);      // Small padding to avoid edge artifact
    lv_obj_set_style_pad_left(lyrics_container, 8, 0);
    lv_obj_set_style_pad_right(lyrics_container, 8, 0);
    lv_obj_clear_flag(lyrics_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(lyrics_container, LV_SCROLLBAR_MODE_OFF);

    // Flex column layout, centered
    lv_obj_set_flex_flow(lyrics_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lyrics_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Previous line (dimmed, scroll if too long)
    lbl_lyric_prev = lv_label_create(lyrics_container);
    lv_label_set_text(lbl_lyric_prev, "");
    lv_obj_set_width(lbl_lyric_prev, 400);
    lv_obj_set_style_text_font(lbl_lyric_prev, &lv_font_montserrat_14, 0);  // Smaller size
    lv_obj_set_style_text_color(lbl_lyric_prev, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(lbl_lyric_prev, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyric_prev, LV_LABEL_LONG_SCROLL_CIRCULAR);  // Scroll effect

    // Current line (bright, BIGGER, scroll if too long)
    lbl_lyric_current = lv_label_create(lyrics_container);
    lv_label_set_text(lbl_lyric_current, "");
    lv_obj_set_width(lbl_lyric_current, 400);
    lv_obj_set_style_text_font(lbl_lyric_current, &lv_font_montserrat_20, 0);  // BIGGER for current!
    lv_obj_set_style_text_color(lbl_lyric_current, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(lbl_lyric_current, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyric_current, LV_LABEL_LONG_SCROLL_CIRCULAR);  // Scroll effect

    // Next line (dimmed, scroll if too long)
    lbl_lyric_next = lv_label_create(lyrics_container);
    lv_label_set_text(lbl_lyric_next, "");
    lv_obj_set_width(lbl_lyric_next, 400);
    lv_obj_set_style_text_font(lbl_lyric_next, &lv_font_montserrat_14, 0);  // Smaller size
    lv_obj_set_style_text_color(lbl_lyric_next, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(lbl_lyric_next, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyric_next, LV_LABEL_LONG_SCROLL_CIRCULAR);  // Scroll effect

    // Start hidden
    lv_obj_add_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
}

// Animation callback for fade effect
static void lyrics_fade_cb(void* var, int32_t v) {
    if (lyrics_container) {
        lv_obj_set_style_opa(lyrics_container, v, 0);
    }
}

void updateLyricsDisplay(int position_seconds) {
    if (!lyrics_container || !lyric_lines) return;  // Check buffer allocated
    if (!lyrics_ready || !lyrics_enabled || lyric_count == 0) {
        if (!lv_obj_has_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    int pos_ms = position_seconds * 1000;

    // Find current line (last line where time_ms <= pos_ms)
    int idx = -1;
    for (int i = 0; i < lyric_count; i++) {
        if (lyric_lines[i].time_ms <= pos_ms) {
            idx = i;
        } else {
            break;
        }
    }

    // No line yet (before first lyric) - hide container
    if (idx < 0) {
        if (!lv_obj_has_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Auto-hide logic: Hide lyrics 10 seconds after current line if there's a gap before next line
    int time_since_current = pos_ms - lyric_lines[idx].time_ms;

    // Check if we're past the last lyric (more than 3 seconds after) - hide container
    if (idx == lyric_count - 1) {  // On last lyric
        if (time_since_current > 3000) {  // 3 seconds after last lyric
            if (!lv_obj_has_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
    } else {
        // Check if next lyric is more than 10 seconds away
        int time_to_next = lyric_lines[idx + 1].time_ms - pos_ms;

        // If we've shown current lyric for 10+ seconds AND next lyric is still far away, hide
        if (time_since_current >= 10000 && time_to_next > 0) {
            if (!lv_obj_has_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
    }

    // Show container if hidden
    if (lv_obj_has_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(lyrics_container, 255, 0);
    }

    // Only update when line changes
    if (idx == current_lyric_index) return;

    int prev_index = current_lyric_index;
    current_lyric_index = idx;

    // Update text
    lv_label_set_text(lbl_lyric_prev, idx > 0 ? lyric_lines[idx - 1].text : "");
    lv_label_set_text(lbl_lyric_current, lyric_lines[idx].text);
    lv_label_set_text(lbl_lyric_next, idx < lyric_count - 1 ? lyric_lines[idx + 1].text : "");

    // Fade animation on line change
    if (prev_index >= 0) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, lyrics_container);
        lv_anim_set_values(&anim, 150, 255);
        lv_anim_set_duration(&anim, 150);
        lv_anim_set_exec_cb(&anim, lyrics_fade_cb);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }

    // Color current line with brightened dominant color (brighter than progress bar)
    uint8_t r = (dominant_color >> 16) & 0xFF;
    uint8_t g = (dominant_color >> 8) & 0xFF;
    uint8_t b = dominant_color & 0xFF;
    r = (uint8_t)max(min((int)r * 4, 255), 120);  // Brighter: 4x multiplier, floor 120
    g = (uint8_t)max(min((int)g * 4, 255), 120);
    b = (uint8_t)max(min((int)b * 4, 255), 120);
    lv_obj_set_style_text_color(lbl_lyric_current, lv_color_make(r, g, b), 0);
}

void setLyricsVisible(bool show) {
    if (!lyrics_container) return;
    if (show && lyrics_ready && lyric_count > 0) {
        lv_obj_remove_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void updateLyricsStatus() {
    extern lv_obj_t *lbl_lyrics_status;
    extern bool lyrics_enabled;

    if (!lbl_lyrics_status) return;
    if (!lyrics_enabled) {
        lv_label_set_text(lbl_lyrics_status, "");  // Hide when disabled
        return;
    }

    // Only show status when fetching, hide otherwise
    if (lyrics_fetching) {
        lv_label_set_text(lbl_lyrics_status, "Fetching lyrics...");
        lv_obj_set_style_text_color(lbl_lyrics_status, lv_color_hex(0x666666), 0);  // Dark gray, subtle
    } else {
        lv_label_set_text(lbl_lyrics_status, "");  // Hide when ready or no lyrics
    }
}
