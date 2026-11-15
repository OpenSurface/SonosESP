/**
 * main_square.cpp — entry point of the square 480x480 build (UI_SQUARE).
 *
 * Boot: panel and touch, then the UI straight away (the favourites saved in
 * flash show at once), Wi-Fi in the background, then the Sonos core, the
 * streaming accounts (from flash after the first boot) and a fresh copy of the
 * favourites.
 *
 * Loop (this task, core 1) owns LVGL. Workers (sq_work.cpp, sq_art.cpp) and the
 * Sonos tasks never call lv_*: the loop picks their results up.
 */
#include <Arduino.h>
#include <WiFi.h>
#include "lvgl.h"
#include "config.h"
#include "ui_common.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "ui_fonts.h"
#include "sq_ui.h"
#include "sq_work.h"
#include "mbedtls/platform.h"
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_core_dump.h>
#include <esp_app_desc.h>

void sqSettingsRefresh(void);
void sqWifiPoll(void);
void sqSearchOnShow(void);

// LVGL renders on this task: object trees nest deep and -O2 inlines more, and
// Arduino's default 8KB overflowed (stack canary) while drawing Favoris.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static uint32_t lvglTick(void) { return millis(); }

#if !UI_SQUARE
#include "screenshot.h"
// The P4 panel driver calls the old UI's screenshot hook on every flush; this
// UI has its own bench "shot" command (display_front_buffer), so it is a no-op.
void screenshotCaptureHook(const uint8_t*, const lv_area_t*) {}
#endif

// The last crash, kept in the coredump partition, printed at boot and then
// erased: a panic while no console is attached is otherwise lost, and a dump
// left in place cannot be told from a new one. Decode the "bt" addresses with
// addr2line against the firmware.elf of the build that crashed (its SHA below).
static void reportStoredCrash(void) {
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return;
    Serial.println("[CRASH] *** the previous run crashed ***");
    esp_core_dump_summary_t* sum = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
    if (sum && esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(sum) == ESP_OK) {
        char running[APP_ELF_SHA256_SZ] = {0};
        esp_app_get_elf_sha256(running, sizeof(running));
        const bool same = !strncmp((const char*)sum->app_elf_sha256, running, sizeof(running));
#if CONFIG_IDF_TARGET_ARCH_XTENSA   // S3
        Serial.printf("[CRASH] task %.16s  pc 0x%08X  cause %u  vaddr 0x%08X  build %.9s (%s)\n",
                      sum->exc_task, (unsigned)sum->exc_pc, (unsigned)sum->ex_info.exc_cause,
                      (unsigned)sum->ex_info.exc_vaddr, (const char*)sum->app_elf_sha256,
                      same ? "this firmware" : "another firmware");
        Serial.print("[CRASH] bt");
        for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 16; ++i)
            Serial.printf(" 0x%08X", (unsigned)sum->exc_bt_info.bt[i]);
        Serial.println(sum->exc_bt_info.corrupted ? " (corrupted)" : "");
#else                               // P4 (RISC-V): no backtrace in the summary
        Serial.printf("[CRASH] task %.16s  pc 0x%08X  mcause 0x%08X  mtval 0x%08X  ra 0x%08X  sp 0x%08X"
                      "  build %.9s (%s)\n",
                      sum->exc_task, (unsigned)sum->exc_pc, (unsigned)sum->ex_info.mcause,
                      (unsigned)sum->ex_info.mtval, (unsigned)sum->ex_info.ra, (unsigned)sum->ex_info.sp,
                      (const char*)sum->app_elf_sha256, same ? "this firmware" : "another firmware");
#endif
    } else {
        Serial.println("[CRASH] summary unavailable");
    }
    free(sum);
    esp_core_dump_image_erase();
}

// mbedTLS is built with CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC: every TLS session
// takes ~40KB of internal RAM, and the S3 has ~120KB left once Wi-Fi, LVGL's
// render band and the bounce buffers are in. Two sessions at once (Spotify
// SMAPI keeps one open, a cover download opens another) and nothing is left:
// downloads failed with "connection refused" at 12KB free. Moving mbedTLS to
// PSRAM costs a little speed and frees all of that.
#if UI_SQUARE
static void* tlsCalloc(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#endif

static bool s_wifi_up     = false;
static bool s_core_posted = false;

// ── Bench serial commands ───────────────────────────────────────────────────
//   bl <0..100>   backlight          shot          RLE screenshot (see below)
//   tap <x> <y>   (UI_DEBUG_TAP)     show <n>      jump to a screen
static void screenshot(void) {
    // Run-length encoded RGB565 of the frame being shown, as hex lines:
    //   SHOT 480 480 / <runs> / SHOT END. A run is 3 bytes: count (1..255),
    //   pixel lo, pixel hi. Flat UIs compress ~10x, so 115200 baud is enough.
    extern const void* display_front_buffer(void);
    const uint16_t* px = (const uint16_t*)display_front_buffer();
    const int n = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    Serial.printf("\nSHOT %d %d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    // Each line starts with the index of its first pixel ("@<hex>:"), so a line
    // lost on the UART leaves a hole instead of shifting the rest of the image.
    char line[220];
    int col = 0;
    for (int i = 0; i < n;) {
        if (col == 0) col = snprintf(line, sizeof(line), "@%X:", i);
        uint16_t p = px[i];
        int run = 1;
        while (i + run < n && run < 255 && px[i + run] == p) run++;
        col += snprintf(line + col, sizeof(line) - col, "%02X%02X%02X", run, p & 0xFF, p >> 8);
        i += run;
        if (col >= 180) { Serial.println(line); col = 0; }
    }
    if (col) Serial.println(line);
    Serial.println("SHOT END");
}

static void benchSerial(void) {
    static char line[48];
    static size_t n = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
        line[n] = 0; n = 0;
        if (!strncmp(line, "bl ", 3)) {
            display_set_brightness((uint8_t)atoi(line + 3));
        } else if (!strcmp(line, "shot")) {
            screenshot();
        } else if (!strncmp(line, "scroll ", 7)) {
            // Scroll the current screen's first scrollable container, animated
            // like a flick, to measure frame times without a finger.
            lv_obj_t* scr = lv_screen_active();
            for (uint32_t i = 0; i < lv_obj_get_child_count(scr); ++i) {
                lv_obj_t* c = lv_obj_get_child(scr, i);
                if (lv_obj_get_scroll_dir(c) == LV_DIR_VER) {
                    lv_obj_scroll_by(c, 0, -atoi(line + 7), LV_ANIM_ON);
                    break;
                }
            }
        } else if (!strncmp(line, "search ", 7)) {
            sqResultsStart(line + 7);
        } else if (!strncmp(line, "show ", 5)) {
            int s = atoi(line + 5);
            if (s >= 0 && s < SQ_SCR_COUNT) sqShow((SqScreen)s);
#if UI_DEBUG_TAP
        } else if (!strncmp(line, "tap ", 4)) {
            int x = 0, y = 0;
            if (sscanf(line + 4, "%d %d", &x, &y) == 2) touch_inject_tap(x, y);
#endif
        }
    }
}

// ── Screen hooks ────────────────────────────────────────────────────────────
void sqOnShow(SqScreen scr) {
    switch (scr) {
        case SQ_SCR_PLAYER:   sqPlayerUpdate(); break;
        case SQ_SCR_ROOMS:    sqRoomsRefresh(); break;
        case SQ_SCR_FAVS:     sqFavsRefresh(); break;
        case SQ_SCR_SPOTIFY:  sqSpotifyRefresh(); break;
        case SQ_SCR_SETTINGS: sqSettingsRefresh(); break;
        case SQ_SCR_SEARCH:   sqSearchOnShow(); break;
        case SQ_SCR_LYRICS:   sqLyricsOnShow(); break;
        default: break;
    }
}

void sqOnWifiConnected(void) {
    if (s_wifi_up) return;
    s_wifi_up = true;
    Serial.printf("[WIFI] Connected - IP %s, AP %s ch=%d rssi=%d\n", WiFi.localIP().toString().c_str(),
                  WiFi.BSSIDstr().c_str(), (int)WiFi.channel(), (int)WiFi.RSSI());
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    sqWorkPost(SQ_JOB_START_SONOS);
    if (sqCurrent() == SQ_SCR_WIFI) sqShow(SQ_SCR_PLAYER);
}

void sqUiPoll(void) {
    // Sonos core -> player
    UIUpdate_t upd;
    bool changed = false;
    if (sq_sonos_ready) {
        while (xQueueReceive(sonos.getUIUpdateQueue(), &upd, 0)) changed = true;
    }
    static uint32_t last_update = 0;
    if (changed && millis() - last_update > 150) {
        last_update = millis();
        sqPlayerUpdate();
        sqRoomPillsRefresh();
        if (millis() - last_update > 200)
            Serial.printf("[SLOW] player+pills %lums\n", (unsigned long)(millis() - last_update));
    }

    // Once the speakers answer: accounts and favourites, once.
    if (sq_sonos_ready && !s_core_posted) {
        s_core_posted = true;
        sqWorkPost(SQ_JOB_SVC_LOAD);
        sqWorkPost(SQ_JOB_FAVS_LOAD);
        sqPlayerUpdate();
        sqRoomPillsRefresh();
    }

    // Streaming accounts: a failed discovery (speaker busy at boot) is retried
    // every 30s until Spotify is there.
    static uint32_t svc_retry_at = 0;
    if (s_core_posted && musicServices.state() == SVC_FAILED && !sqWorkBusy() &&
        (int32_t)(millis() - svc_retry_at) >= 0) {
        if (svc_retry_at) sqWorkPost(SQ_JOB_SVC_LOAD);
        svc_retry_at = millis() + 30000;
    }

    char msg[96];
    if (sqWorkTakeToast(msg, sizeof(msg))) sqToast(msg);

    static uint32_t last_tick = 0;
    if (millis() - last_tick > 250) {
        last_tick = millis();
        switch (sqCurrent()) {
            case SQ_SCR_PLAYER:   sqPlayerTick(); break;
            case SQ_SCR_ROOMS:    sqRoomsRefresh(); break;
            case SQ_SCR_FAVS:     sqFavsRefresh(); break;
            case SQ_SCR_SETTINGS: sqSettingsRefresh(); break;
            default: break;
        }
        static uint32_t rooms_seen = 0;
        if (rooms_seen != sq_rooms_version) { rooms_seen = sq_rooms_version; sqRoomPillsRefresh(); }
    }
    // The lyrics follow the music line by line: a faster beat than the rest.
    static uint32_t last_lyr = 0;
    if (sqCurrent() == SQ_SCR_LYRICS && millis() - last_lyr > 100) {
        last_lyr = millis();
        sqLyricsTick();
    }
    const uint32_t tr = millis();
    switch (sqCurrent()) {
        case SQ_SCR_RESULTS: sqResultsRefresh(); break;
        case SQ_SCR_SPOTIFY: sqSpotifyRefresh(); break;
        case SQ_SCR_BROWSE:  sqBrowseRefresh(); break;
        case SQ_SCR_WIFI:    sqWifiPoll(); break;
        default: break;
    }
    if (millis() - tr > 200)
        Serial.printf("[SLOW] refresh of screen %d %lums\n", (int)sqCurrent(), (unsigned long)(millis() - tr));
    if (!s_wifi_up && WiFi.status() == WL_CONNECTED) sqOnWifiConnected();
}

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(200);
#if UI_SQUARE
    Serial.println("\n=== SONOS ESP — KIDS UI, CARRÉ 480x480 (ESP32-S3) ===");
#else
    Serial.println("\n=== SONOS ESP — KIDS UI, PORTRAIT 480x800 (ESP32-P4) ===");
#endif
    Serial.printf("Heap %u, PSRAM %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    {
        char sha[APP_ELF_SHA256_SZ] = {0};
        esp_app_get_elf_sha256(sha, sizeof(sha));
        Serial.printf("Build %.9s, reset reason %d\n", sha, (int)esp_reset_reason());
    }
    reportStoredCrash();

#if UI_SQUARE   // S3: internal RAM is scarce; the P4 keeps mbedTLS where its SDIO code expects it
    mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
#endif

    // Same watchdog policy as the P4 build: idle tasks are not watched. A TLS
    // handshake plus parsing Spotify's home page keeps core 0 busy for several
    // seconds, and the Arduino default (IDLE0 watched, 5s) rebooted the panel
    // in the middle of it.
    esp_task_wdt_config_t wdt = {};
    wdt.timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000;
    wdt.idle_core_mask = 0;
    wdt.trigger_panic = true;
    esp_task_wdt_reconfigure(&wdt);
    network_mutex      = xSemaphoreCreateMutex();
    ota_progress_mutex = xSemaphoreCreateMutex();
    art_mutex          = xSemaphoreCreateMutex();

    wifiPrefs.begin(NVS_NAMESPACE, false);
    String ssid = wifiPrefs.getString(NVS_KEY_SSID, "");
    String pass = wifiPrefs.getString(NVS_KEY_PASSWORD, "");
    brightness_level = wifiPrefs.getInt(NVS_KEY_BRIGHTNESS, 80);

    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        Serial.printf("[WIFI] Disconnected: reason %u\n", info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);     // mesh: join the strongest node
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#if !UI_SQUARE
    // P4: the radio is an ESP32-C6 behind esp-hosted (SDIO); give it time to
    // come up before the first begin(), as the P4 firmware always has.
    vTaskDelay(pdMS_TO_TICKS(WIFI_INIT_DELAY_MS));
#endif
    if (ssid.length()) {
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.printf("[WIFI] Connecting to '%s'\n", ssid.c_str());
    }

    uiFontsInit();
    lv_init();
    lv_tick_set_cb(lvglTick);
    if (!display_init()) { Serial.println("Display FAIL"); while (1) delay(1000); }
    if (!touch_init())   { Serial.println("Touch FAIL"); }

    sonos.begin();
    sqWorkStart();
    sqArtStart();
    sqLyricsStart();

    sqNavInit();
    sqPlayerCreate();
    sqSearchCreate();
    sqResultsCreate();
    sqFavsCreate();
    sqSpotifyCreate();
    sqBrowseCreate();
    sqRoomsCreate();
    sqSettingsCreate();
    sqWifiCreate();
    sqLyricsCreate();

    if (ssid.length()) sqShow(SQ_SCR_PLAYER);
    else               sqWifiOpen();   // first boot: nothing works without the network
    lv_timer_handler();
    display_set_brightness(brightness_level);
    Serial.printf("Ready - internal free %u, largest %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void loop() {
    const uint32_t t0 = millis();
    benchSerial();
    if (touch_take_wake_request()) last_touch_time = millis();
    sqUiPoll();
    const uint32_t t1 = millis();
    uint32_t wait = lv_timer_handler();
    const uint32_t t2 = millis();
    // A frozen screen is a long loop iteration: say which half held it.
    if (t2 - t0 > 250)
        Serial.printf("[STALL] loop %lums (poll %lums, lvgl %lums) on screen %d\n",
                      (unsigned long)(t2 - t0), (unsigned long)(t1 - t0), (unsigned long)(t2 - t1),
                      (int)sqCurrent());
    if (wait > 10) wait = 10;
    delay(wait ? wait : 1);
}
