#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "lvgl.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "sonos_controller.h"
#include "esp_heap_caps.h"

// Default WiFi credentials (empty = force WiFi setup via UI)
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""

// Firmware version
#define FIRMWARE_VERSION "1.1.7"
#define GITHUB_REPO "OpenSurface/SonosESP"
#define GITHUB_API_URL "https://api.github.com/repos/" GITHUB_REPO "/releases/latest"

// Album art configuration
#define ART_SIZE 420
#define MAX_ART_SIZE 280000          // 280KB max - allows Spotify 640x640 images
#define ART_CHUNK_SIZE 4096          // 4KB chunks for HTTP downloads
#define ART_READ_TIMEOUT_MS 5000     // 5 second timeout for image downloads
#define ART_COMPACT_THRESHOLD 200000 // Compact buffer if image >200KB

// Network configuration
#define NETWORK_MUTEX_TIMEOUT_MS 5000    // Timeout for acquiring network mutex (SOAP)
#define NETWORK_MUTEX_TIMEOUT_ART_MS 10000 // Longer timeout for album art downloads
#define WIFI_RECONNECT_INTERVAL_MS 2000  // Try reconnect every 2 seconds

// Task configuration
#define TASK_PRIORITY_ALBUM_ART 1    // Low priority - background task
#define TASK_PRIORITY_NETWORK 2      // Medium priority
#define TASK_PRIORITY_POLLING 3      // High priority - UI responsiveness
#define TASK_STACK_ALBUM_ART 8192    // 8KB stack for album art task

// Sonos logo declaration
LV_IMG_DECLARE(Sonos_idnu60bqes_1);

// ============================================================================
// Color Theme - extern declarations
// ============================================================================
extern lv_color_t COL_BG;
extern lv_color_t COL_CARD;
extern lv_color_t COL_BTN;
extern lv_color_t COL_BTN_PRESSED;
extern lv_color_t COL_TEXT;
extern lv_color_t COL_TEXT2;
extern lv_color_t COL_ACCENT;
extern lv_color_t COL_HEART;
extern lv_color_t COL_SELECTED;

// ============================================================================
// Global Objects - extern declarations
// ============================================================================
extern SonosController sonos;
extern Preferences wifiPrefs;

// Display brightness settings
extern int brightness_level;
extern int brightness_dimmed;
extern int autodim_timeout;
extern uint32_t last_touch_time;
extern bool screen_dimmed;

// Screen objects
extern lv_obj_t *scr_main, *scr_devices, *scr_queue, *scr_settings;
extern lv_obj_t *scr_wifi, *scr_sources, *scr_browse, *scr_display, *scr_ota, *scr_groups;

// Main screen UI elements
extern lv_obj_t *img_album, *lbl_title, *lbl_artist, *lbl_album, *lbl_time, *lbl_time_remaining;
extern lv_obj_t *btn_play, *btn_prev, *btn_next, *btn_mute, *btn_shuffle, *btn_repeat, *btn_queue;
extern lv_obj_t *slider_progress, *slider_vol;
extern lv_obj_t *panel_right, *panel_art;
extern lv_obj_t *img_next_album, *lbl_next_title, *lbl_next_artist, *lbl_next_header;
extern lv_obj_t *lbl_wifi_icon, *lbl_device_name;

// Lists and status labels
extern lv_obj_t *list_devices, *list_queue, *lbl_status, *lbl_queue_status;
extern lv_obj_t *list_groups, *lbl_groups_status;

// WiFi screen elements
extern lv_obj_t *art_placeholder, *list_wifi, *lbl_wifi_status, *ta_password, *kb;
extern lv_obj_t *btn_wifi_scan, *btn_wifi_connect, *lbl_scan_text;
extern lv_obj_t *btn_sonos_scan, *spinner_scan;
extern lv_obj_t *btn_groups_scan, *spinner_groups_scan;

// Album art
extern lv_img_dsc_t art_dsc;
extern uint16_t *art_buffer;
extern uint16_t *art_temp_buffer;
extern String last_art_url, pending_art_url;
extern volatile bool art_ready;
extern SemaphoreHandle_t art_mutex;
extern uint32_t dominant_color;
extern volatile bool color_ready;
extern int art_offset_x, art_offset_y;
extern bool is_sonos_radio_art;
extern bool pending_is_station_logo;  // True when requesting radio station logo (PNG allowed)
extern unsigned long last_source_change_time;
extern volatile unsigned long last_queue_fetch_time;  // Track queue fetches for WiFi coordination
extern SemaphoreHandle_t network_mutex;  // Serialize network access to prevent SDIO buffer overflow

// UI state
extern String ui_title, ui_artist, ui_repeat;
extern int ui_vol;
extern bool ui_playing, ui_shuffle, ui_muted;
extern bool dragging_vol, dragging_prog;

// WiFi state
extern String selectedSSID;
extern int kb_mode;
extern String wifiNetworks[20];
extern int wifiNetworkCount;

// Browse state
extern String current_browse_id;
extern String current_browse_title;

// Groups state
extern int selected_group_coordinator;

// OTA update state
extern lv_obj_t *lbl_ota_status;
extern lv_obj_t *lbl_ota_progress;
extern lv_obj_t *lbl_current_version;
extern lv_obj_t *lbl_latest_version;
extern lv_obj_t *btn_check_update;
extern lv_obj_t *btn_install_update;
extern lv_obj_t *bar_ota_progress;
extern lv_obj_t *dd_ota_channel;
extern String latest_version;
extern String download_url;
extern int ota_channel;  // 0=Stable, 1=Nightly

// ============================================================================
// Function Declarations - Screen Creation
// ============================================================================
void createMainScreen();
void createDevicesScreen();
void createQueueScreen();
void createSettingsScreen();
void createDisplaySettingsScreen();
void createWiFiScreen();
void createOTAScreen();
void createSourcesScreen();
void createBrowseScreen();
void createGroupsScreen();

// ============================================================================
// Function Declarations - UI Refresh
// ============================================================================
void refreshDeviceList();
void refreshQueueList();
void refreshGroupsList();

// ============================================================================
// Function Declarations - Event Handlers
// ============================================================================
void ev_play(lv_event_t *e);
void ev_prev(lv_event_t *e);
void ev_next(lv_event_t *e);
void ev_shuffle(lv_event_t *e);
void ev_repeat(lv_event_t *e);
void ev_progress(lv_event_t *e);
void ev_vol_slider(lv_event_t *e);
void ev_mute(lv_event_t *e);
void ev_devices(lv_event_t *e);
void ev_queue(lv_event_t *e);
void ev_settings(lv_event_t *e);
void ev_display_settings(lv_event_t *e);
void ev_back_main(lv_event_t *e);
void ev_back_settings(lv_event_t *e);
void ev_groups(lv_event_t *e);
void ev_discover(lv_event_t *e);
void ev_queue_item(lv_event_t *e);
void ev_wifi_scan(lv_event_t *e);
void ev_wifi_connect(lv_event_t *e);
void ev_check_update(lv_event_t *e);
void ev_install_update(lv_event_t *e);
void ev_ota_settings(lv_event_t *e);

// ============================================================================
// Function Declarations - Utilities
// ============================================================================
void setBackgroundColor(uint32_t hex_color);
void setBrightness(int level);
void resetScreenTimeout();
void checkAutoDim();
void requestAlbumArt(const String &url);
void updateUI();
void processUpdates();
String urlEncode(const char *url);
void cleanupBrowseData(lv_obj_t *list);
lv_obj_t *createSettingsSidebar(lv_obj_t *screen, int activeIdx);

// HTML entity decoding helper (inline to avoid code duplication)
inline String decodeHTMLEntities(const String& str) {
    String result = str;
    result.replace("&lt;", "<");
    result.replace("&gt;", ">");
    result.replace("&quot;", "\"");
    result.replace("&#39;", "'");
    result.replace("&amp;", "&");  // Must be last to avoid double-decoding
    return result;
}

// Album art task
extern TaskHandle_t albumArtTaskHandle;
extern volatile bool art_shutdown_requested;
extern volatile bool art_abort_download;
void albumArtTask(void *param);

// Radio mode UI adaptation
bool isCurrentlyRadio();
void setRadioMode(bool enable);
void updateRadioModeUI();

#endif // UI_COMMON_H
