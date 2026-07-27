/**
 * config.h - Centralized configuration constants
 * All magic numbers and configurable values in one place
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// SERIAL & DEBUG
// =============================================================================
#define SERIAL_BAUD_RATE        115200

// Debug levels: 0=OFF, 1=ERRORS, 2=WARNINGS, 3=INFO, 4=VERBOSE
#define DEBUG_LEVEL             3

// Debug macros - compile out verbose logs when not needed
#if DEBUG_LEVEL >= 4
    #define DEBUG_VERBOSE(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_VERBOSE(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 3
    #define DEBUG_INFO(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_INFO(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 2
    #define DEBUG_WARN(fmt, ...) Serial.printf("[WARN] " fmt, ##__VA_ARGS__)
#else
    #define DEBUG_WARN(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 1
    #define DEBUG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt, ##__VA_ARGS__)
#else
    #define DEBUG_ERROR(fmt, ...) ((void)0)
#endif

// =============================================================================
// WIFI CONFIGURATION
// =============================================================================
#define WIFI_INIT_DELAY_MS      2000    // Delay for ESP32-C6 SDIO initialization
#define WIFI_CONNECT_TIMEOUT_MS 500     // Per-attempt timeout
#define WIFI_CONNECT_RETRIES    40      // Max connection attempts (40 x 500ms = 20s)
#define WIFI_MAX_NETWORKS       20      // Max networks to scan/store
#define WIFI_CHECK_INTERVAL_MS  10000   // Main loop: WiFi health check interval

// =============================================================================
// DISPLAY SETTINGS
// =============================================================================
#define DEFAULT_BRIGHTNESS      100     // Default brightness percentage
#define DEFAULT_BRIGHTNESS_DIM  20      // Default dimmed brightness
#define DEFAULT_AUTODIM_SEC     30      // Default auto-dim timeout (seconds)
#define MIN_BRIGHTNESS          5       // Minimum brightness allowed
#define MAX_BRIGHTNESS          100     // Maximum brightness

// =============================================================================
// SCREEN VARIANT  (build-time: -DSCREEN_SIZE=4 | 7 ; defaults to 4")
// -----------------------------------------------------------------------------
// To add a screen variant: add an env in platformio.ini with -DSCREEN_SIZE=N,
// add a matching branch below, and port its panel driver under lib/. Keep the
// UI resolution-relative via include/ui_scale.h so no per-size layout is needed.
// See docs/MULTI_SCREEN_SUPPORT.md.
// =============================================================================
#ifndef SCREEN_SIZE
#define SCREEN_SIZE 4                   // default to 4" if no build flag is set
#endif

#if SCREEN_SIZE == 4
    // GUITION JC4880P433C — ST7701 MIPI DSI
    #define DISPLAY_WIDTH       800     // LVGL width (landscape)
    #define DISPLAY_HEIGHT      480     // LVGL height (landscape)
    #define PANEL_WIDTH         480     // Physical panel width (portrait)
    #define PANEL_HEIGHT        800     // Physical panel height (portrait)
    #define DISPLAY_MODEL       "ST7701 4\" (800x480)"
    #define LCD_RST             5       // Reset GPIO for ST7701
    #define TOUCH_GT911_SDA     7
    #define TOUCH_GT911_SCL     8
    #define TOUCH_GT911_INT     -1      // Not used
    #define TOUCH_GT911_RST     -1      // Not used
    #define TOUCH_PANEL_WIDTH   480     // Touch panel native width (portrait)
    #define TOUCH_PANEL_HEIGHT  800     // Touch panel native height (portrait)
#elif SCREEN_SIZE == 7
    // GUITION JC1060P470C — JD9165 MIPI DSI, native 1024x600 LANDSCAPE.
    // Unlike the 4" ST7701 (portrait panel rotated 90°), the JD9165 is wired
    // landscape, so the flush path does NOT rotate (PANEL_* == DISPLAY_*).
    // Code-complete from the CoopsInChina fork port; not yet hardware-validated.
    #define DISPLAY_WIDTH       1024    // LVGL width (landscape)
    #define DISPLAY_HEIGHT      600     // LVGL height (landscape)
    #define PANEL_WIDTH         1024    // Physical panel width (no rotation)
    #define PANEL_HEIGHT        600     // Physical panel height (no rotation)
    #define DISPLAY_MODEL       "JD9165 7\" (1024x600)"
    #define LCD_RST             23      // Reset GPIO for JD9165 (CoopsInChina fork)
    #define TOUCH_GT911_SDA     7
    #define TOUCH_GT911_SCL     8
    #define TOUCH_GT911_INT     11
    #define TOUCH_GT911_RST     22
    #define TOUCH_PANEL_WIDTH   1024    // Touch panel native width (landscape)
    #define TOUCH_PANEL_HEIGHT  600     // Touch panel native height (landscape)
#else
    #error "Unsupported SCREEN_SIZE (use 4 or 7)"
#endif

// =============================================================================
// ALBUM ART
// =============================================================================
#define ART_MAX_DOWNLOAD_SIZE   (280 * 1024)  // Max JPEG download buffer (280KB)
#define ART_TASK_STACK_SIZE     20000   // Album art task stack — PNG decode stacks TLS + software
                                        // decoder on same task; 12KB hit stack=0 on HTTPS PNG (BBC R4)
#define ART_TASK_PRIORITY       0       // Album art task priority
#define ART_DOWNLOAD_TIMEOUT_MS 8000    // Download timeout
#define ART_CHECK_INTERVAL_MS   100     // How often to check for new art requests
#define ART_DECODE_MAX_FAILURES 3       // Give up on URL after N decode failures
#define ART_SW_JPEG_FALLBACK    1       // Enable JPEGDEC SW fallback (progressive, non-div-8)

// Widest PNG accepted by decodeToRGB565(). SAFETY-CRITICAL: pngDraw()'s static row
// buffer is sized from this, and PNGdec's getLineAsRGB565() takes no length argument —
// it writes iWidth pixels unconditionally. Any PNG wider than this is rejected before
// decode. Keep the buffer (2 bytes/px) and this limit in lock-step.
#define PNG_MAX_DECODE_WIDTH    1024    // 2KB row buffer; Sonos getaa is capped at 600px
#define PNG_MAX_DECODE_HEIGHT   2048    // height is streamed row-by-row — no buffer impact

// =============================================================================
// SONOS CONTROLLER
// =============================================================================
#define SONOS_MAX_DEVICES       32      // Maximum discoverable devices (keep in sync with MAX_SONOS_DEVICES)
#define SONOS_QUEUE_SIZE_MAX    500     // Maximum queue items to fetch
#define SONOS_QUEUE_BATCH_SIZE  10      // Items per queue fetch request (was 50).
                                        // 50-item response (~20KB, 14 TCP segs) forced WiFi driver to allocate
                                        // all 32 dynamic RX buffers (~51KB) in one event → permanent 71KB DMA
                                        // floor drop after first periodic queue poll → Song 2 crash-zone.
                                        // 10-item response (~4KB, 3 TCP segs) allocates only 3 WiFi buffers.
                                        // Art download (16KB burst) adds ~11 more buffers. Pool stabilises at
                                        // ~14 total (22KB) instead of 32 (51KB). DMA floor = ~85KB vs ~35KB.
                                        // 10 items is sufficient for the "Next Up" queue display.
#define SONOS_CMD_QUEUE_SIZE    10      // Command queue depth
#define SONOS_UI_QUEUE_SIZE     20      // UI update queue depth

// Task configuration (stack sizes in BYTES — ESP-IDF xTaskCreate takes bytes, not words)
// High water marks (before fix) showed Poll=684 bytes free / 3000 total = DANGER.
// sendSOAP chain can push 600-800 more bytes → overflow on deeper paths (queue, media info).
// Net=1504 bytes free / 3500 total — also tight. Both doubled for safety.
#define SONOS_NET_TASK_STACK    6000    // Network task stack size (was 3500; actual free was ~1.5KB)
#define SONOS_POLL_TASK_STACK   6000    // Polling task stack size (was 3000; actual free was ~684 bytes!)
#define SONOS_NET_TASK_PRIORITY 2       // Network task priority
#define SONOS_POLL_TASK_PRIORITY 3      // Polling task priority
#define LYRICS_TASK_STACK       8192    // Lyrics task stack size (4096 overflowed on HTTPS fetch — WiFiClientSecure + URL[512] needs ~6KB)
#define LYRICS_TASK_PRIORITY    1       // Lyrics task priority
// Arduino loopTask stack is hard-coded to 8KB in pre-compiled framework (sdkconfig.h) — cannot
// be overridden with -D flags. mainAppTask runs the actual UI loop with a proper stack.
// loopTask becomes idle (vTaskDelay only). Watchdog transfers to mainAppTask.
// MUST be internal SRAM: NVS writes call spi_flash_disable_interrupts_caches_and_other_cpu()
// which asserts (esp_task_stack_is_sane_cache_disabled) if the calling task's stack is in PSRAM.
// 8KB is safe — HWM shows < 5KB used. Art task in PSRAM already frees 20KB DMA SRAM headroom.
// Smaller than 16KB saves 8KB of DMA, giving OTA TLS handshake more room (~106KB peak).
#define MAIN_APP_TASK_STACK     8192    // mainAppTask stack — internal SRAM (flash/NVS safe)
#define MAIN_APP_TASK_PRIORITY  1       // Same priority as loopTask; Sonos (2/3) preempts as before

// Timeouts
#define SONOS_SOAP_TIMEOUT_MS   2000    // SOAP request timeout
#define SONOS_DEBOUNCE_MS       400     // Command debounce time

// Polling tick modulos (base interval = 300ms, so N ticks = N * 300ms)
#define POLL_VOLUME_MODULO      5       // Volume every 1.5s (5 * 300ms)
#define POLL_TRANSPORT_MODULO   10      // Transport settings every 3s
#define POLL_QUEUE_MODULO       200     // Queue every 60s (was 100/30s — halved to reduce DMA pressure)
#define POLL_MEDIA_INFO_MODULO  50      // Radio station info every 15s
#define POLL_BASE_INTERVAL_MS   300     // Base polling interval

// =============================================================================
// OTA UPDATES
// =============================================================================
#define OTA_BUFFER_SIZE         2048    // Download buffer (static in main loop — not FreeRTOS stack)
#define OTA_READ_SIZE           2048    // Max bytes per read. 2KB: same adaptive delay, 2× throughput.
                                        // Critical zone (DMA<4KB): 2KB+80ms = 25KB/s vs old 12.5KB/s
                                        // → ~40s download instead of ~68s for 2MB firmware
#define OTA_MAX_FIRMWARE_SIZE   (10 * 1024 * 1024)  // 10MB max firmware
#define OTA_DOWNLOAD_TIMEOUT_MS 300000  // 5 minutes max for entire download
#define OTA_STALL_TIMEOUT_MS    30000   // 30 seconds max with no data received
#define OTA_PROGRESS_LOG_INTERVAL 10    // Log every N percent
#define OTA_DMA_CHECK_INTERVAL  4       // Check DMA pressure every N chunks (~4KB)
#define OTA_DMA_CRITICAL        4096    // DMA critical threshold (80ms delay)
#define OTA_DMA_LOW             8192    // DMA low threshold (30ms delay)
#define OTA_BASE_DELAY_MS       15      // Base per-chunk delay (~65KB/s, ~25s for 1.5MB)
#define OTA_TLS_MAX_RETRIES     3             // Retry full connect+download on connection failure
#define OTA_TLS_RETRY_DELAY_MS  5000          // Wait between retry attempts (ms per attempt)
#define OTA_MIN_DMA_AFTER_TLS   (8 * 1024)   // Min total free DMA after TLS GET completes.
                                              // < 8KB → SDIO RX pool starved → assert crash.
                                              // Normal full handshake leaves ~17-21KB (safe).
                                              // heap_caps_get_largest_free_block(MALLOC_CAP_DMA)
                                              // always returns 0 on ESP32-P4, so total only.
#define OTA_TARGET_FREE_DMA     (88 * 1024)  // Min free DMA before attempting the OTA TLS handshake.
                                              // Lowered 112→88KB: mainAppTask moved to internal SRAM (not PSRAM)
                                              // reduces available DMA from ~113KB to ~105KB on new platform
                                              // (55.03.37 / ESP-IDF v5.5.2). 88KB lets OTA proceed to TLS.
                                              // The post-TLS check (OTA_MIN_DMA_AFTER_TLS=8KB) is the real
                                              // SDIO safety gate. Watch "[OTA] Post-TLS DMA" log to tune.
#define OTA_HTTPS_COOLDOWN_MS   2000    // Wait for previous HTTPS cleanup
#define OTA_CHECK_DEBOUNCE_MS   5000    // Min delay between update checks
#define OTA_CHECK_TIMEOUT_MS    15000   // HTTP timeout for version check
#define OTA_CHECK_CLEANUP_MS    500     // Delay after version check TLS cleanup
#define OTA_DMA_POLL_MS         15000   // Max wait for TIME_WAIT sockets to expire
                                        // lwIP TIME_WAIT = 2×MSL ≈ 12s; 15s gives 3s margin
#define OTA_DMA_PLATEAU_COUNT   3       // Consecutive seconds with no DMA growth → reboot early

// =============================================================================
// MBEDTLS / SSL
// =============================================================================
#define MBEDTLS_SSL_IN_LEN      4096    // SSL input buffer size
#define MBEDTLS_SSL_OUT_LEN     4096    // SSL output buffer size

// =============================================================================
// NVS PREFERENCES
// =============================================================================
#define NVS_NAMESPACE           "sonos_wifi"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASSWORD        "pass"
#define NVS_KEY_BRIGHTNESS      "brightness"
// NOTE: NVS keys are limited to 15 characters. A longer key makes nvs_set_* fail with
// ESP_ERR_NVS_KEY_TOO_LONG, and Arduino's Preferences only reports that via log_e() —
// which is compiled out by CORE_DEBUG_LEVEL=0, so the write fails SILENTLY.
// "brightness_dimmed" was 17 chars, so dimmed brightness never persisted across reboots.
// (No migration needed: the old key could never be written, so there is nothing to read.)
#define NVS_KEY_BRIGHTNESS_DIM  "bright_dim"
#define NVS_KEY_AUTODIM         "autodim_sec"
#define NVS_KEY_THEME           "theme"         // player theme index (see ui_theme.h)
#define DEFAULT_THEME           0               // 0 = Classic (unchanged original look)
#define NVS_KEY_OTA_CHANNEL     "ota_channel"
#define NVS_KEY_CACHED_DEVICE   "cached_dev"
#define NVS_KEY_LYRICS          "lyrics"
#define NVS_KEY_OTA_PENDING     "ota_pending"    // Auto-reboot OTA flag
#define NVS_KEY_OTA_URL         "ota_url"        // Saved firmware URL for auto-reboot OTA

// =============================================================================
// UI COLORS (hex values)
// =============================================================================
#define COLOR_BACKGROUND        0x000000
#define COLOR_TEXT_PRIMARY      0xFFFFFF
#define COLOR_TEXT_SECONDARY    0x888888
#define COLOR_ACCENT            0xD4A84B  // Sonos gold
#define COLOR_SUCCESS           0x00FF00
#define COLOR_ERROR             0xFF0000
#define COLOR_WARNING           0xFFA500

// =============================================================================
// CLOCK / SCREENSAVER
// =============================================================================
#define CLOCK_MODE_DISABLED    0  // Never show clock screen
#define CLOCK_MODE_INACTIVITY  1  // Show after X mins no touch (any play state)
#define CLOCK_MODE_PAUSED      2  // Show only when paused/stopped + X mins inactivity
#define CLOCK_MODE_NOTHING     3  // Show only when nothing playing + X mins inactivity

#define CLOCK_DEFAULT_MODE       0    // Disabled by default
#define CLOCK_DEFAULT_TIMEOUT    5    // 5 minutes inactivity before clock
#define CLOCK_DEFAULT_TZ_IDX     0    // Index 0 = UTC
#define CLOCK_DEFAULT_PICSUM     1    // Enable random photo background
#define CLOCK_DEFAULT_REFRESH    10   // Refresh background every 10 minutes
#define CLOCK_DEFAULT_KW_IDX     0    // Index 0 = Random (no keyword)
#define CLOCK_DEFAULT_12H        0    // 0 = 24h, 1 = 12h
#define CLOCK_DEFAULT_WEATHER_EN   1  // Weather widget enabled by default
#define CLOCK_DEFAULT_WEATHER_CITY 0  // 0 = Auto-detect from IP
#define CLOCK_WX_REFRESH_MIN      15  // Re-fetch weather every 15 min (independent of photo rate)
#define CLOCK_DEFAULT_WEATHER_FAHR 0  // 0 = Celsius, 1 = Fahrenheit

#define CLOCK_BG_MAX_DL_SIZE  (512 * 1024)  // Max background JPEG download buffer (512KB; Flickr baseline ~100-250KB)
// Clock background photo size. MUST track the panel: these drive the loremflickr
// request URL, the PSRAM decode buffer, its stride, and the size of the image +
// dark overlay widgets. Hardcoded 800x480 meant the 7" (1024x600) asked for a
// 4"-sized photo AND left a 224x120 L-shaped strip of the screen uncovered by the
// image and its readability veil. Identity on the 4" — no change there.
#define CLOCK_BG_WIDTH        DISPLAY_WIDTH   // Clock background pixel width
#define CLOCK_BG_HEIGHT       DISPLAY_HEIGHT  // Clock background pixel height
#define CLOCK_BG_TASK_STACK   8192          // clockBgTask stack size
#define CLOCK_ENTER_TIMEOUT_MS 3000         // Max wait for art/lyrics tasks to exit
#define CLOCK_EXIT_COOLDOWN_MS 30000        // Prevent re-trigger for 30s after exit

#define NVS_KEY_CLOCK_MODE      "clk_mode"
#define NVS_KEY_CLOCK_TIMEOUT   "clk_timeout"
#define NVS_KEY_CLOCK_TZ        "clk_tz"
#define NVS_KEY_CLOCK_PICSUM    "clk_picsum"
#define NVS_KEY_CLOCK_REFRESH   "clk_refresh"
#define NVS_KEY_CLOCK_KW        "clk_kw"
#define NVS_KEY_CLOCK_12H       "clk_12h"
#define NVS_KEY_CLOCK_WEATHER_EN   "clk_wx_en"
#define NVS_KEY_CLOCK_WEATHER_CITY "clk_wx_city"
#define NVS_KEY_CLOCK_WEATHER_FAHR "clk_wx_fahr"
// Custom location override (issue #74) — used when city_idx == CLOCK_LOC_CUSTOM_IDX
#define NVS_KEY_CLOCK_WX_CUSTOM_LAT  "clk_wx_clat"
#define NVS_KEY_CLOCK_WX_CUSTOM_LON  "clk_wx_clon"
#define NVS_KEY_CLOCK_WX_CUSTOM_NAME "clk_wx_cnam"  // optional display name for the clock saver screen

// =============================================================================
// QUEUE / PLAYLIST
// =============================================================================
#define QUEUE_ADD_AT_END        4294967295  // Add to end of queue constant

// =============================================================================
// SDIO CRASH DEFENCE (ESP32-P4 + ESP32-C6 SDIO architecture)
// =============================================================================
// The C6 WiFi chip connects to the P4 host via SDIO. C6 has a fixed-size
// pkt_rxbuff. Concurrent TCP traffic (HTTP response residue + SOAP responses
// + UPnP NOTIFY events) overflows it → C6 asserts sdio_push_data_to_queue:928
// → SDIO host spins in tight register-poll loop → Interrupt WDT on CPU0.
// All values below are calibrated for this hardware. Do NOT reduce without testing.
#define SDIO_GENERAL_COOLDOWN_MS      200   // Min gap between any two network operations
#define SDIO_HTTPS_COOLDOWN_MS       3000   // TLS teardown residue — 2000ms insufficient (DMA AES alloc failure)
#define SDIO_STORM_COOLDOWN_MS       3000   // HTTP-500 storm settle (HLS source transition)
#define SDIO_STORM_SAFETY_CAP_MS     5000   // Max wait inside 500-storm loop (safety cap)
#define SDIO_TRACK_CHANGE_SETTLE_MS     0   // Disabled: Sonos lib is pure SOAP-polling (no UPnP subscriptions,
                                            // no WiFiServer, no NOTIFY events). Long idle → C6 power-save → crash.
#define SDIO_POST_STORM_SETTLE_MS       0   // Disabled: storm gate (3000ms) + any settle = too much idle → C6 SDIO
                                            // DMA clock-gates → pkt_rxbuff fills on wake burst. Even 1000ms settle
                                            // (total 4000ms idle) crashes. Keepalive: see SDIO_STORM_KEEPALIVE_MS.
#define SDIO_TCP_CLOSE_MS             200   // TCP FIN-ACK drain after http.end() (non-TLS)
#define SDIO_HTTPS_TCP_CLOSE_MS       500   // TCP FIN-ACK drain after http.end() (TLS)
#define SDIO_INTER_DOWNLOAD_MS       1000   // Min gap between consecutive art/BG downloads
#define ART_TCP_RCVBUF               4096   // TCP SO_RCVBUF for art HTTP socket. Controls app-layer recv
                                            // buffer size (conn->recv_bufsize). Does NOT directly control
                                            // TCP window advertisement (pcb->rcv_wnd). The TCP window
                                            // (65534) is baked into the pre-compiled liblwip.a and cannot
                                            // be changed at runtime. SO_RCVBUF gradually constrains the
                                            // advertised window as the app buffer fills, preventing runaway
                                            // server bursts during multi-segment downloads.
#define ART_TCP_RCVBUF_DL_SAFETY    16000   // Abort if DMA < this AT dl-start (AFTER http.GET() burst absorbed).
                                            // CONFIRMED CRASH: dl-start=22364 → WiFi alloc ~15KB during read → :928.
                                            // Belt-and-suspenders: ART_DMA_MID_READ_MIN catches further drops during
                                            // the read loop itself. 16KB catches catastrophically-low dl-start values
                                            // (burst >> expected) without blocking healthy Song 2+ downloads.
                                            // Song 2+ DMA floor 38-42KB: burst 16KB → dl-start 22-26KB >> 16KB ✓
                                            // Crash scenario: dl-start 22KB > 16KB passes → mid-read check at 8KB
                                            // catches the WiFi-alloc drop → aborts before :928.
#define ART_MIN_FREE_DMA             8000   // Referenced in boot memory map log only (not a download gate).
#define ART_MIN_DMA_PRE_BURST       56000   // Min DMA before http.GET() (BEFORE burst arrives).
                                            // Observed burst: 13-22KB (server TCP cwnd limited; SO_RCVBUF does NOT
                                            // limit initial burst on ESP32-P4/lwIP). Post-burst floor = 56-22 = 34KB >>
                                            // ART_TCP_RCVBUF_DL_SAFETY (16KB). WiFi-connected DMA ceiling ≈ 68KB →
                                            // post-reconnect DMA ~56-63KB ≥ 56KB → art proceeds after reconnect.
                                            // Was 48000: too low — at depleted DMA 50-53KB + 500 storm, art proceeded
                                            // and crashed :928 (log5: dma=53KB → :928 at 13:18:51; dma=51KB → :928 at
                                            // 08:15:21). 56KB = above 500-storm DMA floor (~53KB) but ≤ post-reconnect
                                            // DMA floor (~56-63KB) → still breaks the lyrics-TLS loop.
                                            // Was 64000: set against 45KB pre-SO_RCVBUF burst. Lowered when burst
                                            // dropped to 13-22KB, but 48KB was too aggressive.
                                            // NOTE: art abort path also checks network_mutex holder — if mutex held
                                            // (lyrics TLS active), abort is skipped and task waits for release instead
                                            // of triggering WiFi stop. See ui_album_art.cpp DMA abort block.
#define LYRICS_MIN_FREE_DMA         55000   // Min DMA before lyrics HTTPS fetch. mbedTLS AES fragmentation failure
                                            // confirmed at 44-48KB total free (contiguous alloc fails even with enough
                                            // total DMA). 55KB = 48KB crash floor + 7KB margin.
                                            // When lyrics fires at 55-70KB DMA, TLS consumes ~44KB → DMA dips to
                                            // 11-26KB temporarily. Art task detects mutex held and waits for release
                                            // (see ART_MIN_DMA_PRE_BURST note) rather than triggering WiFi stop.
                                            // Was hardcoded 30000 in lyrics.cpp: too low (AES fails at 44-48KB).
#define ART_DMA_MID_READ_MIN         8000   // Abort if DMA < this DURING the chunk read loop. Belt-and-suspenders:
                                            // if WiFi dynamic RX buffers allocate ~15KB after dl-start passes, this
                                            // catches the resulting DMA drop before it hits the crash floor (~6-7KB).
                                            // Confirmed crash floor: 6744 bytes. 8KB = 6744 + ~1.3KB safety margin.
#define SDIO_QUEUE_POLL_COOLDOWN_MS  3000   // After updateQueue() 20KB Browse response residue (was 2000, too short)
#define SDIO_POST_QUEUE_DRAIN_MS     1000   // Polling task: drain Browse response before next cycle

// Lyrics-specific
#define LYRICS_ART_WAIT_TIMEOUT_MS  15000   // Max wait for art_download_in_progress to clear (storm cooldown 3000ms + download ~2000ms + margin)
#define LYRICS_RETRY_DELAY_MS        2000   // Between lyrics HTTPS fetch retry attempts
#define LYRICS_TCB_REAP_MS            100   // Grace before reusing the static TCB (idle0 reap)
#define CLOCK_BG_MIN_DMA            64000   // Skip clockBgTask photo download if DMA below this.
                                            // TX crash confirmed at 57KB (log16) — 64KB = 7KB margin above crash floor.
                                            // Photo is non-critical (clock still shows, weather still fetches).
                                            // Unlike ART_MIN_DMA_PRE_BURST, NOT lowered: clockBgTask photo HTTP has
                                            // larger burst (no SO_RCVBUF pre-connect path) so higher floor warranted.

// =============================================================================
// WATCHDOG & RELIABILITY
// =============================================================================
#define WATCHDOG_TIMEOUT_SEC    30          // Watchdog timeout (device reboots if stuck)
#define HEAP_LOG_INTERVAL_MS    60000       // Log heap status every 60 seconds

#endif // CONFIG_H
