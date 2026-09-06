/**
 * UI Event Handlers and Utilities
 * All event callbacks, WiFi, OTA, brightness control, and UI update functions
 */

#include "ui_common.h"
#include "ui_icons.h"
#include <vector>
#include "config.h"
#include "ui_theme.h"
#include "lyrics.h"
#include "clock_screen.h"
#include <esp_task_wdt.h>
#include "ui_fonts.h"

// ============================================================================
// Brightness Control
// ============================================================================
void setBrightness(int level) {
    brightness_level = constrain(level, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
    display_set_brightness(brightness_level);
    wifiPrefs.putInt(NVS_KEY_BRIGHTNESS, brightness_level);
}

void resetScreenTimeout() {
    last_touch_time = millis();
    if (screen_dimmed) {
        // Instant wake-up - no animation
        display_set_brightness(brightness_level);
        screen_dimmed = false;
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
    }
}

// ============================================================================
// Playback Event Handlers
// ============================================================================
void ev_play(lv_event_t* e) {
    SonosDevice* d = sonos.getCurrentDevice();
    if (d) d->isPlaying ? sonos.pause() : sonos.play();
}

void ev_prev(lv_event_t* e) {
    sonos.previous();
}

void ev_next(lv_event_t* e) {
    sonos.next();
}

void ev_shuffle(lv_event_t* e) {
    SonosDevice* d = sonos.getCurrentDevice();
    if (d) sonos.setShuffle(!d->shuffleMode);
}

void ev_repeat(lv_event_t* e) {
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) return;
    if (d->repeatMode == "NONE") sonos.setRepeat("ALL");
    else if (d->repeatMode == "ALL") sonos.setRepeat("ONE");
    else sonos.setRepeat("NONE");
}

void ev_progress(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSING) dragging_prog = true;
    else if (code == LV_EVENT_RELEASED) {
        SonosDevice* d = sonos.getCurrentDevice();
        if (d && d->durationSeconds > 0) sonos.seek((lv_slider_get_value(slider_progress) * d->durationSeconds) / 100);
        dragging_prog = false;
    }
}

void ev_vol_slider(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSING) dragging_vol = true;
    else if (code == LV_EVENT_RELEASED) {
        sonos.setVolume(lv_slider_get_value(slider_vol));
        dragging_vol = false;
    }
}

void ev_mute(lv_event_t* e) {
    SonosDevice* d = sonos.getCurrentDevice();
    if (d) sonos.setMute(!d->isMuted);
}

void ev_queue_item(lv_event_t* e) {
    static uint32_t last_click_ms = 0;
    uint32_t now = millis();
    if (now - last_click_ms < 1500) return;  // debounce: ignore rapid repeat taps
    last_click_ms = now;
    int trackNum = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    sonos.playQueueItem(trackNum);
    lv_screen_load(scr_main);
}

// ============================================================================
// Navigation Event Handlers
// ============================================================================
void ev_devices(lv_event_t* e) {
    // Amber draws Rooms as a modal over the player. Returns false for every
    // other theme, which then loads the Speakers screen as before.
    if (amberShowRooms()) return;
    lv_screen_load(scr_devices);
}

void ev_queue(lv_event_t* e) {
    // Show cached data immediately, then request a fresh windowed fetch.
    // The polling task picks up queue_fetch_requested and calls updateQueue(startIndex)
    // on its next cycle (safe: no SOAP on UI thread).
    SonosDevice* d = sonos.getCurrentDevice();
    int start = 0;
    if (d && d->currentTrackNumber > 0) {
        start = d->currentTrackNumber - SONOS_QUEUE_BATCH_SIZE / 2;
        if (start < 0) start = 0;
        if (d->totalTracks > 0 && start + SONOS_QUEUE_BATCH_SIZE > d->totalTracks)
            start = d->totalTracks - SONOS_QUEUE_BATCH_SIZE;
        if (start < 0) start = 0;
    }
    queue_fetch_start_index = start;
    queue_fetch_requested   = true;
    refreshQueueList();
    // Amber draws the queue as a drawer over the player. The windowed fetch above
    // is requested either way, so the drawer and the screen show the same data.
    if (amberShowQueue()) return;
    lv_screen_load(scr_queue);
}

void ev_settings(lv_event_t* e) {
    // General, not Speakers. createSettingsScreen() aliases scr_settings to
    // scr_devices because it runs BEFORE createGeneralScreen() and needs a screen
    // that already exists; by the time anyone taps the gear, General is built.
    // The design canvas opens Settings on General, and it is the page whose
    // controls are least likely to need a network round-trip to be meaningful.
    lv_screen_load(scr_general ? scr_general : scr_settings);
}

void ev_back_main(lv_event_t* e) {
    lv_screen_load(scr_main);
}

void ev_back_settings(lv_event_t* e) {
    lv_screen_load(scr_settings);
}

void ev_groups(lv_event_t* e) {
    sonos.updateGroupInfo();
    refreshGroupsList();
    lv_screen_load(scr_groups);
}

// ============================================================================
// Speaker Discovery Event Handler
// ============================================================================
void ev_discover(lv_event_t* e) {
    Serial.println("[SCAN] Scan button pressed");

    // Disable scan button during discovery
    if (btn_sonos_scan) {
        lv_obj_add_state(btn_sonos_scan, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_sonos_scan, COL_BORDER, LV_STATE_DISABLED);
    }

    // Show spinner
    if (spinner_scan) {
        Serial.println("[SCAN] Showing spinner");
        lv_obj_remove_flag(spinner_scan, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(spinner_scan);  // Bring to front
    } else {
        Serial.println("[SCAN] ERROR: spinner_scan is NULL!");
    }

    lv_label_set_text(lbl_status, "Scanning for speakers...");
    lv_obj_set_style_text_color(lbl_status, COL_ACCENT, 0);
    lv_obj_clean(list_devices);
    lv_refr_now(NULL);  // Force immediate screen refresh

    int cnt = sonos.discoverDevices();

    // Hide spinner
    if (spinner_scan) {
        lv_obj_add_flag(spinner_scan, LV_OBJ_FLAG_HIDDEN);
    }

    // Re-enable scan button
    if (btn_sonos_scan) {
        lv_obj_clear_state(btn_sonos_scan, LV_STATE_DISABLED);
    }

    if (cnt == 0) {
        lv_label_set_text(lbl_status, MDI_ALERT " No Sonos devices found on network");
        lv_obj_set_style_text_color(lbl_status, COL_ERROR, 0);
        return;
    }

    if (cnt < 0) {
        lv_label_set_text(lbl_status, MDI_ALERT " Discovery failed - check network");
        lv_obj_set_style_text_color(lbl_status, COL_ERROR, 0);
        return;
    }

    lv_label_set_text_fmt(lbl_status, MDI_CHECK " Found %d Sonos device%s", cnt, cnt == 1 ? "" : "s");
    lv_obj_set_style_text_color(lbl_status, COL_OK, 0);
    refreshDeviceList();
}

// ============================================================================
// WiFi Event Handlers
// ============================================================================
void ev_wifi_scan(lv_event_t* e) {
    // Disable button and show loading state
    if (btn_wifi_scan) {
        lv_obj_add_state(btn_wifi_scan, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_wifi_scan, COL_BORDER, LV_STATE_DISABLED);
    }
    if (lbl_scan_text) {
        lv_label_set_text(lbl_scan_text, MDI_REFRESH "  Scanning...");
    }

    lv_label_set_text(lbl_wifi_status, "Scanning for networks...");
    lv_obj_set_style_text_color(lbl_wifi_status, COL_ACCENT, 0);
    lv_obj_clean(list_wifi);
    // Hide password strip if visible from a previous selection
    lv_obj_add_flag(pw_strip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    // Show spinner
    if (spinner_wifi_scan) {
        lv_obj_remove_flag(spinner_wifi_scan, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(spinner_wifi_scan);
    }
    // lv_refr_now, NOT lv_timer_handler: we are inside an event callback, which
    // runs inside the outer lv_timer_handler(), and LVGL's already_running guard
    // makes a nested call return immediately without drawing. The spinner and
    // the status text would never appear before the blocking scan below.
    lv_refr_now(NULL);  // Update UI immediately

    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    int n = WiFi.scanNetworks();
    wifiNetworkCount = min(n, 20);

    // Hide spinner, re-enable button
    if (spinner_wifi_scan) lv_obj_add_flag(spinner_wifi_scan, LV_OBJ_FLAG_HIDDEN);
    if (btn_wifi_scan)     lv_obj_clear_state(btn_wifi_scan, LV_STATE_DISABLED);
    if (lbl_scan_text)     lv_label_set_text(lbl_scan_text, MDI_REFRESH " Scan");

    if (n == 0) {
        lv_label_set_text(lbl_wifi_status, MDI_ALERT " No networks found");
        lv_obj_set_style_text_color(lbl_wifi_status, COL_ERROR, 0);
        return;
    }

    if (n < 0) {
        lv_label_set_text(lbl_wifi_status, MDI_ALERT " Scan failed - try again");
        lv_obj_set_style_text_color(lbl_wifi_status, COL_ERROR, 0);
        return;
    }

    lv_label_set_text_fmt(lbl_wifi_status, MDI_CHECK " Found %d network%s", n, n == 1 ? "" : "s");
    lv_obj_set_style_text_color(lbl_wifi_status, COL_OK, 0);

    // Deduplicate: for mesh networks (same SSID, multiple APs) keep best RSSI only
    std::vector<int> unique_indices;
    for (int i = 0; i < wifiNetworkCount; i++) {
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        bool found = false;
        for (int& j : unique_indices) {
            if (WiFi.SSID(j) == ssid) {
                if (rssi > WiFi.RSSI(j)) j = i;  // keep stronger signal
                found = true;
                break;
            }
        }
        if (!found) unique_indices.push_back(i);
    }
    wifiNetworkCount = min((int)unique_indices.size(), 20);

    for (int ui = 0; ui < wifiNetworkCount; ui++) {
        int i = unique_indices[ui];
        wifiNetworks[ui] = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);

        // Icon color only: green=strong, accent=medium, red=weak
        lv_color_t icon_color;
        if      (rssi > -60) icon_color = COL_OK;
        else if (rssi > -75) icon_color = COL_ACCENT;
        else                 icon_color = COL_ERROR;

        lv_obj_t* btn = lv_btn_create(list_wifi);
        lv_obj_set_size(btn, lv_pct(100), SY(50));
        lv_obj_set_user_data(btn, (void*)(intptr_t)ui);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            selectedSSID = wifiNetworks[idx];
            // Show password strip + update SSID label
            lv_label_set_text(lbl_pw_ssid, selectedSSID.c_str());
            lv_obj_clear_flag(pw_strip, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(lbl_wifi_status, MDI_WIFI " %s", selectedSSID.c_str());
            lv_obj_set_style_text_color(lbl_wifi_status, COL_TEXT, 0);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, NULL);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, MDI_WIFI);
        lv_obj_set_style_text_font(icon, &lv_font_mdi_16, 0);
        lv_obj_set_style_text_color(icon, icon_color, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, SX(10), 0);

        lv_obj_t* ssid_lbl = lv_label_create(btn);
        lv_label_set_text(ssid_lbl, wifiNetworks[ui].c_str());
        lv_obj_set_style_text_color(ssid_lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(ssid_lbl, &font_text_14, 0);
        lv_obj_set_width(ssid_lbl, lv_pct(80));
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, SX(36), 0);
    }
    WiFi.scanDelete();
}

void ev_wifi_connect(lv_event_t* e) {
    if (selectedSSID.length() == 0) {
        lv_label_set_text(lbl_wifi_status, MDI_ALERT " Please select a network first");
        lv_obj_set_style_text_color(lbl_wifi_status, COL_ERROR, 0);
        return;
    }

    const char* pwd = lv_textarea_get_text(ta_password);

    // Disable connect button during connection
    if (btn_wifi_connect) {
        lv_obj_add_state(btn_wifi_connect, LV_STATE_DISABLED);
    }

    lv_label_set_text_fmt(lbl_wifi_status, MDI_REFRESH " Connecting to %s...", selectedSSID.c_str());
    lv_obj_set_style_text_color(lbl_wifi_status, COL_ACCENT, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(NULL);  // Update UI (nested lv_timer_handler would be a no-op)

    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.begin(selectedSSID.c_str(), pwd);

    // Issue #85: stash the entered creds so they still get persisted if this dialog's
    // connect loop times out before WL_CONNECTED (a flaky radio can associate slightly
    // later). checkWiFiReconnect() writes them to NVS once the link is actually up.
    // Cleared below if the immediate save succeeds.
    pending_wifi_ssid = selectedSSID;
    pending_wifi_pass = pwd;
    wifi_creds_need_save = true;

    // Non-blocking connection with visual feedback (max 30 seconds — mesh/Orbi can be slow).
    // MUST reset the hardware WDT each iteration: this function runs on mainAppTask which is
    // registered with the 30s WDT. The loop itself takes up to 30s → WDT fires at loop end.
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 60) {
        esp_task_wdt_reset();  // Feed WDT — loop runs up to 30s, WDT timeout = 30s
        vTaskDelay(pdMS_TO_TICKS(500));
        lv_label_set_text_fmt(lbl_wifi_status, MDI_REFRESH " Connecting to %s%s",
            selectedSSID.c_str(),
            tries % 4 == 0 ? "..." : tries % 4 == 1 ? ".  " : tries % 4 == 2 ? ".. " : " ..");
        // Repaint AFTER updating the label, and with lv_refr_now: this runs
        // inside an event callback, so a nested lv_timer_handler() hits LVGL's
        // already_running guard and returns without drawing. The animated dots
        // never rendered — the screen simply froze for up to 30s.
        lv_refr_now(NULL);  // Keep UI responsive
    }
    esp_task_wdt_reset();  // Reset after loop exits (NVS write below can take ~100ms)

    // Re-enable button
    if (btn_wifi_connect) {
        lv_obj_clear_state(btn_wifi_connect, LV_STATE_DISABLED);
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Save credentials to NVS
        Serial.printf("[WIFI] Saving credentials to NVS: SSID='%s'\n", selectedSSID.c_str());
        wifiPrefs.putString("ssid", selectedSSID);
        wifiPrefs.putString("pass", pwd);
        wifi_creds_need_save = false;  // issue #85: immediate save done; no deferred persist needed

        // Verify write succeeded
        String verifySSID = wifiPrefs.getString("ssid", "");
        String verifyPass = wifiPrefs.getString("pass", "");

        if (verifySSID == selectedSSID && verifyPass == pwd) {
            Serial.println("[WIFI] Credentials successfully saved and verified in NVS");
        } else {
            Serial.println("[WIFI] WARNING: NVS verification failed! Credentials may not persist.");
        }

        String ip = WiFi.localIP().toString();
        lv_label_set_text_fmt(lbl_wifi_status,
            MDI_WIFI " Connected to %s  (%s)",
            selectedSSID.c_str(), ip.c_str());
        lv_obj_set_style_text_color(lbl_wifi_status, COL_OK, 0);

        // Hide strip + keyboard, clear password field
        lv_obj_add_flag(pw_strip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
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
            reason = "Connection timeout - check password and try again";
        }

        lv_label_set_text_fmt(lbl_wifi_status, MDI_ALERT " Failed: %s", reason);
        lv_obj_set_style_text_color(lbl_wifi_status, COL_ERROR, 0);
    }
}

// ============================================================================
// OTA Update Functions
// ============================================================================
// True only when `candidate` is strictly newer than `current` by major.minor.patch.
// The check used to be a raw `!=`, which treats ANY difference as an update: a
// re-tagged release, a rollback, or a hand-edited version.json would all offer the
// fleet a silent DOWNGRADE with an "Update available" label. Parsing stops at the
// first non-digit/non-dot, so a "-nightly.abc1234" suffix is ignored here.
static bool isSemverNewer(const String& candidate, const String& current) {
    auto parse = [](const String& v, int out[3]) {
        out[0] = out[1] = out[2] = 0;
        int part = 0, val = 0;
        for (unsigned i = 0; i < v.length() && part < 3; i++) {
            char c = v[i];
            if (c >= '0' && c <= '9')      val = val * 10 + (c - '0');
            else if (c == '.')           { out[part++] = val; val = 0; }
            else break;                    // prerelease suffix ends the numeric part
        }
        if (part < 3) out[part] = val;
    };
    int a[3], b[3];
    parse(candidate, a);
    parse(current, b);
    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    return false;                          // numerically equal is not newer
}

static void checkForUpdates() {
    if (WiFi.status() != WL_CONNECTED) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_ALERT " No WiFi connection");
            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
        }
        return;
    }

    // CRITICAL: Prevent rapid clicking - minimum 5 seconds between checks
    // Rapid HTTPS checks exhaust SDIO buffer pool even with cooldowns
    static unsigned long last_check_time = 0;
    unsigned long now = millis();
    if (last_check_time > 0 && (now - last_check_time) < OTA_CHECK_DEBOUNCE_MS) {
        unsigned long wait_sec = (OTA_CHECK_DEBOUNCE_MS - (now - last_check_time)) / 1000 + 1;
        if (lbl_ota_status) {
            lv_label_set_text_fmt(lbl_ota_status, MDI_ALERT " Please wait %lu seconds", wait_sec);
            lv_obj_set_style_text_color(lbl_ota_status, COL_WARN, 0);
        }
        return;
    }
    last_check_time = now;

    // Disable check button during check
    if (btn_check_update) lv_obj_add_state(btn_check_update, LV_STATE_DISABLED);

    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, MDI_REFRESH " Checking for updates...");
        lv_obj_set_style_text_color(lbl_ota_status, COL_ACCENT, 0);
    }
    lv_refr_now(NULL);  // nested lv_timer_handler would be a no-op; see ev_wifi_scan

    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation

    HTTPClient http;

    // Choose API endpoint based on channel
    const char* apiUrl;
    if (ota_channel == 0) {
        // Stable: Get only latest non-prerelease
        apiUrl = "https://api.github.com/repos/" GITHUB_REPO "/releases/latest";
        Serial.println("[OTA] Checking Stable channel (latest stable release)");
    } else {
        // Nightly: Get recent releases (GitHub API doesn't sort prereleases first)
        // We'll fetch multiple and filter for the most recent nightly
        apiUrl = "https://api.github.com/repos/" GITHUB_REPO "/releases?per_page=5";
        Serial.println("[OTA] Checking Nightly channel (fetching recent releases)");
    }

    // CRITICAL: Acquire network_mutex BEFORE http.begin() to prevent SDIO overlap
    if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
        Serial.println("[OTA] Failed to acquire network mutex - check aborted");
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_ALERT " Network busy, try again");
            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
        }
        if (btn_check_update) lv_obj_clear_state(btn_check_update, LV_STATE_DISABLED);
        return;
    }

    // CRITICAL: Wait for general SDIO cooldown (200ms since last network op)
    now = millis();
    unsigned long elapsed = now - last_network_end_ms;
    if (last_network_end_ms > 0 && elapsed < 200) {
        vTaskDelay(pdMS_TO_TICKS(200 - elapsed));
    }

    // CRITICAL: Wait for HTTPS-specific cooldown (2000ms since last HTTPS)
    now = millis();
    elapsed = now - last_https_end_ms;
    if (last_https_end_ms > 0 && elapsed < OTA_HTTPS_COOLDOWN_MS) {
        vTaskDelay(pdMS_TO_TICKS(OTA_HTTPS_COOLDOWN_MS - elapsed));
    }

    int httpCode = -1;
    String payload = "";

    // Retry once on connection failure — TLS to api.github.com needs ~114KB DMA;
    // a brief wait lets previous sessions fully release their DMA buffers.
    for (int attempt = 1; attempt <= 2 && httpCode < 0; attempt++) {
        if (attempt > 1) {
            Serial.printf("[OTA] Retry (attempt %d) — free DMA: %d bytes\n",
                          attempt, heap_caps_get_free_size(MALLOC_CAP_DMA));
            vTaskDelay(pdMS_TO_TICKS(3000));
        } else {
            Serial.printf("[OTA] Free DMA before check: %d bytes\n",
                          heap_caps_get_free_size(MALLOC_CAP_DMA));
        }

        http.begin(client, apiUrl);
        http.addHeader("Accept", "application/vnd.github.v3+json");
        http.addHeader("User-Agent", "SonosESP/" FIRMWARE_VERSION);
        http.setTimeout(OTA_CHECK_TIMEOUT_MS);

        httpCode = http.GET();

        if (httpCode == 200) {
            // Cap before getString() — it reserves from Content-Length, so an oversized
            // response would allocate that much internal DRAM. Leaving payload empty
            // falls into the existing "no payload" path rather than short-circuiting
            // out of here, which would skip http.end() / client.stop() below.
            const int body_len = http.getSize();
            if (body_len > OTA_MAX_JSON_BYTES) {
                Serial.printf("[OTA] Release JSON too large (%d bytes, cap %d) — ignoring\n",
                              body_len, OTA_MAX_JSON_BYTES);
            } else {
                payload = http.getString();
            }
        }

        http.end();
        client.stop();
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_CLEANUP_MS));
    }

    // Update timestamps before releasing mutex
    last_network_end_ms = millis();
    last_https_end_ms = millis();

    // Release mutex after ALL network activity including TLS cleanup
    xSemaphoreGive(network_mutex);

    if (btn_check_update) lv_obj_clear_state(btn_check_update, LV_STATE_DISABLED);

    if (httpCode == 200) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            // For nightly channel, search array for first nightly release
            JsonVariant releaseObj;
            if (ota_channel == 1) {
                // Nightly: response is an array, find LATEST nightly release by published_at
                if (doc.is<JsonArray>() && doc.size() > 0) {
                    bool found = false;
                    String latest_published = "";

                    for (JsonVariant release : doc.as<JsonArray>()) {
                        String tag = release["tag_name"].as<String>();
                        // Check if this is a nightly release
                        if (tag.indexOf("-nightly") >= 0) {
                            String published = release["published_at"].as<String>();

                            // Compare published timestamps to find the latest
                            if (!found || published > latest_published) {
                                releaseObj = release;
                                latest_published = published;
                                found = true;
                                Serial.printf("[OTA] Found nightly release: %s (published: %s)\n",
                                            tag.c_str(), published.c_str());
                            }
                        }
                    }
                    if (!found) {
                        Serial.println("[OTA] No nightly releases found in recent releases");
                        if (lbl_ota_status) {
                            lv_label_set_text(lbl_ota_status, MDI_ALERT " No nightly releases found");
                            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
                        }
                        if (lbl_latest_version) {
                            lv_label_set_text(lbl_latest_version, "None");
                        }
                        return;
                    }
                } else {
                    Serial.println("[OTA] Error: Expected array response for nightly channel");
                    if (lbl_ota_status) {
                        lv_label_set_text(lbl_ota_status, MDI_ALERT " No nightly releases found");
                        lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
                    }
                    return;
                }
            } else {
                // Stable: response is a single object
                releaseObj = doc.as<JsonVariant>();
            }

            latest_version = releaseObj["tag_name"].as<String>();
            latest_version.replace("v", "");  // Remove 'v' prefix

            bool isPrerelease = releaseObj["prerelease"].as<bool>();
            const char* channelName = ota_channel == 0 ? "Stable" : "Nightly";

            // CRITICAL: Filter out nightly versions from Stable channel
            // A nightly version may have been incorrectly marked as stable (prerelease=false)
            // Always check the tag name to ensure Stable channel only shows stable versions
            if (ota_channel == 0 && latest_version.indexOf("-nightly") >= 0) {
                Serial.printf("[OTA] Skipping nightly version in Stable channel: v%s\n", latest_version.c_str());
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, MDI_ALERT " No stable releases found");
                    lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
                }
                if (lbl_latest_version) {
                    lv_label_set_text(lbl_latest_version, "None");
                }
                return;
            }

            // CRITICAL: Filter out stable versions from Nightly channel
            // Nightly channel should only show prerelease versions with "-nightly" in tag
            if (ota_channel == 1 && latest_version.indexOf("-nightly") < 0) {
                Serial.printf("[OTA] Skipping stable version in Nightly channel: v%s\n", latest_version.c_str());

                // Check if user is already on a nightly version
                String current_version = FIRMWARE_VERSION;
                if (current_version.indexOf("-nightly") >= 0) {
                    // User is on a nightly, and latest release is stable = user is on latest nightly
                    if (lbl_ota_status) {
                        lv_label_set_text(lbl_ota_status, MDI_CHECK " You're on the latest nightly version!");
                        lv_obj_set_style_text_color(lbl_ota_status, COL_OK, 0);
                    }
                    if (lbl_latest_version) {
                        lv_label_set_text_fmt(lbl_latest_version, "v%s", current_version.c_str());
                    }
                    if (btn_install_update) {
                        lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
                    }
                } else {
                    // User is on stable, no nightlies available
                    if (lbl_ota_status) {
                        lv_label_set_text(lbl_ota_status, MDI_ALERT " No nightly releases found");
                        lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
                    }
                    if (lbl_latest_version) {
                        lv_label_set_text(lbl_latest_version, "None");
                    }
                }
                return;
            }

            if (lbl_latest_version) {
                if (isPrerelease && ota_channel == 1) {
                    lv_label_set_text_fmt(lbl_latest_version, "v%s (pre)", latest_version.c_str());
                } else {
                    lv_label_set_text_fmt(lbl_latest_version, "v%s", latest_version.c_str());
                }
            }

            Serial.printf("[OTA] Latest %s version: v%s (prerelease: %s)\n",
                          channelName, latest_version.c_str(), isPrerelease ? "yes" : "no");

            // Find the firmware asset for THIS screen variant.
            // Preferred: firmware-4inch.bin / firmware-7inch.bin (published since v1.9.0).
            // Fallback:  the legacy unsuffixed firmware.bin, which is a copy of the 4"
            //            build — correct for the 4" only, and scheduled for removal.
            //            A 7" unit must never take it or it flashes 4" firmware.
            // Note "firmware-4inch.bin".indexOf("firmware.bin") is -1 (no substring match),
            // so the legacy check cannot accidentally swallow a variant asset.
            #if SCREEN_SIZE == 7
                const char* kVariantAsset = "firmware-7inch.bin";
                const bool  kAllowLegacy  = false;   // legacy asset is the 4" build
            #else
                const char* kVariantAsset = "firmware-4inch.bin";
                const bool  kAllowLegacy  = true;
            #endif

            // Reset before scanning: on a release with no matching asset this otherwise
            // retains the URL from a previous check and the UI still offers to install it.
            download_url = "";
            String legacy_url = "";
            JsonArray assets = releaseObj["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();
                if (name.indexOf(kVariantAsset) >= 0) {
                    download_url = asset["browser_download_url"].as<String>();
                    // Use HTTPS directly - ESP32-P4 supports it with WiFiClientSecure
                    break;  // exact variant match wins
                }
                if (kAllowLegacy && name.indexOf("firmware.bin") >= 0) {
                    legacy_url = asset["browser_download_url"].as<String>();
                }
            }
            if (download_url.length() == 0 && legacy_url.length() > 0) {
                Serial.println("[OTA] Variant asset not found - using legacy firmware.bin");
                download_url = legacy_url;
            }
            if (download_url.length() == 0) {
                Serial.printf("[OTA] No firmware asset for this build (%s) in release\n", kVariantAsset);
            }

            // Compare versions. Nightly builds all share the same major.minor.patch
            // and differ only by commit hash, so there ANY difference is a new build;
            // Stable must be strictly newer, or a re-tag reads as an update and
            // installs an older image over a newer one.
            const bool update_available = (ota_channel == 1)
                ? (latest_version != FIRMWARE_VERSION)
                : isSemverNewer(latest_version, FIRMWARE_VERSION);

            if (update_available && download_url.length() == 0) {
                // Newer release exists but carries no asset this build can install —
                // don't offer an Install button that would download nothing.
                if (lbl_ota_status) {
                    lv_label_set_text_fmt(lbl_ota_status, MDI_ALERT " v%s available, no build for this screen", latest_version.c_str());
                    lv_obj_set_style_text_color(lbl_ota_status, COL_WARN, 0);
                }
                if (btn_install_update) {
                    lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
                }
            } else if (update_available) {
                if (lbl_ota_status) {
                    lv_label_set_text_fmt(lbl_ota_status, MDI_DOWNLOAD " Update available: v%s", latest_version.c_str());
                    lv_obj_set_style_text_color(lbl_ota_status, COL_OK, 0);
                }
                if (btn_install_update) {
                    lv_obj_clear_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, MDI_CHECK " You're on the latest version!");
                    lv_obj_set_style_text_color(lbl_ota_status, COL_OK, 0);
                }
                if (btn_install_update) {
                    lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
                }
            }
        } else {
            if (lbl_ota_status) {
                lv_label_set_text(lbl_ota_status, MDI_ALERT " Failed to parse response");
                lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
            }
        }
    } else {
        if (lbl_ota_status) {
            lv_label_set_text_fmt(lbl_ota_status, MDI_ALERT " Check failed (HTTP %d)", httpCode);
            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
        }
    }
}

// Helper: Restore all tasks and state after OTA failure
static void otaRecovery() {
    // Close HTTP/TLS cleanup
    Serial.println("[OTA] === RECOVERY: Restoring normal operation ===");

    // Update HTTPS timestamps so art/lyrics tasks use proper cooldown after restarting.
    // performOTAUpdate() always does HTTPS before calling otaRecovery() on failure,
    // so art/lyrics must not fire HTTPS immediately after.
    last_network_end_ms = millis();
    last_https_end_ms = millis();

    // Hide progress bar and re-enable buttons
    if (bar_ota_progress) {
        lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_check_update) lv_obj_clear_state(btn_check_update, LV_STATE_DISABLED);
    if (btn_install_update) lv_obj_clear_state(btn_install_update, LV_STATE_DISABLED);

    // Re-enable WiFi features
    WiFi.setAutoReconnect(true);

    // Clear OTA flag
    if (xSemaphoreTake(ota_progress_mutex, pdMS_TO_TICKS(1000))) {
        ota_in_progress = false;
        xSemaphoreGive(ota_progress_mutex);
    }

    // Resume Sonos background tasks
    sonos.resumeTasks();

    // ALWAYS clear ALL shutdown/abort flags before restarting tasks.
    // These must be cleared unconditionally — tasks can't start cleanly if any
    // flag is still set. Add any new task's flags here when adding new features.
    art_shutdown_requested         = false;
    art_abort_download             = false;
    lyrics_shutdown_requested      = false;
    lyrics_abort_requested         = false;
    clock_bg_shutdown_requested    = false;
    sonos_tasks_shutdown_requested = false;  // resumeTasks() also resets this, belt-and-suspenders

    // Restart album art task if it isn't already running
    if (albumArtTaskHandle == NULL) {
        Serial.println("[OTA] Restarting album art task");
        createArtTask();  // PSRAM stack — frees 20KB internal SRAM for SDIO/WiFi DMA
    }

    Serial.println("[OTA] === Recovery complete ===");
}

// Signals all background tasks to stop, waits up to 12s for clean exit,
// force-kills stragglers, recreates the network mutex, sets ota_in_progress.
// Returns true if any task was force-killed (indicates possible DMA leak).
static bool otaStopTasks() {
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, MDI_REFRESH " Stopping background tasks...");
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);

    // Signal ALL tasks to stop SIMULTANEOUSLY — do this before any waiting
    // so all tasks start their shutdown paths in parallel at t=0.
    art_abort_download            = true;
    art_shutdown_requested        = true;
    lyrics_shutdown_requested     = true;
    lyrics_abort_requested        = true;
    clock_bg_shutdown_requested   = true;
    sonos_tasks_shutdown_requested = true;

    bool force_killed = false;
    {
        const uint32_t SHUTDOWN_BUDGET_MS = 12000;
        uint32_t shutdown_start = millis();
        Serial.println("[OTA] Waiting for all tasks to exit (parallel)...");

        while (millis() - shutdown_start < SHUTDOWN_BUDGET_MS) {
            if (albumArtTaskHandle == nullptr &&
                lyricsTaskHandle   == nullptr &&
                clockBgTaskHandle  == nullptr) break;
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_task_wdt_reset();
        }

        if (albumArtTaskHandle) {
            Serial.println("[OTA] WARNING: Force-killing art task (possible DMA leak)");
            vTaskDelete(albumArtTaskHandle);
            albumArtTaskHandle = nullptr;
            force_killed = true;
        }
        if (lyricsTaskHandle) {
            Serial.println("[OTA] WARNING: Force-killing lyrics task (possible DMA leak)");
            vTaskDelete(lyricsTaskHandle);
            lyricsTaskHandle = nullptr;
            force_killed = true;
        }
        if (clockBgTaskHandle) {
            Serial.println("[OTA] WARNING: Force-killing clock bg task (possible DMA leak)");
            vTaskDelete(clockBgTaskHandle);
            clockBgTaskHandle = nullptr;
            force_killed = true;
        }
        if (force_killed) {
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_task_wdt_reset();
        }
        Serial.printf("[OTA] Task shutdown in %lums — DMA: %d bytes\n",
                      (unsigned long)(millis() - shutdown_start),
                      heap_caps_get_free_size(MALLOC_CAP_DMA));
    }

    Serial.println("[OTA] Suspending Sonos tasks...");
    sonos.suspendTasks();

    // Recreate network mutex — a force-killed task may have left it poisoned.
    if (network_mutex) {
        vSemaphoreDelete(network_mutex);
        network_mutex = xSemaphoreCreateMutex();
        Serial.println("[OTA] Network mutex recreated (clean state)");
    }

    if (xSemaphoreTake(ota_progress_mutex, pdMS_TO_TICKS(1000))) {
        ota_in_progress = true;
        xSemaphoreGive(ota_progress_mutex);
    }

    return force_killed;
}

// Waits for TIME_WAIT TCP sockets to release DMA after task shutdown.
// If DMA remains below OTA_TARGET_FREE_DMA after OTA_DMA_POLL_MS, saves URL
// to NVS and calls ESP.restart() — does NOT return in that case.
// On success, configures WiFi for the download and returns normally.
static void otaCheckDMA() {
    // ================================================================
    // PHASE 4: CLEAR NETWORK STATE AND VERIFY DMA
    // ================================================================
    // DMA may be low because recent HTTPS sessions (OTA check, lyrics, weather)
    // leave their TCP sockets in TIME_WAIT for ~12s (lwIP: 2×MSL = 2×6s).
    // Each TIME_WAIT socket holds ~5-6KB DMA. With 3 sessions: ~15KB held.
    // Wait up to OTA_DMA_POLL_MS (15s) for them to expire naturally — no WiFi
    // disruption needed. WiFi.disconnect/reconnect is NEVER done here: it
    // destabilises the ESP32-C6 SDIO transport driver and causes download crashes.
    //
    // Only reboot if DMA is STILL insufficient after 15s. That would indicate an
    // mbedTLS DMA leak from a force-killed task — extremely rare with clean shutdown.
    uint32_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    Serial.printf("[OTA] DMA after task cleanup: %d bytes (need %d)\n", free_dma, OTA_TARGET_FREE_DMA);

    if (free_dma < OTA_TARGET_FREE_DMA) {
        // TIME_WAIT sockets are still alive — poll until they expire (up to 15s).
        // Exit early if DMA plateaus for 3 consecutive seconds (no more recovery
        // possible — mbedTLS state is permanent until full restart).
        Serial.println("[OTA] Waiting for TIME_WAIT sockets to expire...");
        uint32_t poll_start = millis();
        size_t prev_dma = 0;
        int plateau_count = 0;
        while (millis() - poll_start < OTA_DMA_POLL_MS) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
            free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
            uint32_t elapsed = (millis() - poll_start) / 1000;
            Serial.printf("[OTA] DMA: %d bytes (need %d) — %lus elapsed\n",
                free_dma, OTA_TARGET_FREE_DMA, (unsigned long)elapsed);
            if (lbl_ota_status) {
                lv_label_set_text_fmt(lbl_ota_status,
                    MDI_REFRESH " Freeing memory... (%d/%d KB)",
                    (int)(free_dma / 1024), (int)(OTA_TARGET_FREE_DMA / 1024));
                lv_obj_set_style_text_color(lbl_ota_status, COL_TEXT3, 0);
            }
            lv_tick_inc(1000);
            lv_refr_now(NULL);
            if (free_dma >= OTA_TARGET_FREE_DMA) break;
            // Plateau detection: if DMA hasn't changed in 3s, reboot now
            if (free_dma == prev_dma) {
                if (++plateau_count >= OTA_DMA_PLATEAU_COUNT) {
                    Serial.printf("[OTA] DMA plateaued at %d bytes — restarting early\n", free_dma);
                    break;
                }
            } else {
                plateau_count = 0;
            }
            prev_dma = free_dma;
        }
    }

    if (free_dma < OTA_TARGET_FREE_DMA) {
        // Still insufficient after 15s — likely an mbedTLS DMA leak from a
        // force-killed task. Only a full restart can reclaim it.
        // Save URL to NVS; auto-trigger on next boot skips checkForUpdates() HTTPS.
        Serial.printf("[OTA] DMA still insufficient (%d / %d) — restarting\n",
            free_dma, OTA_TARGET_FREE_DMA);
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putBool(NVS_KEY_OTA_PENDING, true);
        prefs.putString(NVS_KEY_OTA_URL, download_url.c_str());
        prefs.end();
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_REFRESH " Restarting to apply update...");
            lv_obj_set_style_text_color(lbl_ota_status, COL_TEXT, 0);
        }
        lv_tick_inc(10);
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(2000));
        display_set_brightness(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP.restart();
        return;  // unreachable
    }

    // WiFi already connected; disable auto-reconnect and power-save for download
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
}

static void performOTAUpdate() {
    if (download_url.length() == 0) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_ALERT " No update URL found");
            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
        }
        return;
    }

    // ================================================================
    // PHASE 1: IMMEDIATE UI FEEDBACK
    // ================================================================
    // Show "Preparing..." IMMEDIATELY so user knows button press registered
    if (btn_install_update) lv_obj_add_state(btn_install_update, LV_STATE_DISABLED);
    if (btn_check_update) lv_obj_add_state(btn_check_update, LV_STATE_DISABLED);
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, MDI_REFRESH " Preparing update...");
        lv_obj_set_style_text_color(lbl_ota_status, COL_ACCENT, 0);
    }
    if (bar_ota_progress) {
        lv_obj_clear_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);  // Force immediate refresh so user sees feedback

    Serial.println("[OTA] ========================================");
    Serial.println("[OTA] PREPARING FOR FIRMWARE UPDATE");
    Serial.println("[OTA] ========================================");

    // ================================================================
    // PHASE 2: WAIT FOR PREVIOUS HTTPS CLEANUP
    // ================================================================
    unsigned long now = millis();
    unsigned long elapsed = now - last_https_end_ms;
    if (last_https_end_ms > 0 && elapsed < OTA_HTTPS_COOLDOWN_MS) {
        unsigned long wait_ms = OTA_HTTPS_COOLDOWN_MS - elapsed;
        Serial.printf("[OTA] Waiting for previous HTTPS cleanup: %lums\n", wait_ms);
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_REFRESH " Waiting for network cleanup...");
        }
        lv_tick_inc(10);
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }

    // ================================================================
    // PHASE 3: STOP ALL BACKGROUND TASKS
    // ================================================================
    bool force_killed = otaStopTasks();

    // ================================================================
    // PHASE 4: CLEAR NETWORK STATE AND VERIFY DMA
    // ================================================================
    otaCheckDMA();  // polls TIME_WAIT sockets; restarts if DMA still low; sets WiFi mode

    // ================================================================
    // PHASE 5+6: CONNECT AND DOWNLOAD (retry on connection failure)
    // ================================================================
    // TLS handshake allocates ~71KB DMA (mbedTLS context + certificate chain +
    // crypto operation buffers). These remain allocated for the entire download
    // — TLS must stay active to decrypt the incoming firmware stream.
    // Called from setup() boot OTA path: ~125KB pre-TLS → ~54KB post-TLS (safe).
    // The retry loop retries on connection-level failures (stream drops,
    // 0 bytes received). Stall and timeout are fatal — do not retry.
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, MDI_DOWNLOAD " Connecting to server...");
    }
    if (lbl_ota_progress) {
        lv_label_set_text(lbl_ota_progress, "0%");
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(100));

    WiFiClientSecure* clientPtr = nullptr;
    HTTPClient* httpPtr = nullptr;
    int contentLength = 0;          // TOTAL firmware size, learned from the first 200 response
    size_t written = 0;             // Bytes handed to Update. NOT reset per attempt — see below.
    uint32_t download_start = 0;
    static uint8_t buff[OTA_BUFFER_SIZE];

    // The Update session and the byte counter deliberately span all attempts.
    // Root cause this addresses: esp-hosted-mcu #184 — on ESP32-P4 + C6 over SDIO,
    // inbound TCP stalls once the C6's RX buffers fill and the host mishandles
    // backpressure. It is a platform bug we cannot prevent, so instead we survive
    // it: each retry reconnects and RESUMES via an HTTP Range request rather than
    // re-downloading all ~2.4MB. Update.write() buffers into a 4KB sector buffer
    // held inside UpdateClass, so a partially-filled sector survives the reconnect
    // and the byte stream stays contiguous (keeping the MD5 valid) — provided we
    // never call Update.abort() on a retryable failure.
    bool update_begun = false;
    const uint32_t ota_started_ms = millis();

    for (int attempt = 1; attempt <= OTA_TLS_MAX_RETRIES; attempt++) {
        // Wall-clock guard: OTA_DOWNLOAD_TIMEOUT_MS is per-attempt, so the retry
        // ladder as a whole needs its own ceiling.
        if (millis() - ota_started_ms > OTA_TOTAL_BUDGET_MS) {
            Serial.printf("[OTA] Total budget %lus exhausted at %u/%d bytes — giving up\n",
                (unsigned long)(OTA_TOTAL_BUDGET_MS / 1000), (unsigned)written, contentLength);
            break;
        }

        if (attempt > 1) {
            uint32_t wait_sec = (uint32_t)(attempt - 1) * (OTA_TLS_RETRY_DELAY_MS / 1000);
            Serial.printf("[OTA] Connection failed - waiting %lus before retry %d/%d\n",
                (unsigned long)wait_sec, attempt, OTA_TLS_MAX_RETRIES);

            const int resume_pct = (contentLength > 0) ? (int)(written * 100 / contentLength) : 0;

            for (uint32_t s = wait_sec; s > 0; s--) {
                if (lbl_ota_status) {
                    lv_label_set_text_fmt(lbl_ota_status,
                        MDI_REFRESH " Retrying in %lus... (%d/%d)",
                        (unsigned long)s, attempt, OTA_TLS_MAX_RETRIES);
                    lv_obj_set_style_text_color(lbl_ota_status, COL_WARN, 0);
                }
                lv_tick_inc(1000);
                lv_refr_now(NULL);
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            if (lbl_ota_status) {
                // Say "Resuming" when we actually are, so the screen matches what
                // docs/TROUBLESHOOTING.md tells people to expect.
                if (written > 0)
                    lv_label_set_text_fmt(lbl_ota_status,
                        MDI_DOWNLOAD " Resuming from %d%%... (%d/%d)",
                        resume_pct, attempt, OTA_TLS_MAX_RETRIES);
                else
                    lv_label_set_text_fmt(lbl_ota_status,
                        MDI_DOWNLOAD " Connecting (attempt %d/%d)...", attempt, OTA_TLS_MAX_RETRIES);
                lv_obj_set_style_text_color(lbl_ota_status, COL_ACCENT, 0);
            }
            lv_tick_inc(10);
            lv_refr_now(NULL);
        }

        // --- CONNECT ---
        clientPtr = new WiFiClientSecure();
        clientPtr->setInsecure();
        httpPtr = new HTTPClient();

        Serial.println("[OTA] ========================================");
        Serial.printf("[OTA] DOWNLOAD ATTEMPT %d/%d\n", attempt, OTA_TLS_MAX_RETRIES);
        Serial.printf("[OTA] Free DMA: %d bytes | Free heap: %d bytes\n",
            heap_caps_get_free_size(MALLOC_CAP_DMA), ESP.getFreeHeap());
        Serial.println("[OTA] ========================================");

        // Update UI BEFORE GET() — lv_refr_now() after GET() causes a ~50ms delay during
        // which lwIP buffers 40–50 KB of firmware into DMA-backed TCP receive buffers,
        // consuming DMA needed for AES encryption and the SDIO RX pool (→ AES failure /
        // sdio_push_data_to_queue assert crash). Rendering before TLS connect is safe:
        // no firmware is flowing yet so no unexpected lwIP DMA consumption occurs.
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_DOWNLOAD " Downloading firmware...");
            lv_obj_set_style_text_color(lbl_ota_status, COL_ACCENT, 0);
        }
        lv_tick_inc(10);
        lv_refr_now(NULL);

        // Always re-request the original github.com URL — never a cached redirect
        // target. GitHub 302s to a signed release-assets.githubusercontent.com URL
        // whose JWT is valid for only 300s (exp - nbf), so a cached one would expire
        // mid-retry on exactly the slow links that need resuming. Re-requesting mints
        // a fresh signature every attempt.
        httpPtr->begin(*clientPtr, download_url);
        httpPtr->setTimeout(60000);
        httpPtr->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        // Resume request. GitHub release assets honour Range (verified: 206 with
        // Content-Range against release-assets.githubusercontent.com). HTTPClient's
        // setURL() -> beginInternal() does not clear _headers for an absolute
        // redirect, so this survives the 302 — but upstream considers that a bug
        // (arduino-esp32 #4487), so we verify the response below instead of trusting it.
        const bool resuming = (written > 0 && contentLength > 0);
        if (resuming) {
            char range_hdr[40];
            snprintf(range_hdr, sizeof(range_hdr), "bytes=%u-", (unsigned)written);
            httpPtr->addHeader("Range", range_hdr);
            Serial.printf("[OTA] Resuming: Range %s (of %d total)\n", range_hdr, contentLength);
        }

        int httpCode = httpPtr->GET();
        size_t post_tls_total = heap_caps_get_free_size(MALLOC_CAP_DMA);
        Serial.printf("[OTA] HTTP %d - Post-TLS DMA: %d bytes\n", httpCode, post_tls_total);

        // 200 = full body, 206 = partial (resume). A transient network/server fault
        // (negative HTTPClient code, or 5xx) is worth retrying now that retries
        // resume; a 4xx means the asset is genuinely wrong and never will be.
        if (httpCode != 200 && httpCode != 206) {
            const bool retryable = (httpCode < 0 || httpCode >= 500);
            Serial.printf("[OTA] HTTP %d — %s\n", httpCode, retryable ? "retryable" : "fatal");
            if (lbl_ota_status) {
                lv_label_set_text_fmt(lbl_ota_status, MDI_ALERT " Download failed (HTTP %d)", httpCode);
                lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
            }
            httpPtr->end(); clientPtr->stop();
            delete httpPtr; delete clientPtr;
            httpPtr = nullptr; clientPtr = nullptr;
            if (retryable && attempt < OTA_TLS_MAX_RETRIES) continue;
            if (update_begun) Update.abort();
            otaRecovery();
            return;
        }

        if (resuming) {
            // Verify the server honoured the Range rather than trusting either the
            // server or HTTPClient's header handling. A 200 here means it sent the
            // WHOLE file — appending that to what we already flashed would corrupt
            // the image, so drop everything and start clean.
            const int part_len = httpPtr->getSize();
            const int expected = contentLength - (int)written;
            if (httpCode != 206 || part_len != expected) {
                Serial.printf("[OTA] Range not honoured (HTTP %d, %d bytes, expected %d) — restarting from 0\n",
                    httpCode, part_len, expected);
                if (update_begun) { Update.abort(); update_begun = false; }
                written = 0;
                httpPtr->end(); clientPtr->stop();
                delete httpPtr; delete clientPtr;
                httpPtr = nullptr; clientPtr = nullptr;
                if (attempt < OTA_TLS_MAX_RETRIES) continue;
                otaRecovery();
                return;
            }
            Serial.printf("[OTA] Resume accepted: %d bytes remaining\n", part_len);
        } else {
            contentLength = httpPtr->getSize();
            if (contentLength <= 0 || contentLength > OTA_MAX_FIRMWARE_SIZE) {
                Serial.printf("[OTA] Invalid firmware size: %d bytes\n", contentLength);
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, MDI_ALERT " Invalid firmware file");
                    lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
                }
                httpPtr->end(); clientPtr->stop();
                delete httpPtr; delete clientPtr;
                httpPtr = nullptr; clientPtr = nullptr;
                otaRecovery();
                return;
            }
            Serial.printf("[OTA] Firmware: %d bytes (%.1f KB)\n", contentLength, contentLength / 1024.0);
        }

        // Post-TLS DMA check: if total free DMA is below the threshold, SDIO RX buffers
        // will be starved during download causing an assert crash. Retry the full TLS
        // handshake — the previous session's DMA will be returned to the pool first.
        // Note: heap_caps_get_largest_free_block(MALLOC_CAP_DMA) always returns 0 on
        // ESP32-P4 (DMA heap is managed outside the standard allocator), so we check
        // total free only. If TLS session resumption leaves a fragmented heap and causes
        // an mbedTLS AES failure downstream, the natural retry (continue) will start a
        // fresh full handshake from clean ~126KB DMA state and resume where we left off.
        if (post_tls_total < OTA_MIN_DMA_AFTER_TLS) {
            Serial.printf("[OTA] Post-TLS DMA too low (%d bytes, need %d) — retrying\n",
                post_tls_total, OTA_MIN_DMA_AFTER_TLS);
            if (lbl_ota_status) {
                lv_label_set_text_fmt(lbl_ota_status,
                    MDI_REFRESH " Low memory after TLS (%d KB) - retrying...",
                    post_tls_total / 1024);
                lv_obj_set_style_text_color(lbl_ota_status, COL_WARN, 0);
            }
            lv_tick_inc(10);
            lv_refr_now(NULL);
            httpPtr->end(); clientPtr->stop();
            delete httpPtr; delete clientPtr;
            httpPtr = nullptr; clientPtr = nullptr;
            continue;  // Retry — do not call otaRecovery()
        }

        // --- DOWNLOAD ---
        // Update.begin() is deferred (lazy) — called only on first received byte.
        // This avoids allocating the DMA-backed flash write buffer (~11KB) before we
        // know data actually flows; an early connection drop would otherwise leak that
        // allocation across retry attempts, starving DMA for the next TLS handshake.
        WiFiClient* stream = httpPtr->getStreamPtr();
        int lastPercent = -1;
        uint32_t lastUIUpdate = millis();
        int chunk_count = 0;
        download_start = millis();
        uint32_t last_data_time = millis();
        bool fatal_abort = false;
        // Stall and per-attempt timeout are NOT fatal any more: they are the
        // signature of esp-hosted-mcu #184, and they are exactly what resuming
        // exists to recover from. Only an unrecoverable flash-side error is fatal.
        bool attempt_failed = false;

        Serial.printf("[OTA] Downloading from %u/%d... DMA: %d bytes\n",
            (unsigned)written, contentLength, heap_caps_get_free_size(MALLOC_CAP_DMA));

        while (httpPtr->connected() && (written < (size_t)contentLength)) {
            if ((millis() - last_data_time) > OTA_STALL_TIMEOUT_MS) {
                Serial.printf("[OTA] STALL: No data for %ds at %d%% (%u bytes) — will resume\n",
                    OTA_STALL_TIMEOUT_MS / 1000, (int)(written * 100 / contentLength), (unsigned)written);
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, MDI_REFRESH " Download stalled - resuming...");
                    lv_obj_set_style_text_color(lbl_ota_status, COL_WARN, 0);
                }
                lv_tick_inc(10);
                lv_refr_now(NULL);
                attempt_failed = true;
                break;
            }

            if ((millis() - download_start) > OTA_DOWNLOAD_TIMEOUT_MS) {
                Serial.printf("[OTA] TIMEOUT: >%ds at %d%% (%u bytes) — will resume\n",
                    OTA_DOWNLOAD_TIMEOUT_MS / 1000, (int)(written * 100 / contentLength), (unsigned)written);
                if (lbl_ota_status) {
                    lv_label_set_text(lbl_ota_status, MDI_REFRESH " Download slow - resuming...");
                    lv_obj_set_style_text_color(lbl_ota_status, COL_WARN, 0);
                }
                lv_tick_inc(10);
                lv_refr_now(NULL);
                attempt_failed = true;
                break;
            }

            size_t available = stream->available();
            if (available) {
                last_data_time = millis();

                // Lazy Update.begin() — only allocate the DMA-backed flash write buffer
                // once actual data starts flowing. This prevents ~11KB DMA leaks when the
                // connection drops before any bytes arrive (e.g. AES failure path).
                if (!update_begun) {
                    if (!Update.begin(contentLength)) {
                        Serial.println("[OTA] Update.begin() failed — not enough flash space");
                        if (lbl_ota_status) {
                            lv_label_set_text(lbl_ota_status, MDI_ALERT " Not enough space for OTA");
                            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
                        }
                        lv_tick_inc(10);
                        lv_refr_now(NULL);
                        fatal_abort = true;
                        break;
                    }
                    update_begun = true;
                    Serial.printf("[OTA] Update.begin() OK — DMA: %d bytes\n",
                        heap_caps_get_free_size(MALLOC_CAP_DMA));
                }

                size_t toRead = (available < OTA_READ_SIZE) ? available : OTA_READ_SIZE;
                if (toRead > sizeof(buff)) toRead = sizeof(buff);
                int bytesRead = stream->readBytes(buff, toRead);
                if (bytesRead > 0) {
                    // A short write means bytes left the socket but never reached flash.
                    // Resuming from `written` would then skip them and corrupt the image,
                    // so this has to be fatal rather than retryable.
                    size_t w = Update.write(buff, (size_t)bytesRead);
                    if (w != (size_t)bytesRead) {
                        Serial.printf("[OTA] Short write: %u/%d at %u bytes — %s\n",
                            (unsigned)w, bytesRead, (unsigned)written, Update.errorString());
                        if (lbl_ota_status) {
                            lv_label_set_text_fmt(lbl_ota_status,
                                MDI_ALERT " Flash write failed: %s", Update.errorString());
                            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
                        }
                        lv_tick_inc(10);
                        lv_refr_now(NULL);
                        written += w;
                        fatal_abort = true;
                        break;
                    }
                    written += w;
                }

                chunk_count++;
                if (chunk_count % OTA_DMA_CHECK_INTERVAL == 0) {
                    size_t cur_free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
                    if (cur_free_dma < OTA_DMA_CRITICAL) {
                        vTaskDelay(pdMS_TO_TICKS(80));
                    } else if (cur_free_dma < OTA_DMA_LOW) {
                        vTaskDelay(pdMS_TO_TICKS(30));
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(OTA_BASE_DELAY_MS));
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(OTA_BASE_DELAY_MS));
                }

                esp_task_wdt_reset();

                int percent = (written * 100) / contentLength;
                if (percent != lastPercent) {
                    if (lbl_ota_progress) {
                        lv_label_set_text_fmt(lbl_ota_progress, "%d%%", percent);
                    }
                    if (bar_ota_progress) {
                        lv_bar_set_value(bar_ota_progress, percent, LV_ANIM_OFF);
                    }
                    lastPercent = percent;

                    if (percent % OTA_PROGRESS_LOG_INTERVAL == 0) {
                        uint32_t ui_now = millis();
                        lv_tick_inc(ui_now - lastUIUpdate);
                        lv_refr_now(NULL);
                        lastUIUpdate = ui_now;
                        Serial.printf("[OTA] %d%% (%d/%d bytes) - Free DMA: %d bytes\n",
                            percent, written, contentLength, heap_caps_get_free_size(MALLOC_CAP_DMA));
                    }
                }
            } else {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

        if (fatal_abort) {
            // Flash-side failure (no space, short write) — retrying cannot help.
            if (update_begun) Update.abort();
            httpPtr->end(); clientPtr->stop();
            delete httpPtr; delete clientPtr;
            httpPtr = nullptr; clientPtr = nullptr;
            otaRecovery();
            return;
        }

        if (written == (size_t)contentLength) {
            httpPtr->end(); clientPtr->stop();
            delete httpPtr; delete clientPtr;
            httpPtr = nullptr; clientPtr = nullptr;
            break;  // SUCCESS — exit retry loop
        }

        // Stalled or the connection dropped early. Tear down the socket but KEEP the
        // Update session: the next attempt resumes from `written` via Range, so the
        // bytes already flashed (and the partial sector still buffered inside
        // UpdateClass) are preserved. This is the whole point of the rework — before
        // it, every attempt restarted at 0 and a deterministic mid-file failure could
        // never be beaten by retrying.
        Serial.printf("[OTA] Attempt %d/%d ended at %u/%d bytes (%d%%) after %lus — %s\n",
            attempt, OTA_TLS_MAX_RETRIES, (unsigned)written, contentLength,
            contentLength > 0 ? (int)(written * 100 / contentLength) : 0,
            (unsigned long)((millis() - download_start) / 1000),
            attempt_failed ? "stalled, will resume" : "connection dropped, will resume");
        httpPtr->end(); clientPtr->stop();
        delete httpPtr; delete clientPtr;
        httpPtr = nullptr; clientPtr = nullptr;
    }

    // Reached when every attempt was used up, or the total budget expired.
    if (written != (size_t)contentLength) {
        Serial.printf("[OTA] Giving up after %d attempts (written=%u, expected=%d) — recovering\n",
            OTA_TLS_MAX_RETRIES, (unsigned)written, contentLength);
        if (update_begun) Update.abort();
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_ALERT " Download failed - try again later");
            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
        }
        lv_tick_inc(10);
        lv_refr_now(NULL);
        otaRecovery();
        return;
    }

    // ================================================================
    // PHASE 7: VERIFY AND INSTALL
    // ================================================================
    // Reaches here only on full successful download (written == contentLength).
    if (bar_ota_progress) lv_bar_set_value(bar_ota_progress, 100, LV_ANIM_OFF);
    if (lbl_ota_progress) lv_label_set_text(lbl_ota_progress, "100%");
    if (lbl_ota_status) lv_label_set_text(lbl_ota_status, MDI_CHECK " Download complete!");
    lv_tick_inc(10);
    lv_refr_now(NULL);

    Serial.printf("[OTA] Download complete: %d bytes in %lus\n", written, (millis() - download_start) / 1000);
    vTaskDelay(pdMS_TO_TICKS(500));

    // START INSTALL
    if (bar_ota_progress) lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
    if (lbl_ota_progress) lv_label_set_text(lbl_ota_progress, "");
    if (lbl_ota_status) lv_label_set_text(lbl_ota_status, MDI_REFRESH " Installing & verifying...");
    lv_tick_inc(10);
    lv_refr_now(NULL);

    // Animate install progress
    for (int i = 0; i <= 100; i += 10) {
        if (bar_ota_progress) lv_bar_set_value(bar_ota_progress, i, LV_ANIM_OFF);
        lv_tick_inc(50);
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (Update.end()) {
        if (Update.isFinished()) {
            // INSTALL COMPLETE - Clean screen and show reboot message
            lv_obj_clean(lv_screen_active());
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);

            lv_obj_t *reboot_label = lv_label_create(lv_screen_active());
            lv_label_set_text(reboot_label, "REBOOTING...");
            lv_obj_set_style_text_color(reboot_label, COL_TEXT, 0);
            lv_obj_set_style_text_font(reboot_label, &font_text_24, 0);
            lv_obj_center(reboot_label);

            lv_tick_inc(10);
            lv_refr_now(NULL);
            vTaskDelay(pdMS_TO_TICKS(1000));

            display_set_brightness(0);
            vTaskDelay(pdMS_TO_TICKS(100));

            ESP.restart();
        } else {
            if (lbl_ota_status) {
                lv_label_set_text(lbl_ota_status, MDI_ALERT " Update failed: Not finished");
                lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
            }
        }
    } else {
        if (lbl_ota_status) {
            lv_label_set_text_fmt(lbl_ota_status, MDI_ALERT " Update failed: %s", Update.errorString());
            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
        }
    }

    // The socket is already closed and freed by the success path in the retry loop —
    // both pointers are null here. Kept null-guarded so a future early exit can't
    // double-free.
    if (httpPtr)   { httpPtr->end(); delete httpPtr; httpPtr = nullptr; }
    if (clientPtr) { clientPtr->stop(); delete clientPtr; clientPtr = nullptr; }
    otaRecovery();
}

void ev_check_update(lv_event_t* e) {
    checkForUpdates();
}

void ev_install_update(lv_event_t* e) {
    if (download_url.isEmpty()) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_ALERT " No firmware URL - check for updates first");
            lv_obj_set_style_text_color(lbl_ota_status, COL_ERROR, 0);
        }
        return;
    }

    // Save URL to NVS and restart immediately.
    // The firmware download runs at the START of the next boot, before any background
    // tasks (art, Sonos, lyrics) are created. This gives TLS the full ~125KB DMA
    // headroom it needs. The ~71KB consumed by the TLS handshake leaves ~54KB free —
    // enough for the SDIO RX pool, AES alignment buffers, and Update.begin() buffer.
    //
    // Attempting a live download (tasks running) leaves only ~34KB DMA after TLS,
    // which starves the SDIO RX pool → sdio_push_data_to_queue assert crash.
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_OTA_PENDING, true);
    prefs.putString(NVS_KEY_OTA_URL, download_url.c_str());
    prefs.end();

    Serial.println("[OTA] URL saved to NVS — restarting for boot OTA");
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, MDI_REFRESH " Restarting to install update...");
        lv_obj_set_style_text_color(lbl_ota_status, COL_TEXT, 0);
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(1500));  // let user see the message
    display_set_brightness(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP.restart();
}

// Called from setup() if NVS_KEY_OTA_PENDING was saved before the previous reboot.
// Uses the URL saved before reboot - skips checkForUpdates() entirely so no HTTPS session
// consumes DMA before the OTA TLS handshake. This breaks the reboot loop.
// Put the player into its "this track has no artwork" state: hide the art, show
// the placeholder, and drop the blurred backdrop generated from the PREVIOUS
// track.
//
// This exists because there are TWO independent ways to reach that state and
// they were drifting apart. v1.14.1 fixed the backdrop for the download-failed
// path (art_show_placeholder) but not for the far more common one — a track
// that simply has no art URL, handled inline in updateUI() — so #49 came back
// with the reporter still seeing the previous album behind the placeholder.
//
// blur_bg_valid must be cleared as well as the HIDDEN flag: it is what
// themeSet() and the Display-settings blur toggle consult when they republish
// existing artwork, so hiding the object alone lets the stale image return the
// moment either of those runs.
static void showNoArtwork(void) {
    if (img_album)       lv_obj_add_flag(img_album, LV_OBJ_FLAG_HIDDEN);
    if (art_placeholder) lv_obj_remove_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);
    if (img_blur_bg)     lv_obj_add_flag(img_blur_bg, LV_OBJ_FLAG_HIDDEN);
    blur_bg_valid = false;
    blur_bg_ready = false;
}

void triggerPendingOTA() {
    // Load saved URL - skip checkForUpdates() (its HTTPS session costs ~8KB DMA we can't afford)
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    String saved_url = prefs.getString(NVS_KEY_OTA_URL, "");
    prefs.remove(NVS_KEY_OTA_URL);
    prefs.end();

    lv_screen_load(scr_ota);
    lv_tick_inc(10);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(500));

    if (saved_url.length() > 0) {
        Serial.printf("[OTA] Auto-trigger: using saved URL (no pre-OTA HTTPS - max DMA preserved)\n");
        download_url = saved_url;
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, MDI_REFRESH " Resuming update after restart...");
            lv_obj_set_style_text_color(lbl_ota_status, COL_TEXT, 0);
        }
        lv_tick_inc(10);
        lv_refr_now(NULL);
        performOTAUpdate();
    } else {
        // No saved URL (e.g. flag set manually) - fall back to normal check
        Serial.println("[OTA] Auto-trigger: no saved URL - running check");
        checkForUpdates();
        if (download_url.length() > 0) {
            performOTAUpdate();
        }
    }
}

// ============================================================================
// ============================================================================
// updateUI() Sub-Functions (static — implementation details, not in any header)
// ============================================================================

// Handles disconnect/reconnect UI state. Returns true if device is connected
// and updateUI() should continue. Returns false → caller must return immediately.
static bool updateConnectionState(SonosDevice* d) {
    static bool was_connected  = false;
    static bool ui_cleared     = false;
    static bool last_conn_state = false;

    if (d->connected != last_conn_state) {
        Serial.printf("[UI] Connection state changed: %s (errorCount=%d)\n",
                     d->connected ? "CONNECTED" : "DISCONNECTED", d->errorCount);
        last_conn_state = d->connected;
    }

    if (!d->connected) {
        if (was_connected || !ui_cleared) {
            lv_label_set_text(lbl_title, "Device Not Connected");
            lv_label_set_text(lbl_artist, "");
            lv_label_set_text(lbl_album, "");
            lv_label_set_text(lbl_time, "0:00");
            lv_label_set_text(lbl_time_remaining, "0:00");
            lv_slider_set_value(slider_progress, 0, LV_ANIM_OFF);

            lv_obj_add_flag(img_album, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);

            lv_obj_add_flag(img_next_album, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);

            if (panel_art)   lv_obj_set_style_bg_color(panel_art,   COL_BG, 0);
            if (panel_right) lv_obj_set_style_bg_color(panel_right, COL_BG, 0);

            lv_obj_t* lbl = lv_obj_get_child(btn_play, 0);
            lv_label_set_text(lbl, MDI_PAUSE);
            lv_obj_set_style_text_font(lbl, &lv_font_mdi_40, 0);
            lv_obj_center(lbl);

            ui_title = "";
            ui_artist = "";
            was_connected = false;
            ui_cleared = true;
            Serial.println("[UI] Device disconnected - UI cleared");
        }
        return false;  // not connected
    }

    if (d->connected && !was_connected) {
        was_connected = true;
        ui_cleared = false;
        ui_title = "";
        ui_artist = "";
        Serial.println("[UI] Device reconnected - forcing UI refresh");
    }
    return true;  // connected
}

// Displays art or placeholder from the background art task.
// Must be called on the main LVGL thread. Takes art_mutex internally.
static void displayCompletedArt() {
    if (!xSemaphoreTake(art_mutex, 0)) return;

    if (art_ready) {
        // Build art_dsc here on the main thread — same thread as lv_timer_handler() /
        // LVGL renderer — so there is never concurrent read+write of the descriptor.
        // The background art task only writes art_buffer (pixels); we set the header here.
        memset(&art_dsc, 0, sizeof(art_dsc));
        art_dsc.header.w    = ART_PX;
        art_dsc.header.h    = ART_PX;
        art_dsc.header.cf   = LV_COLOR_FORMAT_RGB565;
        art_dsc.data_size   = ART_PX * ART_PX * 2;
        art_dsc.data        = (const uint8_t*)art_buffer;
        lv_img_set_src(img_album, &art_dsc);
        themeApplyArtGeometry(img_album);   // HERO (Classic) vs THUMB (Immersive)
        lv_obj_remove_flag(img_album, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);
        art_ready = false;
        art_show_placeholder = false;
    } else if (art_show_placeholder) {
        // Download failed or was abandoned — see showNoArtwork().
        showNoArtwork();
        art_show_placeholder = false;
    }
    // Ambient/Immersive themes paint a solid backdrop instead — themeUsesBlurBg()
    // keeps the blurred art hidden so it can't sit on top of that colour.
    if (blur_bg_ready && img_blur_bg && blur_bg_buf && themeUsesBlurBg()) {
        memset(&blur_bg_dsc, 0, sizeof(blur_bg_dsc));
        blur_bg_dsc.header.w  = DISPLAY_WIDTH;
        blur_bg_dsc.header.h  = DISPLAY_HEIGHT;
        blur_bg_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        blur_bg_dsc.data_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2;
        blur_bg_dsc.data      = (const uint8_t*)blur_bg_buf;
        lv_img_set_src(img_blur_bg, &blur_bg_dsc);
        lv_obj_remove_flag(img_blur_bg, LV_OBJ_FLAG_HIDDEN);
        blur_bg_ready = false;
    }
    if (color_ready) {
        // Restore progress bar + button pressed accent colors from dominant art color.
        // Panel bg changes in setBackgroundColor are invisible (panels are transparent)
        // but slider_progress indicator/knob + button pressed highlights still update.
        setBackgroundColor(dominant_color);
        color_ready = false;
    }
    xSemaphoreGive(art_mutex);
}

// Updates the "Next Up" track labels from the cached queue.
static void updateNextTrackUI(SonosDevice* d) {
    static String last_next_title = "";

    // CR-1 follow-up: queue[].title/artist and repeatMode are Strings the polling task
    // rewrites under deviceMutex (updateQueue/updateTrackInfo). Walk the queue and extract
    // the next title/artist INTO locals while holding the lock, then do all LVGL work below
    // outside it (never call LVGL while holding the mutex). Scalars/bools (queueSize,
    // currentTrackNumber, isRadioStation…) are atomic single-word reads and stay direct.
    bool show = false;       // found a valid next track → update labels
    bool hideAll = false;    // next track truly unavailable → hide labels
    String nextTitle, nextArtist;

    if (!d->isRadioStation && !d->isLineIn && !d->isTvAudio && d->queueSize > 0 && d->currentTrackNumber > 0) {
        if (xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(30))) {
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
                nextTitle  = d->queue[nextIdx].title;
                nextArtist = d->queue[nextIdx].artist;
                show = true;
            } else if (nextIdx < 0) {
                hideAll = true;  // next track truly unavailable (not just temporarily)
            }
            // nextIdx>=0 with empty title → leave display as-is (matches prior behavior)
            xSemaphoreGive(sonos.getDeviceMutex());
        } else {
            return;  // polling is mid-write; skip this UI tick (retries in ~200ms)
        }
    } else {
        hideAll = true;
    }

    // ── LVGL work happens outside the lock ──
    if (show) {
        if (nextTitle != last_next_title || ui_force_refresh) {
            lv_label_set_text(lbl_next_title, nextTitle.c_str());
            lv_label_set_text(lbl_next_artist, nextArtist.c_str());
            lv_obj_clear_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
            last_next_title = nextTitle;
        }
    } else if (hideAll) {
        if (last_next_title != "") {
            lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
            last_next_title = "";
        }
    }
}

// Selects the correct art URL and calls requestAlbumArt() when needed.
// Also handles URI change detection and "not playing" transitions.
static void updateAlbumArtRequest(SonosDevice* d) {
    static String last_track_uri = "";
    static String last_source_prefix = "";
    static bool had_track = false;

    // CR-1 follow-up: currentURI / currentTrack / albumArtURL / radioStationArtURL are
    // Strings the polling task rewrites under deviceMutex (updateTrackInfo/updateMediaInfo).
    // Snapshot them into locals once under the lock, then use the locals throughout —
    // lock-free reads risked a torn/dangling pointer. No LVGL call is made under the lock.
    String s_uri, s_track, s_albumArtURL, s_stationURL;
    if (xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(30))) {
        s_uri         = d->currentURI;
        s_track       = d->currentTrack;
        s_albumArtURL = d->albumArtURL;
        s_stationURL  = d->radioStationArtURL;
        xSemaphoreGive(sonos.getDeviceMutex());
    } else {
        return;  // polling is mid-write; skip this UI tick (retries in ~200ms)
    }

    // Extract source prefix to detect actual source changes (not just track changes)
    String current_source_prefix = "";
    if (s_uri.startsWith("x-sonos-vli:")) {
        current_source_prefix = "x-sonos-vli";  // Spotify, Apple Music, etc
    } else if (s_uri.startsWith("hls-radio://")) {
        current_source_prefix = "hls-radio";  // Radio
    } else if (s_uri.startsWith("x-sonos-http:")) {
        current_source_prefix = "x-sonos-http";  // Radio
    } else if (s_uri.startsWith("x-rincon-mp3radio:")) {
        current_source_prefix = "x-rincon-mp3radio";  // Radio
    } else {
        // Extract first part before colon for unknown sources
        int colonPos = s_uri.indexOf(':');
        if (colonPos > 0) {
            current_source_prefix = s_uri.substring(0, colonPos);
        }
    }

    // Detect ACTUAL source changes (Spotify→Radio, not Spotify track1→track2)
    bool actual_source_change = (current_source_prefix != last_source_prefix && current_source_prefix.length() > 0);

    // Detect any URI change (track or source)
    bool uri_changed = (s_uri != last_track_uri);

    if (uri_changed) {
        // Always update last_track_uri (even when empty) to prevent repeated firing
        // when Sonos reports empty URI in stopped state
        last_track_uri = s_uri;

        if (s_uri.length() > 0) {
            if (actual_source_change) {
                Serial.printf("[ART] SOURCE CHANGE: %s -> %s\n", last_source_prefix.c_str(), current_source_prefix.c_str());
                last_source_prefix = current_source_prefix;
            } else {
                Serial.printf("[ART] Track changed (same source: %s)\n", current_source_prefix.c_str());
            }
            // CRITICAL: Abort any in-progress album art download immediately
            // Applies to ALL track changes (not just source changes) so the art task
            // doesn't wait for a 10-second HTTP timeout before processing the new track
            art_abort_download = true;
            // CRITICAL: Must hold art_mutex when writing last_art_url/pending_art_url -
            // the art task reads both under mutex, and String assignment is not atomic.
            if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(50))) {
                last_art_url    = "";  // Force art refresh on any URI change
                pending_art_url = "";  // Prevent art task downloading old song's art during
                                       // the window between URI detection and requestAlbumArt()
                                       // call ~100 lines below. Without this, art task wakes,
                                       // sees pending=old_url != last_art_url="" and starts
                                       // downloading wrong art, wasting SDIO traffic and setting
                                       // last_art_download_end_ms (adding 1s cooldown for new art).
                art_ready = false;  // Discard any just-completed download — prevents old art
                                    // flashing for the new track if displayCompletedArt() fires
                                    // before the new art task iteration starts.
                xSemaphoreGive(art_mutex);
            }
            // Clear LRU cache: prevents cached art from the OLD track flashing for
            // the NEW track if the art task picks up the new URL within the same
            // updateUI frame (cache hit would bypass the placeholder entirely).
            clearAlbumArtCache();
            // Show placeholder immediately (main thread = LVGL-safe)
            if (img_album)       lv_obj_add_flag(img_album, LV_OBJ_FLAG_HIDDEN);
            if (art_placeholder) lv_obj_remove_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Show placeholder when device transitions to "Not Playing" (no active track)
    // Without this, the last track's art stays frozen when playback stops
    bool has_track = (s_track.length() > 0);
    if (had_track && !has_track) {
        Serial.println("[ART] Not playing - clearing art display");
        art_abort_download = true;  // Stop any in-progress download immediately
        clearAlbumArtCache();
        if (img_album) lv_obj_add_flag(img_album, LV_OBJ_FLAG_HIDDEN);
        if (art_placeholder) lv_obj_remove_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);
        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(50))) {
            last_art_url = "";
            pending_art_url = "";  // Prevent art task re-fetching the old URL
            art_ready = false;     // Discard any just-completed download (prevents art flash)
            xSemaphoreGive(art_mutex);
        }
    }
    had_track = has_track;

    // Request album art if URL provided and (URL changed or track changed)
    // Compare against last_requested_art_url (HTTPS, same type as d->albumArtURL) NOT pending_art_url.
    // The art task converts pending_art_url to HTTP internally — comparing HTTPS vs HTTP always
    // returns "changed", calling requestAlbumArt() every frame and keeping art_download_in_progress=true
    // permanently (blocking the clock screensaver and spamming last_track_change_ms).
    static String last_requested_art_url = "";
    bool hasArt = !d->isLineIn && !d->isTvAudio &&
                  ((s_albumArtURL.length() > 0) || (d->isRadioStation && s_stationURL.length() > 0));
    bool artChanged = uri_changed || (s_albumArtURL.length() > 0 && s_albumArtURL != last_requested_art_url);

    // For radio stations: also check if radioStationArtURL changed (even if albumArtURL is empty)
    if (d->isRadioStation && s_stationURL.length() > 0 && s_stationURL != last_requested_art_url) {
        artChanged = true;
    }

    if (!hasArt && uri_changed) {
        // Track changed but has NO art URL — clear old art and show placeholder immediately
        // (Without this, the old track's art stays on screen forever)
        Serial.println("[ART] No art URL for this track - showing placeholder");
        // Was hiding the art and showing the placeholder but leaving the
        // blurred backdrop up, so the previous album stayed on screen
        // behind it (#49).
        showNoArtwork();
        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(50))) {
            last_art_url = "";
            pending_art_url = "";  // Prevent art task re-fetching the old URL
            art_ready = false;     // Discard any just-completed download (prevents art flash)
            xSemaphoreGive(art_mutex);
        }
    } else if (hasArt && artChanged) {
        String artURL = "";
        bool usingStationLogo = false;  // Track if we're using station logo (PNG allowed)

        // Determine which art to use
        if (s_albumArtURL.length() > 0) {
            artURL = s_albumArtURL;
        }

        // RADIO STATION LOGO FALLBACK:
        // If playing radio and no song art available, use station logo instead
        if (d->isRadioStation) {
            bool hasSongArt = (artURL.length() > 0);
            bool hasStationLogo = (s_stationURL.length() > 0);
            Serial.printf("[ART] Radio check - hasSongArt=%d, hasStationLogo=%d, artURL='%s', stationURL='%s'\n",
                         hasSongArt, hasStationLogo, artURL.c_str(), s_stationURL.c_str());

            // If no song art but have station logo, use the logo
            if (!hasSongArt && hasStationLogo) {
                artURL = s_stationURL;
                usingStationLogo = true;
                Serial.println("[ART] Radio: Using station logo (no song art)");
            }
            // If song art is just a generic Sonos radio icon, prefer the actual station logo
            else if (hasSongArt && hasStationLogo && artURL.indexOf("/getaa?") > 0) {
                // Check if it's pointing to the radio URI (generic icon)
                if (artURL.indexOf("x-sonosapi-stream") > 0 ||
                    artURL.indexOf("x-rincon-mp3radio") > 0 ||
                    artURL.indexOf("x-sonosapi-radio") > 0 ||
                    artURL.indexOf("x-sonosapi-hls") > 0) {
                    artURL = s_stationURL;
                    usingStationLogo = true;
                    Serial.println("[ART] Radio: Using station logo (replacing generic icon)");
                }
            }
        }

        // Set the flag for album art task to know if PNG is allowed
        pending_is_station_logo = usingStationLogo;

        if (artURL.length() > 0) {
            // Note: Using ESP32-P4 hardware JPEG decoder - can handle full 640x640 Spotify images!

            // Apple Music: reduce image size to avoid "too large" errors (1400x1400 can be 500KB+)
            if (artURL.indexOf("mzstatic.com") > 0) {
                if (artURL.indexOf("/1400x1400bb.jpg") > 0) {
                    artURL.replace("/1400x1400bb.jpg", "/400x400bb.jpg");
                    Serial.println("[ART] Apple Music - reduced to 400x400");
                } else if (artURL.indexOf("/1080x1080cc.jpg") > 0) {
                    artURL.replace("/1080x1080cc.jpg", "/400x400cc.jpg");
                    Serial.println("[ART] Apple Music - reduced to 400x400");
                }
            }

            requestAlbumArt(artURL);
            last_requested_art_url = artURL;  // track HTTPS URL; prevents HTTPS!=HTTP false-positive on next frame
        } else {
            // No art available - clear display
            Serial.println("[ART] No art URL - clearing display");
            if (img_album) lv_obj_add_flag(img_album, LV_OBJ_FLAG_HIDDEN);
            if (art_placeholder) lv_obj_remove_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);
            // CRITICAL: Must hold art_mutex when writing last_art_url (not atomic)
            if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(50))) {
                last_art_url = "";  // Clear to allow next art request
                xSemaphoreGive(art_mutex);
            }
        }
    }
}

// ============================================================================
// UI Update Function
// ============================================================================
void updateUI() {
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) return;

    if (!updateConnectionState(d)) return;

    // Device is connected - update UI normally

    // CR-1: snapshot the racy String fields under deviceMutex once, then read the locals
    // below. The polling task reassigns these Strings under the same mutex; reading them
    // lock-free risked a torn/dangling pointer → crash. Scalars (ints/bools) are atomic
    // single-word reads and stay direct. No LVGL call is made while holding the lock.
    String s_track, s_artist, s_album, s_room, s_relTime, s_repeat;
    if (xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(30))) {
        s_track   = d->currentTrack;
        s_artist  = d->currentArtist;
        s_album   = d->currentAlbum;
        s_room    = d->roomName;
        s_relTime = d->relTime;
        s_repeat  = d->repeatMode;
        xSemaphoreGive(sonos.getDeviceMutex());
    } else {
        return;  // polling is mid-write; skip this UI tick (retries in ~200ms)
    }

    // Title
    if (s_track != ui_title) {
        lv_label_set_text(lbl_title, s_track.length() > 0 ? s_track.c_str() : "Not Playing");
        ui_title = s_track;
    }

    // Artist
    if (s_artist != ui_artist) {
        lv_label_set_text(lbl_artist, s_artist.c_str());
        ui_artist = s_artist;
    }

    // Fetch synced lyrics when track changes
    // lyrics_last_track is a global (ui_globals.cpp) so clock exit can reset it to ""
    // forcing a re-fetch for the current track when returning from the clock screen.
    String lyrics_key = s_artist + "|" + s_track;
    if (lyrics_key != lyrics_last_track && s_track.length() > 0) {
        last_track_change_ms = millis();
        if (lyrics_enabled && !d->isRadioStation && !d->isLineIn && !d->isTvAudio) {
            // Abort any running task FIRST. requestLyrics() checks artist.length()==0
            // at line 1 and returns false without ever touching lyrics_abort_requested —
            // so for podcasts/audiobooks the old task keeps running, finishes, writes
            // stale lyrics into the buffer, and main thread displays them even after
            // clearLyrics(). Setting the flag here stops the old task before clear.
            lyrics_abort_requested = true;
            clearLyrics();
            // requestLyrics() returns false if the previous task is still running
            // (e.g. blocked inside http.GET() with up to 10s timeout). In that case
            // we do NOT update lyrics_last_track — the condition fires again next
            // frame until the old task exits and requestLyrics() can safely spawn.
            // This prevents TCB/stack reuse while the old task is alive, which would
            // permanently leak ~32KB of TLS DMA buffers per rapid track change.
            if (requestLyrics(s_artist, s_track, d->durationSeconds)) {
                lyrics_last_track = lyrics_key;
            } else if (!lyrics_fetching) {
                // Task not running and spawn failed (e.g. empty artist for podcast/audiobook,
                // buffer not initialised). Accept "no lyrics" to avoid a per-frame retry loop.
                lyrics_last_track = lyrics_key;
            }
        } else {
            lyrics_last_track = lyrics_key;
            clearLyrics();
        }
    }

    // Album name (below album art)
    static String ui_album_name = "";
    if (s_album != ui_album_name || ui_force_refresh) {
        lv_label_set_text(lbl_album, s_album.c_str());
        ui_album_name = s_album;
    }

    // Device name in header
    static String ui_device_name = "";
    if (s_room != ui_device_name || ui_force_refresh) {
        // Just the room. "Now Playing - " was 14 characters of prefix on the
        // now-playing screen, which pushed the only useful part of the string
        // ("Living Room") out of the box and into an ellipsis. Dropping it means
        // almost every room name now fits outright — which matters beyond
        // tidiness, because a label that fits does not run a scroll animation,
        // and animations on this screen cost frames.
        lv_label_set_text(lbl_device_name, s_room.c_str());
        ui_device_name = s_room;
    }

    // Consume the force-refresh request now that every cached block above has
    // had a chance to re-push its value into the (possibly rebuilt) widgets.
    ui_force_refresh = false;

    // Time display
    String t = s_relTime;
    if (t.startsWith("0:")) t = t.substring(2);
    lv_label_set_text(lbl_time, t.c_str());

    // Remaining time as negative countdown: -M:SS (Apple Music / Spotify style)
    if (d->durationSeconds > 0) {
        int rem = d->durationSeconds - d->relTimeSeconds;
        if (rem < 0) rem = 0;
        int rm = rem / 60;
        int rs = rem % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "-%d:%02d", rm, rs);
        lv_label_set_text(lbl_time_remaining, buf);
    }

    // Progress slider
    if (!dragging_prog && d->durationSeconds > 0)
        lv_slider_set_value(slider_progress, (d->relTimeSeconds * 100) / d->durationSeconds, LV_ANIM_OFF);

    // Update synced lyrics display and status indicator
    updateLyricsDisplay(d->relTimeSeconds);
    updateLyricsStatus();  // Update status indicator from main thread

    // Play/Pause button
    if (d->isPlaying != ui_playing) {
        lv_obj_t* lbl = lv_obj_get_child(btn_play, 0);
        lv_label_set_text(lbl, d->isPlaying ? MDI_PAUSE : MDI_PLAY);
        lv_obj_set_style_text_font(lbl, &lv_font_mdi_40, 0);
        lv_obj_center(lbl);  // MDI icons are optically centered — no offset needed

        ui_playing = d->isPlaying;
    }

    // Volume slider update
    if (!dragging_vol && d->volume != ui_vol && slider_vol) {
        lv_slider_set_value(slider_vol, d->volume, LV_ANIM_OFF);
        // LVGL does not raise VALUE_CHANGED for a programmatic set, and the
        // Amber player hangs its volume readout off exactly that event. Raising
        // it here keeps the number in step when the volume moves from the app or
        // another controller. ev_vol_slider ignores VALUE_CHANGED (it only acts
        // on PRESSING/RELEASED), so no command is issued.
        lv_obj_send_event(slider_vol, LV_EVENT_VALUE_CHANGED, NULL);
        ui_vol = d->volume;
    }

    // Mute button
    if (d->isMuted != ui_muted && btn_mute) {
        lv_obj_t* lbl = lv_obj_get_child(btn_mute, 0);
        lv_label_set_text(lbl, d->isMuted ? MDI_VOLUME_OFF : MDI_VOLUME_HIGH);
        ui_muted = d->isMuted;
    }

    // Shuffle
    if (d->shuffleMode != ui_shuffle) {
        lv_obj_t* lbl = lv_obj_get_child(btn_shuffle, 0);
        // Theme accessors, same as the repeat button below. Hardcoding COL_* here
        // handed Amber the original palette's gold (#D4A84B) instead of its own
        // (#E0B252) - close enough to look like a rendering artefact rather than
        // the wrong colour, which is exactly why it survived review.
        lv_obj_set_style_text_color(lbl, d->shuffleMode ? themeAccentColor()
                                                        : themeMutedColor(), 0);
        ui_shuffle = d->shuffleMode;
    }

    // Next track info - find next track in queue
    // SKIP FOR RADIO MODE - radio stations don't have a queue/next track
    updateNextTrackUI(d);

    // Repeat
    if (s_repeat != ui_repeat) {
        lv_obj_t* lbl = lv_obj_get_child(btn_repeat, 0);
        // The FONT is the builder's business, not this function's. Hardcoding
        // lv_font_mdi_32 here stamped Classic's face onto whichever theme was
        // active — Amber's 32px icon face on the 4", and on the 7" it pinned the
        // repeat glyph at 32 while every neighbour scaled to 40. The glyph itself
        // is safe to write: font_icon_* chains through MDI, so MDI_REPEAT_ONCE
        // resolves whichever theme built the button.
        //
        // Colour goes through the theme accessors, added for exactly this, so
        // Amber keeps its warm gold instead of being handed COL_ACCENT.
        const bool on = (s_repeat == "ONE" || s_repeat == "ALL");
        lv_label_set_text(lbl, s_repeat == "ONE" ? MDI_REPEAT_ONCE : MDI_REPEAT);
        lv_obj_set_style_text_color(lbl, on ? themeAccentColor() : themeMutedColor(), 0);
        ui_repeat = s_repeat;
    }

    // Album art - only request if URL changed to prevent download loops
    // NOTE: last_art_url is GLOBAL (extern in ui_common.h), don't shadow it!
    updateAlbumArtRequest(d);
    displayCompletedArt();

    // Line-in and TV audio modes take priority over radio mode — checked first.
    // setLineInMode/setTvAudioMode(false) restores UI when transitioning back to music/radio.
    if (d->isLineIn) {
        setTvAudioMode(false);
        updateLineInUI();
        return;
    }
    if (d->isTvAudio) {
        setLineInMode(false);
        updateTvAudioUI();
        return;
    }
    setLineInMode(false);
    setTvAudioMode(false);

    // Radio mode UI adaptation - must be at the END of updateUI()
    updateRadioModeUI();
}

void processUpdates() {
    static uint32_t lastUpdate = 0;
    UIUpdate_t upd;
    bool need = false;
    bool queue_updated = false;
    while (xQueueReceive(sonos.getUIUpdateQueue(), &upd, 0)) {
        need = true;
        if (upd.type == UPDATE_QUEUE) queue_updated = true;
    }
    if (need && (millis() - lastUpdate > 200)) { updateUI(); lastUpdate = millis(); }
    else displayCompletedArt();  // Run even without Sonos events (e.g. art ready while polling suppressed)
    // Auto-refresh queue list if the queue screen is visible when new data arrives
    if (queue_updated) {
        if (lv_screen_active() == scr_queue) refreshQueueList();
        // The Amber drawer floats over scr_main, so the screen check above can
        // never be true for it.
        amberRefreshQueue();
    }
}
