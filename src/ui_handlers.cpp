/**
 * UI Event Handlers and Utilities
 * All event callbacks, WiFi, OTA, brightness control, and UI update functions
 */

#include "ui_common.h"

// ============================================================================
// Brightness Control
// ============================================================================
void setBrightness(int level) {
    brightness_level = constrain(level, 10, 100);  // 10-100% range
    display_set_brightness(brightness_level);
    wifiPrefs.putInt("brightness", brightness_level);
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

static void performOTAUpdate() {
    if (download_url.length() == 0) {
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, LV_SYMBOL_WARNING " No update URL found");
            lv_obj_set_style_text_color(lbl_ota_status, lv_color_hex(0xFF6B6B), 0);
        }
        return;
    }

    // CRITICAL: Stop album art task gracefully to prevent WiFi buffer overflow during OTA
    // Album art downloads can compete with OTA firmware download for WiFi TX/RX buffers
    if (albumArtTaskHandle) {
        Serial.println("[OTA] Requesting album art shutdown");
        art_shutdown_requested = true;

        // Wait for task to finish current operation and exit (max 3 seconds)
        int wait_count = 0;
        while (albumArtTaskHandle != NULL && wait_count < 30) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }

        if (albumArtTaskHandle == NULL) {
            Serial.println("[OTA] Album art task stopped successfully");
        } else {
            Serial.println("[OTA] Album art task did not stop - forcing delete");
            vTaskDelete(albumArtTaskHandle);
            albumArtTaskHandle = NULL;
        }
    }

    // CRITICAL: Suspend Sonos polling tasks to prevent WiFi buffer overflow during OTA
    // Sonos SOAP requests can compete with OTA firmware download for WiFi TX/RX buffers
    Serial.println("[OTA] Suspending Sonos tasks");
    sonos.suspendTasks();

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

            // Dim backlight heavily during download to hide flash write flicker
            int original_brightness = brightness_level;
            display_set_brightness(5);  // 5% brightness during download

            WiFiClient* stream = http.getStreamPtr();
            size_t written = 0;
            // Use 16KB buffer to reduce flash write frequency and minimize blue flicker
            // (Each flash write disables external memory cache, causing RGB LCD PSRAM access issues)
            static uint8_t buff[16384];  // 16KB - reduces ~500KB firmware to ~31 flash writes instead of 500
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
                // Restore brightness after download
                display_set_brightness(original_brightness);

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
                    // INSTALL COMPLETE - Clean screen and show "REBOOTING..." message
                    lv_obj_clean(lv_screen_active());  // Remove all children
                    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);

                    // Create centered "REBOOTING..." label
                    lv_obj_t *reboot_label = lv_label_create(lv_screen_active());
                    lv_label_set_text(reboot_label, "REBOOTING...");
                    lv_obj_set_style_text_color(reboot_label, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_center(reboot_label);

                    lv_tick_inc(10);
                    lv_refr_now(NULL);
                    vTaskDelay(pdMS_TO_TICKS(1000));

                    // Turn off backlight before restart to avoid blue screen flash
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

    // Restart album art task (if update failed - successful update will restart device)
    if (albumArtTaskHandle == NULL) {
        Serial.println("[OTA] Restarting album art task");
        art_shutdown_requested = false;
        xTaskCreatePinnedToCore(albumArtTask, "Art", 8192, NULL, 1, &albumArtTaskHandle, 0);
    }

    // Resume Sonos tasks (if update failed - successful update will restart device)
    Serial.println("[OTA] Resuming Sonos tasks");
    sonos.resumeTasks();
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
    static String last_art_url = "";
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
            last_source_change_time = millis();  // Track when SOURCE changed for WiFi buffer management
            last_source_prefix = current_source_prefix;
            // CRITICAL: Abort any in-progress album art download immediately
            // This prevents Spotify download from blocking YouTube Music download
            art_abort_download = true;
        } else {
            Serial.printf("[ART] Track changed (same source: %s)\n", current_source_prefix.c_str());
        }
        last_art_url = "";  // Force art refresh on any URI change
        last_track_uri = d->currentURI;
    }

    // Request album art if URL changed or URI changed
    if (d->albumArtURL != last_art_url || uri_changed) {
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
            last_art_url = artURL;  // Track the actual URL we requested
        } else {
            // No art available - clear display and tracking
            if (last_art_url.length() > 0) {
                Serial.println("[ART] No art URL - clearing display");
                if (img_album) lv_obj_add_flag(img_album, LV_OBJ_FLAG_HIDDEN);
                if (art_placeholder) lv_obj_remove_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);
                last_art_url = "";
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
