/**
 * Synced Lyrics Display
 * Fetches time-synced lyrics from LRCLIB and displays them overlaid on album art
 */

#include "lyrics.h"
#include "ui_common.h"
#include "config.h"
#include "ui_network_guard.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
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
static char lyrics_status_msg[64] = "";         // Status shown briefly after fetch completes
static unsigned long lyrics_status_until_ms = 0; // Show status until this timestamp
// Track which song already failed so requestLyrics() doesn't re-spawn for same track.
// Cleared when a genuinely new track is detected (different artist or title).
static char lyrics_failed_artist[128] = "";
static char lyrics_failed_title[128]  = "";

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

// Background task: fetch lyrics from LRCLIB.
// Retry loop is internal (no self-spawn) so a single PSRAM stack can be reused safely.
static void lyricsTaskFunc(void* param) {
    // Initial delay: lets album art start first, reduces SDIO contention
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (lyrics_abort_requested || lyrics_shutdown_requested) {
        lyrics_fetching = false;
        lyrics_abort_requested = false;
        lyricsTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    Serial.printf("[LYRICS] Fetching: %s - %s\n", pending_artist, pending_title);

    // Build URL once — same URL for all retry attempts
    static char url[512];
    {
        String artist_enc = lyricsUrlEncode(pending_artist);
        String title_enc  = lyricsUrlEncode(pending_title);
        if (pending_duration > 0) {
            snprintf(url, sizeof(url),
                "https://lrclib.net/api/get?artist_name=%s&track_name=%s&duration=%d",
                artist_enc.c_str(), title_enc.c_str(), pending_duration);
        } else {
            snprintf(url, sizeof(url),
                "https://lrclib.net/api/get?artist_name=%s&track_name=%s",
                artist_enc.c_str(), title_enc.c_str());
        }
    }

    String payload = "";

    // Retry loop: max 2 attempts for transient errors.
    // HTTP -1 (connection failed) and 404 (no lyrics) give up immediately.
    // No self-spawn: loops internally so the single PSRAM stack is reused safely.
    while (true) {
        // PRE-WAIT cooldowns outside mutex so SOAP commands aren't blocked

        // Wait for art download to finish before HTTP fetch (concurrent art + lyrics overflows pkt_rxbuff)
        {
            unsigned long _now = millis();
            Serial.printf("[LYRICS] PRE: art_dl=%d 500=%ldms net=%ldms art_end=%ldms heap=%u dma=%u stk=%u\n",
                (int)art_download_in_progress,
                last_transient_500_ms    ? (long)(_now - last_transient_500_ms)    : -1L,
                last_network_end_ms      ? (long)(_now - last_network_end_ms)      : -1L,
                last_art_download_end_ms ? (long)(_now - last_art_download_end_ms) : -1L,
                heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                heap_caps_get_free_size(MALLOC_CAP_DMA),
                uxTaskGetStackHighWaterMark(NULL));
        }
        {
            unsigned long wait_start = millis();
            while (art_download_in_progress) {
                if (lyrics_abort_requested || lyrics_shutdown_requested) break;
                if (millis() - wait_start > LYRICS_ART_WAIT_TIMEOUT_MS) break;  // safety timeout
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            unsigned long art_waited = millis() - wait_start;
            if (art_waited > 50)
                Serial.printf("[LYRICS] Art-wait done: %lums (art_dl=%d)\n", art_waited, (int)art_download_in_progress);
        }

        // SDIO crash-defence: HTTPS cooldown + general cooldown.
        // First HTTPS session triggers ~77KB one-time mbedTLS global init (cipher tables,
        // entropy, RNG state). Subsequent sessions reuse this state and need only ~5KB DMA.
        // DMA floor after first lyrics fetch: ~20KB (97KB post-WiFi − 77KB mbedTLS).
        // SDIO_WAIT_HTTPS_COOLDOWN ensures 3s gap after any previous HTTPS session.
        if (!sdioPreWait("LYRICS", SDIO_WAIT_HTTPS_COOLDOWN, &lyrics_abort_requested, &lyrics_shutdown_requested)) {
            lyrics_fetching = false;
            lyrics_abort_requested = false;
            lyrics_retry_count = 0;
            lyricsTaskHandle = NULL;
            vTaskDelete(NULL);
            return;
        }

        // Re-check art_download_in_progress after sdioPreWait:
        // (1) requestLyrics() is called BEFORE requestAlbumArt() sets the flag in the same
        //     updateUI() call — lyrics task may start and pass the while loop above before
        //     the flag is set. (2) Track may change and set the flag mid-sdioPreWait.
        // Either way: loop back and wait for art to finish before attempting HTTPS.
        if (art_download_in_progress) {
            Serial.println("[LYRICS] Art download in progress after pre-wait — retrying after art");
            continue;
        }

        // Acquire mutex (3s timeout — short so SOAP commands aren't blocked for long)
        uint32_t t_lyr_mutex = millis();
        if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(3000))) {
            Serial.println("[LYRICS] Network busy, skipping fetch");
            lyrics_fetching = false;
            lyricsTaskHandle = NULL;
            vTaskDelete(NULL);
            return;
        }

        Serial.printf("[LYRICS] Mutex acquired after %lums | heap=%u dma=%u\n",
            millis() - t_lyr_mutex,
            heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
            heap_caps_get_free_size(MALLOC_CAP_DMA));
        // Inside-mutex check: final race guard. Track change may have set art_download_in_progress
        // between the recheck above and mutex acquisition (tiny window). Release and loop back.
        if (art_download_in_progress) {
            Serial.println("[LYRICS] Art download started while acquiring mutex — retrying after art");
            xSemaphoreGive(network_mutex);
            continue;
        }

        // Inside-mutex rechecks: TCP drain time only (full cooldown already ran in sdioPreWait).
        {
            unsigned long now = millis();
            unsigned long elapsed_net  = now - last_network_end_ms;
            unsigned long elapsed_https = now - last_https_end_ms;
            if (last_network_end_ms > 0 && elapsed_net < SDIO_GENERAL_COOLDOWN_MS)
                vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - elapsed_net));
            if (last_https_end_ms > 0 && elapsed_https < SDIO_HTTPS_TCP_CLOSE_MS)
                vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_TCP_CLOSE_MS - elapsed_https));
        }

        // DMA guard: esp-aes needs at least ~5KB DMA for HTTPS session.
        // Confirmed failure: dma=10,980 total → "Failed to allocate DMA descriptors".
        // NOTE: heap_caps_get_largest_free_block(MALLOC_CAP_DMA) returns wrong values
        // on ESP32-P4 (4-16 bytes even at 96KB free) — use get_free_size only.
        // Art auto-restart fires at 20s DMA floor, so this guard is belt-and-suspenders.
        {
            size_t dma_total = heap_caps_get_free_size(MALLOC_CAP_DMA);
            if (dma_total < LYRICS_MIN_FREE_DMA) {
                Serial.printf("[LYRICS] DMA too low (%u < %u) — skipping fetch\n",
                              (unsigned)dma_total, (unsigned)LYRICS_MIN_FREE_DMA);
                xSemaphoreGive(network_mutex);
                lyrics_fetching = false;
                lyricsTaskHandle = NULL;
                vTaskDelete(NULL);
                return;
            }
        }

        // HTTPS fetch — local WiFiClientSecure per song: TLS buffers (~32KB DMA) allocated for
        // handshake and freed by destructor at scope exit. Permanent loss ~6.8KB (fragmentation)
        // vs ~32KB if kept static. Critical: with static + setReuse the TLS record buffers are
        // never released → session DMA floor drops ~32KB extra → Song 2+ pre-GET too low → crash.
        {
            Serial.printf("[LYRICS] HTTPS open → lrclib.net | heap=%u dma=%u\n",
                heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                heap_caps_get_free_size(MALLOC_CAP_DMA));
            WiFiClientSecure client;
            client.setInsecure();
            HTTPClient http;
            http.begin(client, url);
            http.setTimeout(10000);
            http.addHeader("User-Agent", "SonosESP/1.0");

            uint32_t t_lyr_get = millis();
            int code = http.GET();
            Serial.printf("[LYRICS] HTTPS ← code=%d in %lums\n", code, millis() - t_lyr_get);
            if (code == 200) {
                payload = http.getString();
                Serial.printf("[LYRICS] Payload: %u bytes\n", (unsigned)payload.length());
                lyrics_retry_count = 0;
            } else {
                const char* error_msg;
                switch (code) {
                    case -1:  error_msg = "Connection failed"; break;
                    case -2:  error_msg = "Send header failed"; break;
                    case -3:  error_msg = "Send payload failed"; break;
                    case -4:  error_msg = "Not connected"; break;
                    case -5:  error_msg = "Connection lost/timeout"; break;
                    case -6:  error_msg = "No stream"; break;
                    case -7:  error_msg = "No HTTP server"; break;
                    case -8:  error_msg = "Too less RAM"; break;
                    case -9:  error_msg = "Encoding error"; break;
                    case -10: error_msg = "Stream write error"; break;
                    case -11: error_msg = "Read timeout"; break;
                    default:  error_msg = "Unknown error"; break;
                }
                Serial.printf("[LYRICS] HTTP %d (%s)\n", code, error_msg);
                Serial.flush();

                if (code == -1 || code == 404) {
                    // No retry: -1 = connection failed, 404 = no lyrics exist
                    Serial.printf("[LYRICS] No retry for HTTP %d — marking song as failed\n", code);
                    strncpy(lyrics_failed_artist, pending_artist, sizeof(lyrics_failed_artist) - 1);
                    lyrics_failed_artist[sizeof(lyrics_failed_artist) - 1] = '\0';
                    strncpy(lyrics_failed_title, pending_title, sizeof(lyrics_failed_title) - 1);
                    lyrics_failed_title[sizeof(lyrics_failed_title) - 1] = '\0';
                    lyrics_retry_count = 0;
                } else {
                    lyrics_retry_count++;
                    if (lyrics_retry_count < 2) {
                        Serial.printf("[LYRICS] Retry %d/2 in 2s...\n", lyrics_retry_count);
                    } else {
                        Serial.println("[LYRICS] Max retries reached, giving up");
                        strncpy(lyrics_failed_artist, pending_artist, sizeof(lyrics_failed_artist) - 1);
                        lyrics_failed_artist[sizeof(lyrics_failed_artist) - 1] = '\0';
                        strncpy(lyrics_failed_title, pending_title, sizeof(lyrics_failed_title) - 1);
                        lyrics_failed_title[sizeof(lyrics_failed_title) - 1] = '\0';
                        lyrics_retry_count = 0;
                    }
                }
            }

            // Wait up to 200ms for server FIN (passive close → server gets TIME_WAIT, not us).
            if (WiFiClient* s = http.getStreamPtr()) {
                for (int i = 0; i < 200 && s->connected(); i++)
                    vTaskDelay(pdMS_TO_TICKS(1));
            }
            http.end();
            client.stop();  // Explicitly free mbedTLS record buffers (~32KB DMA) before destructor.
                            // Without this, TLS in/out record buffers linger until scope exit but
                            // destructor ordering is non-deterministic vs. HTTPClient destruction.
            Serial.printf("[LYRICS] HTTPS closed | heap=%u dma=%u\n",
                heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                heap_caps_get_free_size(MALLOC_CAP_DMA));
        }  // WiFiClientSecure and HTTPClient destroyed here; TLS buffers already freed by stop()

        // Cleanup wait + timestamp update before releasing mutex
        vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_TCP_CLOSE_MS));  // drain TLS FIN-ACK
        last_network_end_ms = millis();
        last_https_end_ms   = millis();
        Serial.printf("[LYRICS] Mutex releasing | heap=%u dma=%u\n",
            heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
            heap_caps_get_free_size(MALLOC_CAP_DMA));
        xSemaphoreGive(network_mutex);

        // Abort check after mutex release
        if (lyrics_abort_requested) {
            lyrics_fetching = false;
            lyrics_abort_requested = false;
            lyrics_retry_count = 0;
            lyricsTaskHandle = NULL;
            vTaskDelete(NULL);
            return;
        }

        // Exit loop if we have a response OR no retries remain
        if (payload.length() > 0) break;
        if (lyrics_retry_count == 0) break;  // gave up (marked as failed or -1/404)

        // Transient error: wait before retry
        vTaskDelay(pdMS_TO_TICKS(LYRICS_RETRY_DELAY_MS));
        if (lyrics_abort_requested) {
            lyrics_fetching = false;
            lyrics_abort_requested = false;
            lyrics_retry_count = 0;
            lyricsTaskHandle = NULL;
            vTaskDelete(NULL);
            return;
        }
    }

    // Parse JSON response
    if (payload.length() > 0) {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            const char* synced = doc["syncedLyrics"];
            if (synced && strlen(synced) > 0) {
                parseLRC(String(synced));
                if (lyric_count > 0) {
                    current_lyric_index = -1;
                    lyrics_ready = true;
                    Serial.printf("[LYRICS] Ready: %d lines\n", lyric_count);
                    strncpy(lyrics_status_msg, "Synced lyrics available", sizeof(lyrics_status_msg) - 1);
                }
            } else {
                Serial.println("[LYRICS] No synced lyrics available");
                strncpy(lyrics_status_msg, "No synced lyrics", sizeof(lyrics_status_msg) - 1);
            }
        } else {
            Serial.printf("[LYRICS] JSON parse error: %s\n", err.c_str());
            strncpy(lyrics_status_msg, "No lyrics", sizeof(lyrics_status_msg) - 1);
        }
    } else {
        strncpy(lyrics_status_msg, "No lyrics", sizeof(lyrics_status_msg) - 1);
    }

    lyrics_status_msg[sizeof(lyrics_status_msg) - 1] = '\0';
    lyrics_status_until_ms = millis() + 5000;

    lyrics_fetching = false;
    lyricsTaskHandle = NULL;
    vTaskDelete(NULL);
}

bool requestLyrics(const String& artist, const String& title, int durationSec) {
    if (artist.length() == 0 || title.length() == 0) return false;
    if (!lyric_lines) {
        Serial.println("[LYRICS] Buffer not initialized - call initLyrics() first");
        return false;
    }

    bool sameTrack = (strcmp(artist.c_str(), pending_artist) == 0 &&
                      strcmp(title.c_str(), pending_title) == 0);

    // If this exact song already failed (network error / no lyrics), don't retry.
    // SOAP polls every 300ms for the same song — without this guard, every poll would
    // spawn a new lyrics task, creating an HTTPS retry storm that exhausts C6 SDIO buffers.
    if (strcmp(artist.c_str(), lyrics_failed_artist) == 0 &&
        strcmp(title.c_str(), lyrics_failed_title) == 0) {
        return false;  // Already attempted this track — skip silently
    }

    // New song: clear the failed-song record so it gets a fresh attempt
    if (!sameTrack) {
        lyrics_failed_artist[0] = '\0';
        lyrics_failed_title[0]  = '\0';
    }

    // CRITICAL: If the previous task is still running (lyricsTaskHandle != NULL), do NOT
    // spawn a new task yet. Spawning with the same lyrics_task_stack and lyricsTaskTCB
    // while the old task is alive (e.g. blocked inside http.GET() with up to 10s timeout)
    // causes FreeRTOS to re-initialise the TCB in-place. The old task is zombified: its
    // stack-allocated WiFiClientSecure is never destructed → ~32KB TLS DMA session buffers
    // are permanently leaked. After 2-3 rapid track changes: DMA floor drops ~64-96KB →
    // art pre-GET check fails → no album art loads for any subsequent song.
    //
    // Fix: signal abort + update pending parameters, but return false without spawning.
    // Caller (updateUI) does NOT update lyrics_last_track when we return false, so the
    // condition fires again next frame. Old task exits when its current network call
    // completes or times out (≤ 10s) and sees lyrics_abort_requested → cleans up TLS
    // properly → sets lyricsTaskHandle = NULL. Next frame: we spawn normally.
    if (lyricsTaskHandle != NULL) {
        Serial.println("[LYRICS] Previous task still running — aborting, will retry next frame");
        lyrics_abort_requested = true;
        clearLyrics();  // Clear old song lyrics from display immediately
        // NOTE: do NOT write pending_artist/title here — the old task may still be
        // reading them to build its URL (data race on the shared fixed buffers, H-3).
        // The caller does not advance lyrics_last_track on a false return, so it calls
        // again next frame; once the old task exits we copy the CURRENT song fresh in
        // the spawn path below. Skipping the copy here costs nothing and removes the race.
        return false;  // Caller should NOT update lyrics_last_track; retry next frame
    }

    // No previous task running — proceed with spawn.

    // Clear previous lyrics and reset state
    clearLyrics();
    lyrics_abort_requested = false;
    lyrics_retry_count = 0;
    lyrics_status_msg[0] = '\0';
    lyrics_status_until_ms = 0;

    // Store parameters for the task (copy to fixed buffers)
    strncpy(pending_artist, artist.c_str(), sizeof(pending_artist) - 1);
    pending_artist[sizeof(pending_artist) - 1] = '\0';
    strncpy(pending_title, title.c_str(), sizeof(pending_title) - 1);
    pending_title[sizeof(pending_title) - 1] = '\0';
    pending_duration = durationSec;
    lyrics_fetching = true;
    updateLyricsStatus();  // Show "fetching" status

    // Spawn one-shot background task with PSRAM stack to save DMA SRAM
    if (!lyrics_task_stack)
        lyrics_task_stack = (StackType_t*)heap_caps_malloc(LYRICS_TASK_STACK, MALLOC_CAP_SPIRAM);
    if (lyrics_task_stack) {
        lyricsTaskHandle = xTaskCreateStaticPinnedToCore(
            lyricsTaskFunc, "lyrics", LYRICS_TASK_STACK / sizeof(StackType_t),
            NULL, LYRICS_TASK_PRIORITY, lyrics_task_stack, &lyricsTaskTCB, 0);
    } else {
        Serial.println("[LYRICS] PSRAM stack alloc failed — using internal SRAM");
        xTaskCreatePinnedToCore(lyricsTaskFunc, "lyrics", LYRICS_TASK_STACK, NULL, LYRICS_TASK_PRIORITY, &lyricsTaskHandle, 0);
    }
    return (lyricsTaskHandle != NULL);
}

void clearLyrics() {
    lyrics_ready = false;
    lyric_count = 0;
    current_lyric_index = -1;
    setLyricsVisible(false);
    updateLyricsStatus();  // Clear status indicator
}

void createLyricsOverlay(lv_obj_t* parent) {
    // Gradient overlay at bottom of album art — transparent top, semi-opaque black bottom
    lyrics_container = lv_obj_create(parent);
    lv_obj_set_size(lyrics_container, SMIN(420), SY(180));  // width tracks the album art (SMIN(ART_SIZE))
    // Centered in 450px panel (x=15), bottom aligned with art bottom (y=270..450)
    lv_obj_align(lyrics_container, LV_ALIGN_BOTTOM_MID, 0, SY(-30));
    // Vertical gradient: transparent at top, dark semi-opaque at bottom
    lv_obj_set_style_bg_opa(lyrics_container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(lyrics_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(lyrics_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_dir(lyrics_container, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(lyrics_container, LV_OPA_TRANSP, 0);   // top: transparent
    lv_obj_set_style_bg_grad_opa(lyrics_container, 245, 0);              // bottom: ~96% opaque
    lv_obj_set_style_border_width(lyrics_container, 0, 0);
    lv_obj_set_style_outline_width(lyrics_container, 0, 0);
    lv_obj_set_style_shadow_width(lyrics_container, 0, 0);
    lv_obj_set_style_radius(lyrics_container, 24, 0);     // match album art corner radius
    lv_obj_set_style_clip_corner(lyrics_container, true, 0);
    lv_obj_set_style_pad_top(lyrics_container, 24, 0);    // extra top pad — fades into art
    lv_obj_set_style_pad_bottom(lyrics_container, 12, 0);
    lv_obj_set_style_pad_left(lyrics_container, 12, 0);
    lv_obj_set_style_pad_right(lyrics_container, 12, 0);
    lv_obj_clear_flag(lyrics_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(lyrics_container, LV_SCROLLBAR_MODE_OFF);

    // Flex column layout, centered
    lv_obj_set_flex_flow(lyrics_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lyrics_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Previous line — dimmed
    lbl_lyric_prev = lv_label_create(lyrics_container);
    lv_label_set_text(lbl_lyric_prev, "");
    lv_obj_set_width(lbl_lyric_prev, SMIN(396));
    lv_obj_set_style_text_font(lbl_lyric_prev, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_lyric_prev, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(lbl_lyric_prev, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyric_prev, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Current line — bright, larger
    lbl_lyric_current = lv_label_create(lyrics_container);
    lv_label_set_text(lbl_lyric_current, "");
    lv_obj_set_width(lbl_lyric_current, SMIN(396));
    lv_obj_set_style_text_font(lbl_lyric_current, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_lyric_current, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(lbl_lyric_current, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyric_current, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Next line — dimmed
    lbl_lyric_next = lv_label_create(lyrics_container);
    lv_label_set_text(lbl_lyric_next, "");
    lv_obj_set_width(lbl_lyric_next, SMIN(396));
    lv_obj_set_style_text_font(lbl_lyric_next, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_lyric_next, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(lbl_lyric_next, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_lyric_next, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Start hidden
    lv_obj_add_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
}

static void lyrics_fade_cb(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    if (obj) lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
}

// Fade-out complete: hide the container
static void lyrics_fadeout_done_cb(lv_anim_t* a) {
    lv_obj_t* obj = (lv_obj_t*)a->var;
    if (obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// Smooth 300ms fade-out then hide
static void lyricsHide() {
    if (!lyrics_container || lv_obj_has_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN)) return;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, lyrics_container);
    lv_anim_set_values(&anim, lv_obj_get_style_opa(lyrics_container, LV_PART_MAIN), LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, 300);
    lv_anim_set_exec_cb(&anim, lyrics_fade_cb);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&anim, lyrics_fadeout_done_cb);
    lv_anim_start(&anim);
}

// Instant show — cancel any fade-out, snap to full opacity
static void lyricsShow() {
    if (!lyrics_container) return;
    lv_anim_del(lyrics_container, lyrics_fade_cb);
    lv_obj_remove_flag(lyrics_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(lyrics_container, LV_OPA_COVER, 0);
}

void updateLyricsDisplay(int position_seconds) {
    if (!lyrics_container || !lyric_lines) return;  // Check buffer allocated
    if (!lyrics_ready || !lyrics_enabled || lyric_count == 0) {
        lyricsHide();
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

    // No line yet (before first lyric)
    if (idx < 0) {
        lyricsHide();
        return;
    }

    int time_since_current = pos_ms - lyric_lines[idx].time_ms;

    // Past last lyric by 3 seconds — fade out
    if (idx == lyric_count - 1 && time_since_current > 3000) {
        lyricsHide();
        return;
    }

    // Long gap before next lyric (10s shown, next still far) — fade out
    if (idx < lyric_count - 1) {
        int time_to_next = lyric_lines[idx + 1].time_ms - pos_ms;
        if (time_since_current >= 10000 && time_to_next > 0) {
            lyricsHide();
            return;
        }
    }

    // Instant snap-in — cancel any ongoing fade-out
    lyricsShow();

    // Only update text when line changes
    if (idx == current_lyric_index) return;

    current_lyric_index = idx;

    // Snap text instantly (no fade-in on line change)
    lv_label_set_text(lbl_lyric_prev, idx > 0 ? lyric_lines[idx - 1].text : "");
    lv_label_set_text(lbl_lyric_current, lyric_lines[idx].text);
    lv_label_set_text(lbl_lyric_next, idx < lyric_count - 1 ? lyric_lines[idx + 1].text : "");

    // Color current line with vivid dominant color: normalize to max=255, then floor at 200
    uint8_t r = (dominant_color >> 16) & 0xFF;
    uint8_t g = (dominant_color >> 8) & 0xFF;
    uint8_t b = dominant_color & 0xFF;
    int max_ch = max({(int)r, (int)g, (int)b});
    if (max_ch > 0) {
        r = (uint8_t)min(255, (int)r * 255 / max_ch);
        g = (uint8_t)min(255, (int)g * 255 / max_ch);
        b = (uint8_t)min(255, (int)b * 255 / max_ch);
    }
    r = max(r, (uint8_t)200);
    g = max(g, (uint8_t)200);
    b = max(b, (uint8_t)200);
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

    if (lyrics_fetching) {
        // Animate dots: cycles "Fetching lyrics." → ".." → "..." every 500ms
        static const char* dot_frames[] = { "Fetching lyrics.", "Fetching lyrics..", "Fetching lyrics..." };
        int frame = (millis() / 500) % 3;
        lv_label_set_text(lbl_lyrics_status, dot_frames[frame]);
        lv_obj_set_style_text_color(lbl_lyrics_status, lv_color_hex(0x666666), 0);
    } else if (lyrics_status_msg[0] != '\0' && millis() < lyrics_status_until_ms) {
        // Show result status briefly after fetch completes (5 seconds)
        lv_label_set_text(lbl_lyrics_status, lyrics_status_msg);
        lv_obj_set_style_text_color(lbl_lyrics_status, lv_color_hex(0x666666), 0);
    } else {
        lv_label_set_text(lbl_lyrics_status, "");
    }
}
