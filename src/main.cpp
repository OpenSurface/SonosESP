/**
 * ESP32-S3 Sonos Controller
 * 800x480 RGB Display with Touch
 * Modern UI matching reference design
 */

#include "ui_common.h"

// Sonos logo
LV_IMG_DECLARE(Sonos_idnu60bqes_1);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== SONOS CONTROLLER ===");
    Serial.printf("Free heap: %d, PSRAM: %d\n", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize preferences with debug logging
    wifiPrefs.begin("sonos_wifi", false);
    String ssid = wifiPrefs.getString("ssid", DEFAULT_WIFI_SSID);
    String pass = wifiPrefs.getString("pass", DEFAULT_WIFI_PASSWORD);

    // Debug: Log what was loaded from NVS
    if (ssid.length() > 0) {
        Serial.printf("[WIFI] Loaded from NVS: SSID='%s' (pass length: %d)\n", ssid.c_str(), pass.length());
    } else {
        Serial.println("[WIFI] No saved credentials found in NVS, using defaults");
    }

    // Load display settings from NVS (defaults: brightness=100%, dimmed=20%, autodim=30sec)
    brightness_level = wifiPrefs.getInt("brightness", 100);
    brightness_dimmed = wifiPrefs.getInt("brightness_dimmed", 20);
    autodim_timeout = wifiPrefs.getInt("autodim_sec", 30);
    Serial.printf("[DISPLAY] Loaded settings from NVS: brightness=%d%%, dimmed=%d%%, autodim=%dsec\n",
                  brightness_level, brightness_dimmed, autodim_timeout);

    // Brightness will be set after display_init() is called
    Serial.println("[DISPLAY] ESP32-P4 uses ST7701 backlight control (no PWM needed)");

    WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for WiFi hardware
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[WIFI] Connecting to '%s'", ssid.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
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
    updateBootProgress(83);

    createGroupsScreen();
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
