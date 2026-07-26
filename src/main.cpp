/**
 * ESP32-P4 Sonos Controller
 * 480x800 MIPI DSI Display with Touch
 * Modern UI matching reference design
 */

#include "ui_common.h"
#include "config.h"
#include "lyrics.h"
#include "clock_screen.h"
#include "ui_theme.h"
#include <esp_flash.h>
#include <esp_task_wdt.h>
// Sonos logo
LV_IMG_DECLARE(Sonos_idnu60bqes_1);

static volatile bool sonos_started = false;  // true once Sonos tasks are running (written by deferred task, read by mainAppTask)
static volatile bool deferred_discovery_running = false;  // guards the one-shot deferred discovery task (issue #69)
static TaskHandle_t mainAppTaskHandle = nullptr;
static void mainAppTask(void* param);            // forward declaration — defined after loop()
static void deferredDiscoveryTask(void* param);  // forward declaration — one-shot, runs off mainAppTask

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(500);
    Serial.println("\n=== SONOS CONTROLLER ===");
    Serial.printf("Free heap: %d, PSRAM: %d\n", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Detect flash chip - auto-suspend only works with specific chips
    uint32_t flash_id = 0;
    esp_err_t ret = esp_flash_read_id(esp_flash_default_chip, &flash_id);

    if (ret == ESP_OK) {
        uint8_t mfg_id = (flash_id >> 16) & 0xFF;
        uint8_t capacity_id = flash_id & 0xFF;
        int flash_size_mb = (1 << capacity_id) / (1024 * 1024);

        const char* mfg_name;
        switch(mfg_id) {
            case 0x68: mfg_name = "Boya BY25Q"; break;
            case 0xC8: mfg_name = "GigaDevice GD25"; break;
            case 0x20: mfg_name = "XMC XM25"; break;
            case 0xEF: mfg_name = "Winbond W25"; break;
            case 0x1C: mfg_name = "EON EN25"; break;
            case 0xA1: mfg_name = "Fudan FM25"; break;
            default:   mfg_name = "Unknown"; break;
        }

        // Check if flash supports auto-suspend (ESP-IDF: GD25QxxE, XM25QxxC, FM25Q32)
        bool suspend_ok = (mfg_id == 0xC8 || mfg_id == 0x20 || mfg_id == 0xA1);
        Serial.printf("[FLASH] %s %dMB (0x%06X) - Auto-suspend: %s\n",
                      mfg_name, flash_size_mb, flash_id, suspend_ok ? "YES" : "NO");
    }

    // Create network mutex to serialize WiFi access (prevents SDIO buffer overflow)
    network_mutex = xSemaphoreCreateMutex();

    // Create OTA progress mutex to protect OTA state and UI updates
    ota_progress_mutex = xSemaphoreCreateMutex();

    // Initialize preferences with debug logging
    wifiPrefs.begin(NVS_NAMESPACE, false);
    String ssid = wifiPrefs.getString(NVS_KEY_SSID, DEFAULT_WIFI_SSID);
    String pass = wifiPrefs.getString(NVS_KEY_PASSWORD, DEFAULT_WIFI_PASSWORD);

    // Debug: Log what was loaded from NVS
    if (ssid.length() > 0) {
        Serial.printf("[WIFI] Loaded from NVS: SSID='%s' (pass length: %d)\n", ssid.c_str(), pass.length());
    } else {
        Serial.println("[WIFI] No saved credentials found in NVS, using defaults");
    }

    // Load display settings from NVS
    brightness_level = wifiPrefs.getInt(NVS_KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    brightness_dimmed = wifiPrefs.getInt(NVS_KEY_BRIGHTNESS_DIM, DEFAULT_BRIGHTNESS_DIM);
    autodim_timeout = wifiPrefs.getInt(NVS_KEY_AUTODIM, DEFAULT_AUTODIM_SEC);
    lyrics_enabled = wifiPrefs.getBool(NVS_KEY_LYRICS, true);
    Serial.printf("[DISPLAY] Loaded settings from NVS: brightness=%d%%, dimmed=%d%%, autodim=%dsec, lyrics=%s\n",
                  brightness_level, brightness_dimmed, autodim_timeout, lyrics_enabled ? "on" : "off");

    // Load clock settings from NVS
    clock_mode           = wifiPrefs.getInt(NVS_KEY_CLOCK_MODE,    CLOCK_DEFAULT_MODE);
    clock_timeout_min    = wifiPrefs.getInt(NVS_KEY_CLOCK_TIMEOUT,  CLOCK_DEFAULT_TIMEOUT);
    clock_tz_idx         = wifiPrefs.getInt(NVS_KEY_CLOCK_TZ,       CLOCK_DEFAULT_TZ_IDX);
    clock_picsum_enabled = wifiPrefs.getBool(NVS_KEY_CLOCK_PICSUM,  (bool)CLOCK_DEFAULT_PICSUM);
    clock_refresh_min    = wifiPrefs.getInt(NVS_KEY_CLOCK_REFRESH,  CLOCK_DEFAULT_REFRESH);
    clock_bg_kw_idx      = wifiPrefs.getInt(NVS_KEY_CLOCK_KW,       CLOCK_DEFAULT_KW_IDX);
    clock_12h            = wifiPrefs.getBool(NVS_KEY_CLOCK_12H,     (bool)CLOCK_DEFAULT_12H);
    // Clamp indices in case lists changed between firmware versions
    if (clock_tz_idx    < 0 || clock_tz_idx    >= CLOCK_ZONES_COUNT)   clock_tz_idx    = 0;
    if (clock_bg_kw_idx < 0 || clock_bg_kw_idx >= CLOCK_BG_KW_COUNT)   clock_bg_kw_idx = 0;
    clock_weather_enabled  = wifiPrefs.getBool(NVS_KEY_CLOCK_WEATHER_EN,   (bool)CLOCK_DEFAULT_WEATHER_EN);
    clock_weather_city_idx = wifiPrefs.getInt(NVS_KEY_CLOCK_WEATHER_CITY,  CLOCK_DEFAULT_WEATHER_CITY);
    // Index range: 0..CLOCK_CITY_COUNT-1 = predefined cities (0 = Auto); CLOCK_CITY_COUNT = Custom (issue #74).
    if (clock_weather_city_idx < 0 || clock_weather_city_idx > CLOCK_LOC_CUSTOM_IDX) clock_weather_city_idx = 0;
    clock_wx_fahrenheit    = wifiPrefs.getBool(NVS_KEY_CLOCK_WEATHER_FAHR, (bool)CLOCK_DEFAULT_WEATHER_FAHR);
    // Custom location override (#74) — load as String from NVS (text form, full precision),
    // then parse into the atomic float globals the bg task reads. 0.0f = unset.
    {
        String lat_s  = wifiPrefs.getString(NVS_KEY_CLOCK_WX_CUSTOM_LAT,  "");
        String lon_s  = wifiPrefs.getString(NVS_KEY_CLOCK_WX_CUSTOM_LON,  "");
        String name_s = wifiPrefs.getString(NVS_KEY_CLOCK_WX_CUSTOM_NAME, "");
        clock_custom_lat = (lat_s.length() > 0) ? lat_s.toFloat() : 0.0f;
        clock_custom_lon = (lon_s.length() > 0) ? lon_s.toFloat() : 0.0f;
        strlcpy(clock_custom_name, name_s.c_str(), sizeof(clock_custom_name));
    }
    Serial.printf("[CLOCK] mode=%d timeout=%dmin tz=%s picsum=%s refresh=%dmin kw=%s 12h=%s weather=%s city=%s\n",
                  clock_mode, clock_timeout_min,
                  CLOCK_ZONES[clock_tz_idx].name,
                  clock_picsum_enabled ? "on" : "off", clock_refresh_min,
                  CLOCK_BG_KEYWORDS[clock_bg_kw_idx].label,
                  clock_12h ? "yes" : "no",
                  clock_weather_enabled ? "on" : "off",
                  CLOCK_CITIES[clock_weather_city_idx].label);

    // Brightness will be set after display_init() is called
    Serial.println("[DISPLAY] ESP32-P4 uses ST7701 backlight control (no PWM needed)");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // keep C6 radio always active — no modem sleep on mains-powered device
    // ESP32-C6 WiFi initialization delay - fixes ESP-Hosted SDIO timing issues
    vTaskDelay(pdMS_TO_TICKS(WIFI_INIT_DELAY_MS));
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[WIFI] Connecting to '%s'", ssid.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < WIFI_CONNECT_RETRIES) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WIFI] Connected - IP: %s\n", WiFi.localIP().toString().c_str());
        // Start NTP sync (SNTP daemon — no HTTPS, tiny UDP packets)
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        // Apply user-selected timezone via POSIX TZ string
        setenv("TZ", CLOCK_ZONES[clock_tz_idx].posix, 1);
        tzset();
        Serial.printf("[NTP] Sync started, TZ=%s\n", CLOCK_ZONES[clock_tz_idx].name);
    } else {
        Serial.println("\n[WIFI] Connection failed - will retry from settings");
    }

    // === Memory map logged once at boot (post-WiFi, pre-LVGL) ===
    // Used to diagnose DMA depletion: compare to runtime [ART/*/MEM] logs.
    // DMA SRAM is the crash-critical pool — WiFi/TCP/JPEG all draw from it.
    {
        size_t dma_free    = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t dma_total   = heap_caps_get_total_size(MALLOC_CAP_DMA);
        size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        size_t int_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t int_total   = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        size_t wifi_used   = dma_total > dma_free ? dma_total - dma_free : 0;
        Serial.println("\n=== MEMORY MAP (post-WiFi, pre-LVGL) ===");
        Serial.printf("  DMA SRAM:   %4uKB free / %4uKB total  (WiFi permanent: %uKB)\n",
                      dma_free/1024, dma_total/1024, wifi_used/1024);
        Serial.printf("  PSRAM:      %4uKB free / %4uKB total\n",
                      psram_free/1024, psram_total/1024);
        Serial.printf("  IRAM/DRAM:  %4uKB free / %4uKB total\n",
                      int_free/1024, int_total/1024);
        Serial.println("  --- DMA SRAM consumer estimates ---");
        Serial.printf("  WiFi/SDIO permanent:     ~%uKB (pkt_rxbuff, DMA descs, HMAC, LMAC)\n",
                      wifi_used/1024);
        Serial.printf("  lwIP TIME_WAIT PCBs:     0-??KB (variable; use [SOAP/DMA] logs)\n");
        Serial.printf("  Art TCP SO_RCVBUF=8KB:   ~9KB  (during art HTTP download only)\n");
        Serial.printf("  JPEG HW decode output:   ~??KB (log [ART/pre-decode vs post-decode] MEM)\n");
        Serial.printf("  mbedTLS HTTPS session:   ~5KB  (during lyrics/clock HTTPS only)\n");
        Serial.printf("  Safe idle floor:         ~%uKB (ART_MIN_FREE_DMA threshold)\n",
                      ART_MIN_FREE_DMA/1024);
        Serial.println("  --- PSRAM consumer estimates ---");
        Serial.printf("  LVGL frame bufs: ~%uKB (2 x %ux%ux2)\n",
                      2*DISPLAY_WIDTH*DISPLAY_HEIGHT*2/1024, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        Serial.printf("  Art LRU cache:   ~230KB (2 slots x 240x240x2)\n");
        Serial.printf("  Art task stack:    %uKB\n", ART_TASK_STACK_SIZE/1024);
        Serial.printf("  Art download buf:  %uKB max (alloc+free per download)\n",
                      ART_MAX_DOWNLOAD_SIZE/1024);
        Serial.println("  --- Internal SRAM task stacks ---");
        Serial.printf("  mainAppTask: %uKB  SonosPoll: %uKB  SonosNet: %uKB\n",
                      MAIN_APP_TASK_STACK/1024, SONOS_POLL_TASK_STACK/1024, SONOS_NET_TASK_STACK/1024);
        Serial.printf("  Lyrics: %uKB  ClockBG: %uKB\n",
                      LYRICS_TASK_STACK/1024, CLOCK_BG_TASK_STACK/1024);
        Serial.println("=========================================\n");
    }

    lv_init();
    if (!display_init()) { Serial.println("Display FAIL"); while(1) delay(1000); }
    if (!touch_init()) { Serial.println("Touch FAIL"); while(1) delay(1000); }

    // Initialize hardware watchdog timer - auto-reboot if system hangs
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,  // Don't watch idle tasks
        .trigger_panic = true // Reboot on timeout
    };
    esp_task_wdt_reconfigure(&wdt_config);
    // mainAppTask registers itself with the watchdog on startup (not loopTask — it becomes idle)
    Serial.printf("[WDT] Watchdog configured: %d sec timeout\n", WATCHDOG_TIMEOUT_SEC);

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
    lv_obj_align(img_logo, LV_ALIGN_CENTER, 0, SY(-30));
    // Scale down significantly (256 = 100%, so 80 = ~31% size, 100 = ~39% size)
    lv_image_set_scale(img_logo, 130);  // Smaller - about 25% of original size

    // Create animated progress bar below logo
    lv_obj_t* boot_bar = lv_bar_create(boot_scr);
    lv_obj_set_size(boot_bar, SX(300), SY(8));
    lv_obj_align(boot_bar, LV_ALIGN_CENTER, 0, SY(80));
    lv_obj_set_style_bg_color(boot_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(boot_bar, lv_color_hex(0xD4A84B), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(boot_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(boot_bar, 0, 100);
    lv_bar_set_value(boot_bar, 0, LV_ANIM_OFF);

    // Version number in bottom right corner
    lv_obj_t* lbl_boot_version = lv_label_create(boot_scr);
    lv_label_set_text(lbl_boot_version, "v" FIRMWARE_VERSION);
    lv_obj_set_style_text_color(lbl_boot_version, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_boot_version, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_boot_version, LV_ALIGN_BOTTOM_RIGHT, SX(-10), SY(-10));

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

    // Initialize lyrics PSRAM buffer before creating screens
    initLyrics();

    // Resolve the saved player theme BEFORE the first build — createMainScreen()
    // dispatches to the active theme's builder. Clamps stale/invalid indices.
    themeLoad();

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

    // =========================================================================
    // BOOT OTA FAST PATH — before any background tasks start
    // =========================================================================
    // If ev_install_update() saved a URL to NVS and restarted, run the OTA
    // download RIGHT HERE, before art/Sonos/lyrics tasks are created.
    //
    // Why this matters (DMA budget):
    //   With tasks running: ~105KB DMA free → TLS uses ~71KB → ~34KB post-TLS
    //     → SDIO RX pool + AES alignment + Update.begin() all fight over 34KB → crash
    //   At this boot point: ~125KB DMA free → TLS uses ~71KB → ~54KB post-TLS
    //     → plenty of headroom for SDIO (~16KB) + AES + Update.begin() (~6KB)
    //
    // PSRAM is irrelevant: flash writes and TLS buffers use DMA SRAM only.
    // wifiPrefs is already open (read-write) from setup() — no new handle needed.
    //
    // If OTA is pending but the initial WiFi connect timed out, wait up to 30 extra seconds.
    // Some routers/channels take 30–40s to assign an IP — the 20s initial window can be too short.
    // We must NOT call triggerPendingOTA() without WiFi — it would silently fail and clear the URL.
    if (wifiPrefs.getBool(NVS_KEY_OTA_PENDING, false) && WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] Boot OTA pending — waiting for WiFi...");
        lv_obj_t* lbl_ota_wifi = lv_label_create(boot_scr);
        lv_obj_set_style_text_color(lbl_ota_wifi, lv_color_hex(0xD4A84B), 0);
        lv_obj_set_style_text_font(lbl_ota_wifi, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl_ota_wifi, LV_ALIGN_CENTER, 0, SY(50));
        lv_label_set_text_fmt(lbl_ota_wifi, "Waiting for WiFi: %s ...", ssid.c_str());
        lv_refr_now(NULL);
        int ota_wifi_tries = 0;
        while (WiFi.status() != WL_CONNECTED && ota_wifi_tries++ < 120) {  // up to 60s extra
            vTaskDelay(pdMS_TO_TICKS(500));
            // At 15s: if still not connected, disconnect and retry WiFi.begin()
            // Handles SDIO/C6 re-init stall after OTA firmware flash + restart
            if (ota_wifi_tries == 30) {
                Serial.println("[OTA] WiFi stalled — retrying WiFi.begin()");
                lv_label_set_text_fmt(lbl_ota_wifi, "Retrying WiFi: %s ...", ssid.c_str());
                lv_refr_now(NULL);
                WiFi.disconnect();
                vTaskDelay(pdMS_TO_TICKS(WIFI_INIT_DELAY_MS));
                WiFi.begin(ssid.c_str(), pass.c_str());
            }
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[OTA] WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            setenv("TZ", CLOCK_ZONES[clock_tz_idx].posix, 1);
            tzset();
        } else {
            Serial.println("[OTA] WiFi still not connected after 60s — skipping boot OTA");
            wifiPrefs.putBool(NVS_KEY_OTA_PENDING, false);  // clear flag to avoid infinite reboot loop
        }
        lv_obj_del(lbl_ota_wifi);
    }
    if (WiFi.status() == WL_CONNECTED && wifiPrefs.getBool(NVS_KEY_OTA_PENDING, false)) {
        wifiPrefs.putBool(NVS_KEY_OTA_PENDING, false);  // clear immediately — prevent reboot loops
        Serial.printf("[OTA] Boot OTA: %d bytes DMA free (pre-task)\n",
                      heap_caps_get_free_size(MALLOC_CAP_DMA));
        esp_task_wdt_add(NULL);  // subscribe loopTask — performOTAUpdate() calls esp_task_wdt_reset()
                                 // which spams "task not found" errors if the calling task isn't subscribed
        triggerPendingOTA();  // loads saved URL → performOTAUpdate() → ESP.restart() on success
        // If we reach here, all download retries failed (otaRecovery() was called).
        // Restart to return to normal operation; NVS_KEY_OTA_PENDING is already false.
        vTaskDelay(pdMS_TO_TICKS(5000));  // let user read the error message
        ESP.restart();
    }

    createSourcesScreen();
    updateBootProgress(83);

    createGroupsScreen();
    createGeneralScreen();
    createClockScreen();
    createClockSettingsScreen();
    updateBootProgress(85);

    art_mutex = xSemaphoreCreateMutex();
    createArtTask();  // PSRAM stack — frees 20KB internal SRAM for SDIO/WiFi DMA
    updateBootProgress(90);

    sonos.begin();
    updateBootProgress(95);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[SONOS] WiFi not connected at boot - deferring discovery");
    } else {
        // Try to load cached device first for fast boot (~2s vs ~15s)
        bool loadedFromCache = sonos.tryLoadCachedDevice();
        if (loadedFromCache) {
            sonos.selectDevice(0);
            sonos.startTasks();
            sonos_started = true;
        } else {
            // Cache miss or unreachable - skip SSDP at boot (device may not be ready yet)
            // User can trigger discovery manually via Settings > Scan
            Serial.println("[SONOS] Cached device unreachable at boot - use Settings to scan");
        }
    }

    updateBootProgress(100);  // Complete!
    delay(300);  // Show 100% briefly

    lv_screen_load(scr_main);  // Now load main screen
    lv_obj_del(boot_scr);     // Free boot screen objects (~3KB LVGL memory)
    Serial.println("Ready!");

    // Launch mainAppTask in internal SRAM (NOT PSRAM).
    // NVS writes (OTA settings, brightness, etc.) call spi_flash_disable_interrupts_caches_and_other_cpu()
    // which asserts esp_task_stack_is_sane_cache_disabled() if the calling task's stack is in
    // cache-mapped PSRAM. Art task (PSRAM, 20KB) already freed the critical DMA SRAM headroom,
    // so 16KB here no longer triggers SDIO DMA boot crashes. HWM shows < 5KB actually used.
    xTaskCreatePinnedToCore(mainAppTask, "Main", MAIN_APP_TASK_STACK, NULL,
                            MAIN_APP_TASK_PRIORITY, &mainAppTaskHandle, 1);

}

// WiFi auto-reconnection check (runs every WIFI_CHECK_INTERVAL_MS when disconnected)
static unsigned long lastWifiCheck = 0;

void checkWiFiReconnect() {
    if (millis() - lastWifiCheck < WIFI_CHECK_INTERVAL_MS) return;
    lastWifiCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Connection lost, attempting reconnect...");
        WiFi.reconnect();
        return;
    }

    // Issue #85: a flaky radio can associate AFTER the WiFi-setup dialog's connect loop
    // has timed out, so the dialog's "save on WL_CONNECTED" never fired and the creds were
    // lost on reboot. Now that the link is genuinely up, persist any pending creds. We only
    // ever reach here when truly connected, so a wrong password is never persisted.
    // Runs on mainAppTask (SRAM stack) → NVS-write-safe.
    if (wifi_creds_need_save && pending_wifi_ssid.length() > 0) {
        wifiPrefs.putString(NVS_KEY_SSID, pending_wifi_ssid);
        wifiPrefs.putString(NVS_KEY_PASSWORD, pending_wifi_pass);
        wifi_creds_need_save = false;
        Serial.println("[WIFI] Pending credentials persisted after async connect (issue #85)");
    }

    if (!sonos_started && !deferred_discovery_running) {
        // WiFi connected but Sonos not yet started (WiFi was down at boot).
        // tryLoadCachedDevice() does a blocking HTTP reachability GET (up to 5s). Running it
        // here — on mainAppTask, the same thread as lv_timer_handler() — froze LVGL and blocked
        // touch for the full timeout (issue #69). Hand it to a one-shot background task so the
        // UI thread stays responsive. The 10s lastWifiCheck throttle + the guard flag ensure
        // only one task is ever in flight; it retries on the next tick if discovery fails.
        deferred_discovery_running = true;
        xTaskCreatePinnedToCore(deferredDiscoveryTask, "DefDisc", 8192, NULL, 1, NULL, 1);
    }
}

// One-shot deferred Sonos init — runs off mainAppTask to avoid freezing touch (issue #69).
// Pinned to internal SRAM (default xTaskCreate): tryLoadCachedDevice() reads NVS and
// startTasks() may touch flash, both of which assert if the calling stack is in PSRAM.
// Self-deletes when done; deferred_discovery_running gates re-entry.
static void deferredDiscoveryTask(void* /*param*/) {
    // (Re)start NTP sync now that we have connectivity
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", CLOCK_ZONES[clock_tz_idx].posix, 1);
    tzset();
    Serial.println("[SONOS] WiFi now connected - attempting deferred discovery from cache");
    bool loadedFromCache = sonos.tryLoadCachedDevice();
    if (loadedFromCache) {
        sonos.selectDevice(0);
        sonos.startTasks();
        sonos_started = true;
        Serial.println("[SONOS] Deferred discovery succeeded from cache");
    } else {
        Serial.println("[SONOS] No cached device - use Devices screen to discover");
    }
    deferred_discovery_running = false;
    vTaskDelete(NULL);
}

// Periodic heap monitoring for debugging memory issues
static unsigned long lastHeapLog = 0;

void logHeapStatus() {
    if (millis() - lastHeapLog < HEAP_LOG_INTERVAL_MS) return;
    lastHeapLog = millis();

    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);

    Serial.printf("[HEAP] Free: %dKB | Min: %dKB | PSRAM: %dKB | DMA: %dKB\n",
                  free_heap / 1024, min_heap / 1024, free_psram / 1024, free_dma / 1024);

    // Log task stack high water marks — minimum free bytes ever observed.
    // On ESP-IDF 5.x (ESP32-P4 RISC-V), uxTaskGetStackHighWaterMark returns bytes directly.
    // Lower number = more stack used, closer to overflow. 0 = already overflowed.
    Serial.printf("[STACK] Main:%d ", uxTaskGetStackHighWaterMark(NULL));  // NULL = mainAppTask
    Serial.printf("Art:%d ", albumArtTaskHandle ? uxTaskGetStackHighWaterMark(albumArtTaskHandle) : 0);
    Serial.printf("Net:%d ", sonos.getNetworkTaskHandle() ? uxTaskGetStackHighWaterMark(sonos.getNetworkTaskHandle()) : 0);
    Serial.printf("Poll:%d ", sonos.getPollingTaskHandle() ? uxTaskGetStackHighWaterMark(sonos.getPollingTaskHandle()) : 0);
    Serial.printf("ClkBg:%d ", clockBgTaskHandle ? uxTaskGetStackHighWaterMark(clockBgTaskHandle) : 0);
    Serial.printf("Lyrics:%d bytes free\n", lyricsTaskHandle ? uxTaskGetStackHighWaterMark(lyricsTaskHandle) : 0);

    // Warn if heap or DMA is getting low
    if (free_heap < 50000) {
        Serial.println("[HEAP] WARNING: Low memory!");
    }
    if (free_dma < ART_MIN_DMA_PRE_BURST) {
        Serial.printf("[DMA] WARNING: DMA depleting (%dKB) — art/lyrics may abort. "
                      "Session depletion ~3.7KB/song. WiFi reconnect fires at 3 consecutive aborts.\n",
                      (int)(free_dma / 1024));
    }
}

// Main application task — runs all LVGL and UI logic with a 16KB internal SRAM stack.
// The Arduino loopTask (fixed 8KB) becomes idle below; it was regularly hitting
// only ~976 bytes free, causing Store access fault crashes via LVGL buffer corruption.
static void mainAppTask(void* param) {
    esp_task_wdt_add(NULL);  // Register this task with watchdog (not loopTask)

    for (;;) {
        esp_task_wdt_reset();

        lv_tick_inc(3);

        // Skip LVGL timer during OTA to prevent PSRAM access during flash writes
        bool skip_updates = false;
        if (xSemaphoreTake(ota_progress_mutex, pdMS_TO_TICKS(10))) {
            skip_updates = ota_in_progress;
            xSemaphoreGive(ota_progress_mutex);
        }

        if (!skip_updates) {
            lv_timer_handler();
            processUpdates();
            checkAutoDim();
            checkClockTrigger();
            checkWiFiReconnect();
            logHeapStatus();  // Periodic memory monitoring

            // ── DMA recovery handler ─────────────────────────────────────────────
            // Art task (PSRAM stack) cannot call esp_restart() or Preferences.begin()
            // directly — both call spi_flash_disable_interrupts_caches_and_other_cpu()
            // which asserts esp_task_stack_is_sane_cache_disabled() → cache_utils.c:127.
            // mainAppTask has an internal SRAM stack so both are safe here.
            if (art_dma_recovery_requested) {
                // Set flag BEFORE disconnect: pollingTask must not fire any SOAPs during the
                // entire recovery window (disconnect + reconnect + stabilise = ~47s).
                // Without this, pollingTask fires ~11 SOAPs during the 30s reconnect wait,
                // creating ~44KB of TIME_WAIT PCBs.  Those PCBs survive WiFi stop (lwIP runs
                // independently) and are still live when WiFi reconnects, consuming extra DMA.
                // With flag set here, no new PCBs form; existing ones expire during the ~47s
                // window → DMA after reconnect ≈ 117KB - 49KB (WiFi RX pool) = 68KB > 64KB ✓.
                art_download_in_progress = true;
                Serial.println("[MAIN] DMA recovery: stopping WiFi to release dynamic RX pool");
                WiFi.disconnect(true);   // esp_wifi_stop() releases ~51KB WiFi dynamic RX buffers
                esp_task_wdt_reset();
                // Keep UI responsive during WiFi stop drain (2s) — pump LVGL in 5ms ticks
                { unsigned long t_drain = millis();
                  while (millis() - t_drain < 2000) {
                      lv_tick_inc(5); lv_timer_handler();
                      esp_task_wdt_reset();
                      vTaskDelay(pdMS_TO_TICKS(5));
                  }
                }

                size_t dma_after = heap_caps_get_free_size(MALLOC_CAP_DMA);
                Serial.printf("[MAIN] DMA after WiFi stop: %u (need >%u)\n",
                              (unsigned)dma_after, (unsigned)ART_MIN_DMA_PRE_BURST);

                if (dma_after > ART_MIN_DMA_PRE_BURST) {
                    // DMA recovered — reconnect using saved credentials (NVS safe from SRAM stack)
                    Preferences prefs;
                    prefs.begin(NVS_NAMESPACE, true);
                    String ssid = prefs.getString(NVS_KEY_SSID, "");
                    String pass = prefs.getString(NVS_KEY_PASSWORD, "");
                    prefs.end();
                    Serial.printf("[MAIN] Reconnecting WiFi to '%s'\n", ssid.c_str());
                    WiFi.begin(ssid.c_str(), pass.c_str());
                    // Keep UI responsive during reconnect (up to 30s) — pump LVGL in 5ms ticks.
                    // Retry WiFi.begin() at 15s: mesh routers (ORBI95 etc.) sometimes miss the
                    // first scan but respond immediately to a second attempt.
                    unsigned long t = millis();
                    while (WiFi.status() != WL_CONNECTED && millis() - t < 30000) {
                        lv_tick_inc(5); lv_timer_handler();
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(5));
                        if (WiFi.status() != WL_CONNECTED && millis() - t >= 15000 && millis() - t < 15100) {
                            Serial.println("[MAIN] WiFi still not connected at 15s — retrying WiFi.begin()");
                            WiFi.begin(ssid.c_str(), pass.c_str());
                        }
                    }
                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.printf("[MAIN] WiFi reconnected — DMA=%uKB, stabilising...\n",
                                      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024));
                        // Wait for DMA to stabilise: with no new PCBs forming (flag set early),
                        // existing TIME_WAIT PCBs (TCP_MSL*2=12s) expire during this window.
                        // Expected DMA after PCB expiry: ~112KB (117KB WiFi-off - 49KB RX pool
                        // + 44KB recovered PCBs). Early-exit once above comfort threshold.
                        {
                            unsigned long t_stab = millis();
                            const size_t kComfortDMA = ART_MIN_DMA_PRE_BURST + 4000;   // 68KB — reachable ceiling with WiFi connected
                            while (millis() - t_stab < 15000) {
                                lv_tick_inc(5); lv_timer_handler();
                                esp_task_wdt_reset();
                                vTaskDelay(pdMS_TO_TICKS(5));
                                if (heap_caps_get_free_size(MALLOC_CAP_DMA) > kComfortDMA) break;
                            }
                            Serial.printf("[MAIN] Post-reconnect DMA: %uKB (stabilised in %lums)\n",
                                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
                                          millis() - t_stab);
                        }
                        // If DMA is still below the art-download threshold after the full
                        // stabilisation window, the heap is permanently fragmented — the WiFi
                        // RX pool plus residual PCBs consume more DMA than the system has
                        // available.  Releasing the art task here would cause it to abort
                        // immediately and request another recovery cycle, looping indefinitely.
                        // A full restart is the only path that frees everything cleanly.
                        {
                            size_t dma_post = heap_caps_get_free_size(MALLOC_CAP_DMA);
                            if (dma_post < ART_MIN_DMA_PRE_BURST) {
                                Serial.printf("[MAIN] DMA still insufficient after stabilisation "
                                              "(%uKB < %uKB) — restarting\n",
                                              (unsigned)(dma_post / 1024),
                                              (unsigned)(ART_MIN_DMA_PRE_BURST / 1024));
                                esp_task_wdt_reset();
                                vTaskDelay(pdMS_TO_TICKS(1000));
                                esp_restart();
                            }
                        }
                        // Stamp network timestamps before releasing the art task.
                        // Both last_art_download_end_ms and last_queue_fetch_time are stale
                        // (from before the ~40s reconnect window), so all sdioPreWait
                        // cooldowns would be near-zero and art would fire immediately —
                        // concurrent with the polling task's first updateQueue() (which
                        // queued up during reconnect) → pkt_rxbuff overflow → :928.
                        // Setting them to now enforces the full inter-download (1s) and
                        // queue-poll (3s) cooldowns on the first post-reconnect download.
                        last_art_download_end_ms = millis();
                        last_queue_fetch_time    = millis();
                        // Release polling suppression before signalling art task.
                        // Art task re-sets the flag itself after sdioPreWait on its next iteration.
                        art_download_in_progress = false;
                        art_dma_recovery_requested = false;  // signal art task: retry download
                    } else {
                        Serial.println("[MAIN] WiFi reconnect timed out — restarting");
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_restart();
                    }
                } else {
                    // WiFi.stop() didn't help — DMA permanently fragmented, restart required
                    Serial.printf("[MAIN] DMA still low after WiFi stop (%u) — restarting\n",
                                  (unsigned)dma_after);
                    esp_task_wdt_reset();
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }
            }
            // ────────────────────────────────────────────────────────────────────
        }

        vTaskDelay(pdMS_TO_TICKS(3));
    }
}

void loop() {
    // Idle — all UI/LVGL work is done in mainAppTask (32KB stack).
    // loopTask hard-coded 8KB stack cannot be changed via build flags.
    vTaskDelay(pdMS_TO_TICKS(100));
}
