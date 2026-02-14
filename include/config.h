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

// =============================================================================
// DISPLAY SETTINGS
// =============================================================================
#define DEFAULT_BRIGHTNESS      100     // Default brightness percentage
#define DEFAULT_BRIGHTNESS_DIM  20      // Default dimmed brightness
#define DEFAULT_AUTODIM_SEC     30      // Default auto-dim timeout (seconds)
#define MIN_BRIGHTNESS          5       // Minimum brightness allowed
#define MAX_BRIGHTNESS          100     // Maximum brightness

// Display dimensions (LVGL renders in landscape, driver rotates to portrait panel)
#define DISPLAY_WIDTH           800     // LVGL width (landscape)
#define DISPLAY_HEIGHT          480     // LVGL height (landscape)
#define PANEL_WIDTH             480     // Physical panel width (portrait)
#define PANEL_HEIGHT            800     // Physical panel height (portrait)

// =============================================================================
// ALBUM ART
// =============================================================================
#define ART_DISPLAY_SIZE        420     // Album art display size (pixels)
#define ART_MAX_DOWNLOAD_SIZE   (280 * 1024)  // Max JPEG download buffer (280KB)
#define ART_TASK_STACK_SIZE     7000    // Album art task stack (increased for HW JPEG + HTTPS/TLS)
#define ART_TASK_PRIORITY       0       // Album art task priority
#define ART_DOWNLOAD_TIMEOUT_MS 8000    // Download timeout
#define ART_CHECK_INTERVAL_MS   100     // How often to check for new art requests

// =============================================================================
// SONOS CONTROLLER
// =============================================================================
#define SONOS_MAX_DEVICES       10      // Maximum discoverable devices
#define SONOS_QUEUE_SIZE_MAX    500     // Maximum queue items to fetch
#define SONOS_QUEUE_BATCH_SIZE  50      // Items per queue fetch request
#define SONOS_CMD_QUEUE_SIZE    10      // Command queue depth
#define SONOS_UI_QUEUE_SIZE     20      // UI update queue depth

// Task configuration (profiled: Net uses ~16KB, Poll uses ~7.5KB of allocated)
#define SONOS_NET_TASK_STACK    3500    // Network task stack size (was 6144, ~10KB saved)
#define SONOS_POLL_TASK_STACK   3000    // Polling task stack size (was 4096, ~4KB saved)
#define SONOS_NET_TASK_PRIORITY 2       // Network task priority
#define SONOS_POLL_TASK_PRIORITY 3      // Polling task priority

// Timeouts
#define SONOS_SOAP_TIMEOUT_MS   2000    // SOAP request timeout
#define SONOS_DEBOUNCE_MS       400     // Command debounce time
#define SONOS_POLL_INTERVAL_MS  150     // Polling loop interval (optimized: was 100ms, saves ~30% CPU)

// Polling tick modulos (base interval = 300ms, so N ticks = N * 300ms)
#define POLL_VOLUME_MODULO      5       // Volume every 1.5s (5 * 300ms)
#define POLL_TRANSPORT_MODULO   10      // Transport settings every 3s
#define POLL_QUEUE_MODULO       100     // Queue every 30s (optimized: was 50/15s)
#define POLL_MEDIA_INFO_MODULO  50      // Radio station info every 15s
#define POLL_BASE_INTERVAL_MS   300     // Base polling interval

// =============================================================================
// OTA UPDATES
// =============================================================================
#define OTA_BUFFER_SIZE         1024    // Download buffer size
#define OTA_READ_SIZE           1024    // Max bytes per read (1KB - safe with adaptive throttling)
#define OTA_MAX_FIRMWARE_SIZE   (10 * 1024 * 1024)  // 10MB max firmware
#define OTA_DOWNLOAD_TIMEOUT_MS 300000  // 5 minutes max for entire download
#define OTA_STALL_TIMEOUT_MS    30000   // 30 seconds max with no data received
#define OTA_PROGRESS_LOG_INTERVAL 10    // Log every N percent
#define OTA_DMA_CHECK_INTERVAL  4       // Check DMA pressure every N chunks (~4KB)
#define OTA_DMA_CRITICAL        4096    // DMA critical threshold (80ms delay)
#define OTA_DMA_LOW             8192    // DMA low threshold (30ms delay)
#define OTA_BASE_DELAY_MS       15      // Base per-chunk delay (~65KB/s, ~25s for 1.5MB)
#define OTA_SETTLE_AFTER_TLS_MS 1000    // Settle time after TLS handshake
#define OTA_TARGET_FREE_DMA     (110 * 1024)  // Need 110KB free before OTA TLS
#define OTA_DMA_WAIT_TIMEOUT_MS 10000   // Max wait for DMA cleanup
#define OTA_HTTPS_COOLDOWN_MS   2000    // Wait for previous HTTPS cleanup
#define OTA_CHECK_DEBOUNCE_MS   5000    // Min delay between update checks
#define OTA_CHECK_TIMEOUT_MS    15000   // HTTP timeout for version check
#define OTA_CHECK_CLEANUP_MS    500     // Delay after version check TLS cleanup

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
#define NVS_KEY_BRIGHTNESS_DIM  "brightness_dimmed"
#define NVS_KEY_AUTODIM         "autodim_sec"
#define NVS_KEY_OTA_CHANNEL     "ota_channel"
#define NVS_KEY_CACHED_DEVICE   "cached_dev"
#define NVS_KEY_LYRICS          "lyrics"

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
// QUEUE / PLAYLIST
// =============================================================================
#define QUEUE_ADD_AT_END        4294967295  // Add to end of queue constant

// =============================================================================
// WATCHDOG & RELIABILITY
// =============================================================================
#define WATCHDOG_TIMEOUT_SEC    30          // Watchdog timeout (device reboots if stuck)
#define HEAP_LOG_INTERVAL_MS    60000       // Log heap status every 60 seconds

#endif // CONFIG_H
