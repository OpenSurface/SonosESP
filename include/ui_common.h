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
#include "ui_icons.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "sonos_controller.h"
#include "esp_heap_caps.h"
#include "ui_scale.h"   // SX()/SY()/SMIN() resolution-relative scaling

// Default WiFi credentials (empty = force WiFi setup via UI)
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""

// Firmware version
#define FIRMWARE_VERSION "1.14.2"
#define GITHUB_REPO "OpenSurface/SonosESP"
#define GITHUB_API_URL "https://api.github.com/repos/" GITHUB_REPO "/releases/latest"

// Album art configuration
#define ART_SIZE 420
#define MAX_ART_SIZE 280000          // 280KB max - allows Spotify 640x640 images
#define ART_CHUNK_SIZE 4096          // 4KB chunks for HTTP downloads

// Network configuration
#define NETWORK_MUTEX_TIMEOUT_MS 5000    // Timeout for acquiring network mutex (SOAP)
#define NETWORK_MUTEX_TIMEOUT_ART_MS 10000 // Longer timeout for album art downloads

// Sonos logo declaration
LV_IMG_DECLARE(Sonos_idnu60bqes_1);

// ============================================================================
// Color Theme - extern declarations
// ============================================================================
// Surfaces — COL_SCREEN is the page itself; COL_BG is a panel/list sitting on
// it. They are NOT interchangeable and were never meant to be the same value.
extern lv_color_t COL_SCREEN;        // screen background
extern lv_color_t COL_BG;            // panel / list background
extern lv_color_t COL_CARD;          // raised card
extern lv_color_t COL_CARD2;         // card in a highlighted / current row
extern lv_color_t COL_MENU;          // open dropdown list

// Controls
extern lv_color_t COL_BTN;
extern lv_color_t COL_BTN_PRESSED;
extern lv_color_t COL_SELECTED;      // selection highlight, and the slider/bar groove
extern lv_color_t COL_BORDER;

// Text
extern lv_color_t COL_TEXT;          // primary
extern lv_color_t COL_TEXT2;         // secondary
extern lv_color_t COL_TEXT3;         // tertiary / hint

// Semantic — these carry meaning, so they are not interchangeable with the
// accent even when they happen to look similar.
extern lv_color_t COL_ACCENT;
extern lv_color_t COL_ERROR;         // failure / offline
extern lv_color_t COL_OK;            // success / online
extern lv_color_t COL_WARN;          // caution
extern lv_color_t COL_HEART;         // favourite — a role, not a generic red

// ============================================================================
// Global Objects - extern declarations
// ============================================================================
extern SonosController sonos;
extern Preferences wifiPrefs;

// Display brightness settings
extern int brightness_level;
extern int brightness_dimmed;
extern int autodim_timeout;
extern int panel_variant;   // PANEL_VARIANT_* — which 7" LCD panel is fitted
extern bool lyrics_enabled;
extern bool blur_bg_enabled;
extern String pending_wifi_ssid;        // issue #85: creds awaiting persistence
extern String pending_wifi_pass;
extern volatile bool wifi_creds_need_save;
extern uint32_t last_touch_time;
extern bool screen_dimmed;

// Screen objects
extern lv_obj_t *scr_main, *scr_devices, *scr_queue, *scr_settings;
extern lv_obj_t *scr_wifi, *scr_sources, *scr_browse, *scr_display, *scr_ota, *scr_groups, *scr_general;
extern lv_obj_t *scr_clock, *scr_clock_settings;

// Main screen UI elements
extern lv_obj_t *img_album, *lbl_title, *lbl_artist, *lbl_album, *lbl_time, *lbl_time_remaining;
extern lv_obj_t *lbl_lyrics_status;  // Lyrics status indicator (top of album art)
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
extern lv_obj_t *pw_strip, *lbl_pw_ssid, *spinner_wifi_scan;
extern lv_obj_t *btn_sonos_scan, *spinner_scan;
extern lv_obj_t *btn_groups_scan, *spinner_groups_scan;

// Album art — rendering state
extern lv_img_dsc_t art_dsc;
extern uint16_t *art_buffer;
extern uint16_t *art_temp_buffer;
extern String last_art_url, pending_art_url;
extern String lyrics_last_track;
extern volatile bool art_ready;
extern volatile bool art_show_placeholder;
extern SemaphoreHandle_t art_mutex;
extern uint32_t dominant_color;
extern volatile bool color_ready;
extern int art_offset_x, art_offset_y;

// Blur background — blurred art scaled to full screen (replaces ambient color animation)
extern lv_img_dsc_t blur_bg_dsc;
extern uint16_t*    blur_bg_buf;
extern volatile bool blur_bg_ready;
extern volatile bool blur_bg_valid;   // blur_bg_buf matches the current artwork
extern lv_obj_t*    img_blur_bg;
extern lv_obj_t*    lbl_linein_icon;
extern lv_obj_t*    lbl_linein_subtitle;
extern lv_obj_t*    lbl_tv_icon;
extern lv_obj_t*    lbl_tv_subtitle;
extern lv_color_t   g_ambient_bright;  // Current 3× brightened dominant color for playback buttons
extern bool is_sonos_radio_art;
extern bool pending_is_station_logo;

// SDIO crash defence — network timing globals
extern SemaphoreHandle_t network_mutex;
extern volatile unsigned long last_network_end_ms;
extern volatile unsigned long last_https_end_ms;
extern volatile unsigned long last_queue_fetch_time;
extern volatile bool          art_download_in_progress;
extern volatile bool          art_dma_recovery_requested;  // Set by art task; mainAppTask handles WiFi stop+reconnect/restart
extern volatile unsigned long last_art_download_end_ms;
extern volatile unsigned long last_track_change_ms;
extern volatile unsigned long last_transient_500_ms;

// On-demand queue window fetch
extern volatile bool queue_fetch_requested;
extern volatile int  queue_fetch_start_index;

// UI state
extern String ui_title, ui_artist, ui_repeat;
extern int ui_vol;
extern bool ui_playing, ui_shuffle, ui_muted;
extern bool dragging_vol, dragging_prog;

// Set to force updateUI() to re-push every value to the widgets on its next pass,
// bypassing the "only write when changed" caches. Needed after the player screen
// is rebuilt (theme switch): the new widgets start blank while the caches still
// hold the playing track, so nothing would be redrawn until the song changed.
// Covers the caches that live as function-local statics and can't be reset directly.
extern volatile bool ui_force_refresh;

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
extern volatile bool ota_in_progress;  // Flag to skip non-essential tasks during OTA
extern SemaphoreHandle_t ota_progress_mutex;  // Protects OTA progress updates and state

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
void createGeneralScreen();
void createClockScreen();
void createClockSettingsScreen();

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
void runPanelWizard();   // 7": confirm-or-advance panel selection (no-op on 4")
void setBrightness(int level);
void resetScreenTimeout();
void checkAutoDim();
void requestAlbumArt(const String &url);
bool isAlbumArtPending();   // H-4: thread-safe `pending_art_url != last_art_url` (takes art_mutex)
void clearAlbumArtCache();  // Invalidate LRU cache on track change
void updateUI();
void processUpdates();
void triggerPendingOTA();  // Called from setup() if NVS_KEY_OTA_PENDING was saved before reboot — runs OTA at boot before background tasks start (full DMA headroom)
String urlEncode(const char *url);
// (cleanupBrowseData removed — browse rows now free their ItemData via LV_EVENT_DELETE)
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

// Network tasks — FreeRTOS handles and shutdown signals
// Art and lyrics stacks live in PSRAM to free internal SRAM for SDIO/WiFi DMA buffers
extern TaskHandle_t albumArtTaskHandle;
extern StaticTask_t albumArtTaskTCB;
extern StackType_t* art_task_stack;
extern volatile bool art_shutdown_requested;
extern volatile bool art_abort_download;
void albumArtTask(void *param);
void createArtTask();   // PSRAM-stack wrapper — use instead of xTaskCreatePinnedToCore directly

extern TaskHandle_t lyricsTaskHandle;
extern StaticTask_t lyricsTaskTCB;
extern StackType_t* lyrics_task_stack;
extern volatile bool lyrics_shutdown_requested;

extern volatile bool sonos_tasks_shutdown_requested;

// Clock / screensaver
void checkClockTrigger();
void exitClockScreen();
void clockBgTask(void* param);

// Radio mode UI adaptation
void setRadioMode(bool enable);
void updateRadioModeUI();

// Line-in mode UI adaptation
void setLineInMode(bool enable);
void updateLineInUI();

// TV audio mode UI adaptation
void setTvAudioMode(bool enable);
void updateTvAudioUI();

#endif // UI_COMMON_H
