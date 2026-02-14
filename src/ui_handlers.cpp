/**
 * UI Event Handlers and Utilities
 * All event callbacks, WiFi, OTA, brightness control, and UI update functions
 */

#include "ui_common.h"
#include "config.h"
#include "lyrics.h"
#include <esp_task_wdt.h>

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
    int trackNum = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    sonos.playQueueItem(trackNum);
    lv_screen_load(scr_main);
}

// ============================================================================
// Navigation Event Handlers
// ============================================================================
void ev_devices(lv_event_t* e) {
    lv_screen_load(scr_devices);
}

void ev_queue(lv_event_t* e) {
    sonos.updateQueue();
    refreshQueueList();
    lv_screen_load(scr_queue);
}

void ev_settings(lv_event_t* e) {
    lv_screen_load(scr_settings);
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
        lv_obj_set_style_bg_color(btn_sonos_scan, lv_color_hex(0x555555), LV_STATE_DISABLED);
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

// ============================================================================
// WiFi Event Handlers
// ============================================================================
void ev_wifi_scan(lv_event_t* e) {
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
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        // Signal strength by color: green=strong, gold=medium, red=weak
        if (rssi > -50) lv_obj_set_style_text_color(icon, lv_color_hex(0x4ECB71), 0);
        else if (rssi > -70) lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        else lv_obj_set_style_text_color(icon, lv_color_hex(0xFF6B6B), 0);
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

void ev_wifi_connect(lv_event_t* e) {
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
        // Save credentials to NVS
        Serial.printf("[WIFI] Saving credentials to NVS: SSID='%s'\n", selectedSSID.c_str());
        wifiPrefs.putString("ssid", selectedSSID);
        wifiPrefs.putString("pass", pwd);

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

// ============================================================================
// OTA Update Functions
// ============================================================================
static void checkForUpdates() {
    if (WiFi.status() != WL_CONNECTED) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No WiFi connection");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
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
            lv_label_set_text_fmt(lbl_ota_status, LV_SYMBOL_WARNING " Please wait %lu seconds", wait_sec);
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFFA500), 0);
        }
        return;
    }
    last_check_time = now;

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

    http.begin(client, apiUrl);
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.setTimeout(OTA_CHECK_TIMEOUT_MS);

    // CRITICAL: Acquire network_mutex to prevent conflict with album art HTTPS downloads
    if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
        Serial.println("[OTA] Failed to acquire network mutex - check aborted");
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Network busy, try again");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        if (btn_check_update) lv_obj_clear_state(btn_check_update, LV_STATE_DISABLED);
        http.end();
        return;
    }

    // CRITICAL: Wait for general SDIO cooldown (200ms since last network op)
    now = millis();
    unsigned long elapsed = now - last_network_end_ms;
    if (last_network_end_ms > 0 && elapsed < 200) {
        vTaskDelay(pdMS_TO_TICKS(200 - elapsed));
    }

    // CRITICAL: Wait for HTTPS-specific cooldown (2000ms since last HTTPS)
    // Increased from 500ms → 1000ms → 1500ms → 2000ms to prevent SDIO buffer exhaustion
    now = millis();
    elapsed = now - last_https_end_ms;
    if (last_https_end_ms > 0 && elapsed < OTA_HTTPS_COOLDOWN_MS) {
        vTaskDelay(pdMS_TO_TICKS(OTA_HTTPS_COOLDOWN_MS - elapsed));
    }

    int httpCode = http.GET();

    // Read response WHILE holding mutex to prevent TLS traffic overlap
    String payload = "";
    if (httpCode == 200) {
        payload = http.getString();
    }

    // CRITICAL: End HTTP and close TLS BEFORE releasing mutex
    http.end();
    client.stop();
    // Wait for TLS cleanup to complete before releasing mutex
    vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_CLEANUP_MS));

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
                            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No nightly releases found");
                            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
                        }
                        if (lbl_latest_version) {
                            lv_label_set_text(lbl_latest_version, "Latest (Nightly): None");
                        }
                        return;
                    }
                } else {
                    Serial.println("[OTA] Error: Expected array response for nightly channel");
                    if (lbl_ota_status) {
                        lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No nightly releases found");
                        lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
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
                    lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No stable releases found");
                    lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
                }
                if (lbl_latest_version) {
                    lv_label_set_text(lbl_latest_version, "Latest (Stable): None");
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
                        lv_label_set_text(lbl_ota_status, LV_SYMBOL_OK " You're on the latest nightly version!");
                        lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0x4ECB71), 0);
                    }
                    if (lbl_latest_version) {
                        lv_label_set_text_fmt(lbl_latest_version, "Latest (Nightly): v%s", current_version.c_str());
                    }
                    if (btn_install_update) {
                        lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
                    }
                } else {
                    // User is on stable, no nightlies available
                    if (lbl_ota_status) {
                        lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No nightly releases found");
                        lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
                    }
                    if (lbl_latest_version) {
                        lv_label_set_text(lbl_latest_version, "Latest (Nightly): None");
                    }
                }
                return;
            }

            if (lbl_latest_version) {
                if (isPrerelease && ota_channel == 1) {
                    lv_label_set_text_fmt(lbl_latest_version, "Latest (%s): v%s (prerelease)", channelName, latest_version.c_str());
                } else {
                    lv_label_set_text_fmt(lbl_latest_version, "Latest (%s): v%s", channelName, latest_version.c_str());
                }
            }

            Serial.printf("[OTA] Latest %s version: v%s (prerelease: %s)\n",
                          channelName, latest_version.c_str(), isPrerelease ? "yes" : "no");

            // Find firmware.bin asset
            JsonArray assets = releaseObj["assets"];
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
}

// Helper: Restore all tasks and state after OTA failure
static void otaRecovery() {
    // Close HTTP/TLS cleanup
    Serial.println("[OTA] === RECOVERY: Restoring normal operation ===");

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

    // Restart album art task
    if (albumArtTaskHandle == NULL) {
        Serial.println("[OTA] Restarting album art task");
        art_shutdown_requested = false;
        lyrics_shutdown_requested = false;
        xTaskCreatePinnedToCore(albumArtTask, "Art", ART_TASK_STACK_SIZE, NULL, ART_TASK_PRIORITY, &albumArtTaskHandle, 0);
    }

    Serial.println("[OTA] === Recovery complete ===");
}

static void performOTAUpdate() {
    if (download_url.length() == 0) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No update URL found");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
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
        lv_label_set_text(lbl_ota_status, LV_SYMBOL_REFRESH " Preparing update...");
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
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_REFRESH " Waiting for network cleanup...");
        }
        lv_tick_inc(10);
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }

    // ================================================================
    // PHASE 3: STOP ALL BACKGROUND TASKS
    // ================================================================
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, LV_SYMBOL_REFRESH " Stopping background tasks...");
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);

    // Signal ALL tasks to stop FIRST, then wait
    art_abort_download = true;
    art_shutdown_requested = true;
    lyrics_shutdown_requested = true;
    lyrics_abort_requested = true;

    // Stop album art task and WAIT for it to finish
    if (albumArtTaskHandle) {
        Serial.println("[OTA] Waiting for album art task to exit...");
        int wait_count = 0;
        while (albumArtTaskHandle != NULL && wait_count < 100) {  // 10s max
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }

        if (albumArtTaskHandle == NULL) {
            Serial.println("[OTA] Album art task exited cleanly");
            vTaskDelay(pdMS_TO_TICKS(500));  // Let DMA cleanup complete
        } else {
            Serial.println("[OTA] WARNING: Force-killing album art task (may leak DMA)");
            vTaskDelete(albumArtTaskHandle);
            albumArtTaskHandle = NULL;
            vTaskDelay(pdMS_TO_TICKS(1000));  // Extra time for orphaned DMA cleanup
        }
        Serial.printf("[OTA] After art cleanup - Free DMA: %d bytes\n",
            heap_caps_get_free_size(MALLOC_CAP_DMA));
    }

    // Stop lyrics task (now uses its own lyrics_shutdown_requested flag)
    if (lyricsTaskHandle != NULL) {
        Serial.println("[OTA] Waiting for lyrics task to exit...");
        int wait_count = 0;
        while (lyricsTaskHandle != NULL && wait_count < 50) {  // 5s max
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }

        if (lyricsTaskHandle == NULL) {
            Serial.println("[OTA] Lyrics task exited cleanly");
            vTaskDelay(pdMS_TO_TICKS(500));  // Let DMA cleanup complete
        } else {
            Serial.println("[OTA] WARNING: Force-killing lyrics task (may leak DMA)");
            vTaskDelete(lyricsTaskHandle);
            lyricsTaskHandle = NULL;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        Serial.printf("[OTA] After lyrics cleanup - Free DMA: %d bytes\n",
            heap_caps_get_free_size(MALLOC_CAP_DMA));
    }

    // Suspend Sonos tasks
    Serial.println("[OTA] Suspending Sonos tasks...");
    sonos.suspendTasks();

    // Set OTA-in-progress flag
    if (xSemaphoreTake(ota_progress_mutex, pdMS_TO_TICKS(1000))) {
        ota_in_progress = true;
        xSemaphoreGive(ota_progress_mutex);
    }

    // ================================================================
    // PHASE 4: FLUSH WIFI AND VERIFY DMA CLEANUP
    // ================================================================
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, LV_SYMBOL_REFRESH " Freeing memory for download...");
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);

    // Force WiFi to flush pending operations (releases DMA buffers)
    Serial.println("[OTA] Flushing WiFi buffers...");
    WiFi.setSleep(true);
    vTaskDelay(pdMS_TO_TICKS(200));
    WiFi.setSleep(false);

    // ACTIVELY MONITOR free DMA until target reached (verifies cleanup complete)
    Serial.println("[OTA] Verifying DMA memory cleanup...");
    uint32_t wait_start = millis();
    uint32_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);

    while (free_dma < OTA_TARGET_FREE_DMA && (millis() - wait_start) < OTA_DMA_WAIT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        if ((millis() - wait_start) % 1000 < 150) {
            Serial.printf("[OTA] Waiting... Free DMA: %d bytes (target: %d bytes)\n",
                free_dma, OTA_TARGET_FREE_DMA);
        }
    }

    Serial.printf("[OTA] Cleanup complete - Free DMA: %d bytes (waited %lums)\n",
        free_dma, millis() - wait_start);

    if (free_dma < OTA_TARGET_FREE_DMA) {
        Serial.printf("[OTA] WARNING: Only %d bytes free (target %d) - OTA may fail\n",
            free_dma, OTA_TARGET_FREE_DMA);
    }

    // Optimize WiFi for OTA download
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);

    // ================================================================
    // PHASE 5: CONNECT AND DOWNLOAD
    // ================================================================
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, LV_SYMBOL_DOWNLOAD " Connecting to server...");
    }
    if (lbl_ota_progress) {
        lv_label_set_text(lbl_ota_progress, "0%");
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(100));

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    Serial.println("[OTA] ========================================");
    Serial.println("[OTA] STARTING OTA DOWNLOAD");
    Serial.printf("[OTA] Free DMA: %d bytes | Free heap: %d bytes\n",
        heap_caps_get_free_size(MALLOC_CAP_DMA), ESP.getFreeHeap());
    Serial.println("[OTA] ========================================");

    http.begin(client, download_url);
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = http.GET();
    Serial.printf("[OTA] HTTP %d - Free DMA: %d bytes\n", httpCode, heap_caps_get_free_size(MALLOC_CAP_DMA));

    if (httpCode != 200) {
        if (lbl_ota_status) {
            lv_label_set_text_fmt(lbl_ota_status, LV_SYMBOL_WARNING " Download failed (HTTP %d)", httpCode);
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        http.end();
        otaRecovery();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0 || contentLength > OTA_MAX_FIRMWARE_SIZE) {
        Serial.printf("[OTA] Invalid firmware size: %d bytes\n", contentLength);
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Invalid firmware file");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        http.end();
        otaRecovery();
        return;
    }

    Serial.printf("[OTA] Firmware size: %d bytes (%.1f KB)\n", contentLength, contentLength / 1024.0);

    if (!Update.begin(contentLength)) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Not enough space for OTA");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        http.end();
        otaRecovery();
        return;
    }

    // ================================================================
    // PHASE 6: DOWNLOAD WITH ADAPTIVE THROTTLING
    // ================================================================
    if (lbl_ota_status) {
        lv_label_set_text(lbl_ota_status, LV_SYMBOL_DOWNLOAD " Downloading firmware...");
    }
    lv_tick_inc(10);
    lv_refr_now(NULL);

    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    int lastPercent = -1;
    uint32_t lastUIUpdate = millis();
    static uint8_t buff[OTA_BUFFER_SIZE];

    // CRITICAL: Let SDIO buffers settle after TLS handshake
    // TLS session consumes ~114KB DMA, leaving only ~5KB for SDIO
    Serial.println("[OTA] Stabilizing SDIO after TLS handshake...");
    vTaskDelay(pdMS_TO_TICKS(OTA_SETTLE_AFTER_TLS_MS));
    Serial.printf("[OTA] Starting download - Free DMA: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_DMA));

    int chunk_count = 0;
    uint32_t download_start = millis();
    uint32_t last_data_time = millis();  // Track last time we received data (stall detection)

    while (http.connected() && (written < (size_t)contentLength)) {
        // STALL DETECTION: Abort if no data received for OTA_STALL_TIMEOUT_MS
        if ((millis() - last_data_time) > OTA_STALL_TIMEOUT_MS) {
            Serial.printf("[OTA] STALL DETECTED: No data for %dms at %d%% - aborting\n",
                OTA_STALL_TIMEOUT_MS, (int)(written * 100 / contentLength));
            if (lbl_ota_status) {
                lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Download stalled - network timeout");
                lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
            }
            lv_tick_inc(10);
            lv_refr_now(NULL);
            Update.abort();
            http.end();
            otaRecovery();
            return;
        }

        // OVERALL TIMEOUT: Abort if total download time exceeds limit
        if ((millis() - download_start) > OTA_DOWNLOAD_TIMEOUT_MS) {
            Serial.printf("[OTA] TIMEOUT: Download took >%ds at %d%% - aborting\n",
                OTA_DOWNLOAD_TIMEOUT_MS / 1000, (int)(written * 100 / contentLength));
            if (lbl_ota_status) {
                lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " Download timeout - try again");
                lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
            }
            lv_tick_inc(10);
            lv_refr_now(NULL);
            Update.abort();
            http.end();
            otaRecovery();
            return;
        }

        size_t available = stream->available();
        if (available) {
            last_data_time = millis();  // Reset stall timer on data

            // Limit read size to reduce SDIO burst pressure (config-driven)
            size_t toRead = (available < OTA_READ_SIZE) ? available : OTA_READ_SIZE;
            if (toRead > sizeof(buff)) toRead = sizeof(buff);
            int bytesRead = stream->readBytes(buff, toRead);
            written += Update.write(buff, bytesRead);

            // Adaptive throttling: fast base delay, slows only when DMA is under pressure
            chunk_count++;
            if (chunk_count % OTA_DMA_CHECK_INTERVAL == 0) {
                size_t cur_free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
                if (cur_free_dma < OTA_DMA_CRITICAL) {
                    vTaskDelay(pdMS_TO_TICKS(80));   // DMA nearly exhausted - back off
                } else if (cur_free_dma < OTA_DMA_LOW) {
                    vTaskDelay(pdMS_TO_TICKS(30));   // DMA low - moderate throttle
                } else {
                    vTaskDelay(pdMS_TO_TICKS(OTA_BASE_DELAY_MS));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(OTA_BASE_DELAY_MS));
            }

            // Feed watchdog EVERY chunk (download can take 60+ seconds)
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

                // Refresh display and log every OTA_PROGRESS_LOG_INTERVAL%
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
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // ================================================================
    // PHASE 7: VERIFY AND INSTALL
    // ================================================================
    if (written == (size_t)contentLength) {
        // DOWNLOAD COMPLETE - Show 100%
        if (bar_ota_progress) lv_bar_set_value(bar_ota_progress, 100, LV_ANIM_OFF);
        if (lbl_ota_progress) lv_label_set_text(lbl_ota_progress, "100%");
        if (lbl_ota_status) lv_label_set_text(lbl_ota_status, LV_SYMBOL_OK " Download complete!");
        lv_tick_inc(10);
        lv_refr_now(NULL);

        Serial.printf("[OTA] Download complete: %d bytes in %lus\n", written, (millis() - download_start) / 1000);
        vTaskDelay(pdMS_TO_TICKS(500));

        // START INSTALL
        if (bar_ota_progress) lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
        if (lbl_ota_progress) lv_label_set_text(lbl_ota_progress, "");
        if (lbl_ota_status) lv_label_set_text(lbl_ota_status, LV_SYMBOL_REFRESH " Installing & verifying...");
        lv_tick_inc(10);
        lv_refr_now(NULL);

        // Animate install progress
        for (int i = 0; i <= 100; i += 10) {
            if (bar_ota_progress) lv_bar_set_value(bar_ota_progress, i, LV_ANIM_OFF);
            lv_tick_inc(50);
            lv_refr_now(NULL);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    } else {
        // Incomplete download
        Serial.printf("[OTA] Incomplete download: %d/%d bytes\n", written, contentLength);
        if (lbl_ota_status) {
            lv_label_set_text_fmt(lbl_ota_status, LV_SYMBOL_WARNING " Incomplete download (%d%%)", (int)(written * 100 / contentLength));
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        lv_tick_inc(10);
        lv_refr_now(NULL);
        Update.abort();
        http.end();
        otaRecovery();
        return;
    }

    if (Update.end()) {
        if (Update.isFinished()) {
            // INSTALL COMPLETE - Clean screen and show reboot message
            lv_obj_clean(lv_screen_active());
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);

            lv_obj_t *reboot_label = lv_label_create(lv_screen_active());
            lv_label_set_text(reboot_label, "REBOOTING...");
            lv_obj_set_style_text_color(reboot_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(reboot_label, &lv_font_montserrat_24, 0);
            lv_obj_center(reboot_label);

            lv_tick_inc(10);
            lv_refr_now(NULL);
            vTaskDelay(pdMS_TO_TICKS(1000));

            display_set_brightness(0);
            vTaskDelay(pdMS_TO_TICKS(100));

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

    http.end();
    otaRecovery();
}

void ev_check_update(lv_event_t* e) {
    checkForUpdates();
}

void ev_install_update(lv_event_t* e) {
    performOTAUpdate();
}

// ============================================================================
// UI Update Function
// ============================================================================
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

    // Fetch synced lyrics when track changes
    static String lyrics_last_track = "";
    String lyrics_key = d->currentArtist + "|" + d->currentTrack;
    if (lyrics_key != lyrics_last_track && d->currentTrack.length() > 0) {
        lyrics_last_track = lyrics_key;
        if (lyrics_enabled && !d->isRadioStation && d->durationSeconds > 0) {
            requestLyrics(d->currentArtist, d->currentTrack, d->durationSeconds);
        } else {
            clearLyrics();
        }
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

    // Update synced lyrics display and status indicator
    updateLyricsDisplay(d->relTimeSeconds);
    updateLyricsStatus();  // Update status indicator from main thread

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
    // SKIP FOR RADIO MODE - radio stations don't have a queue/next track
    static String last_next_title = "";
    if (!d->isRadioStation && d->queueSize > 0 && d->currentTrackNumber > 0) {
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

    // Album art - only request if URL changed to prevent download loops
    // NOTE: last_art_url is GLOBAL (extern in ui_common.h), don't shadow it!
    static String last_track_uri = "";
    static String last_source_prefix = "";

    // Extract source prefix to detect actual source changes (not just track changes)
    String current_source_prefix = "";
    if (d->currentURI.startsWith("x-sonos-vli:")) {
        current_source_prefix = "x-sonos-vli";  // Spotify, Apple Music, etc
    } else if (d->currentURI.startsWith("hls-radio://")) {
        current_source_prefix = "hls-radio";  // Radio
    } else if (d->currentURI.startsWith("x-sonos-http:")) {
        current_source_prefix = "x-sonos-http";  // Radio
    } else if (d->currentURI.startsWith("x-rincon-mp3radio:")) {
        current_source_prefix = "x-rincon-mp3radio";  // Radio
    } else {
        // Extract first part before colon for unknown sources
        int colonPos = d->currentURI.indexOf(':');
        if (colonPos > 0) {
            current_source_prefix = d->currentURI.substring(0, colonPos);
        }
    }

    // Detect ACTUAL source changes (Spotify→Radio, not Spotify track1→track2)
    bool actual_source_change = (current_source_prefix != last_source_prefix && current_source_prefix.length() > 0);

    // Detect any URI change (track or source)
    bool uri_changed = (d->currentURI != last_track_uri);

    if (uri_changed && d->currentURI.length() > 0) {
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
        // CRITICAL: Must hold art_mutex when writing last_art_url - the art task reads
        // it under mutex, and String assignment is not atomic (race condition → corruption)
        if (xSemaphoreTake(art_mutex, pdMS_TO_TICKS(50))) {
            last_art_url = "";  // Force art refresh on any URI change
            xSemaphoreGive(art_mutex);
        }
        last_track_uri = d->currentURI;
    }

    // Request album art if URL provided and (URL changed or track changed)
    // Note: Don't compare against last_art_url (HTTP) since d->albumArtURL is HTTPS
    // Let art task handle deduplication after HTTP conversion
    // For radio: also check radioStationArtURL if albumArtURL is empty
    bool hasArt = (d->albumArtURL.length() > 0) || (d->isRadioStation && d->radioStationArtURL.length() > 0);
    bool artChanged = (d->albumArtURL != pending_art_url) || uri_changed;

    // For radio stations: also check if radioStationArtURL changed (even if albumArtURL is empty)
    if (d->isRadioStation && d->radioStationArtURL.length() > 0 && d->radioStationArtURL != pending_art_url) {
        artChanged = true;
    }

    if (hasArt && artChanged) {
        String artURL = "";
        bool usingStationLogo = false;  // Track if we're using station logo (PNG allowed)

        // Determine which art to use
        if (d->albumArtURL.length() > 0) {
            artURL = d->albumArtURL;
        }

        // RADIO STATION LOGO FALLBACK:
        // If playing radio and no song art available, use station logo instead
        if (d->isRadioStation) {
            bool hasSongArt = (artURL.length() > 0);
            bool hasStationLogo = (d->radioStationArtURL.length() > 0);
            Serial.printf("[ART] Radio check - hasSongArt=%d, hasStationLogo=%d, artURL='%s', stationURL='%s'\n",
                         hasSongArt, hasStationLogo, artURL.c_str(), d->radioStationArtURL.c_str());

            // If no song art but have station logo, use the logo
            if (!hasSongArt && hasStationLogo) {
                artURL = d->radioStationArtURL;
                usingStationLogo = true;
                Serial.println("[ART] Radio: Using station logo (no song art)");
            }
            // If song art is just a generic Sonos radio icon, prefer the actual station logo
            else if (hasSongArt && hasStationLogo && artURL.indexOf("/getaa?") > 0) {
                // Check if it's pointing to the radio URI (generic icon)
                if (artURL.indexOf("x-sonosapi-stream") > 0 ||
                    artURL.indexOf("x-rincon-mp3radio") > 0 ||
                    artURL.indexOf("x-sonosapi-radio") > 0) {
                    artURL = d->radioStationArtURL;
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
            // Don't set last_art_url here - let art task manage it (HTTP vs HTTPS conversion)
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

    // Radio mode UI adaptation - must be at the END of updateUI()
    updateRadioModeUI();
}

void processUpdates() {
    static uint32_t lastUpdate = 0;
    UIUpdate_t upd;
    bool need = false;
    while (xQueueReceive(sonos.getUIUpdateQueue(), &upd, 0)) need = true;
    if (need && (millis() - lastUpdate > 200)) { updateUI(); lastUpdate = millis(); }
}
