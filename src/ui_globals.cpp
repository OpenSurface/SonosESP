/**
 * UI Global Variables
 * All shared state for the Sonos Controller UI
 */

#include "ui_common.h"

// ============================================================================
// Color Theme
// ============================================================================
lv_color_t COL_BG = lv_color_hex(0x1A1A1A);
lv_color_t COL_CARD = lv_color_hex(0x2A2A2A);
lv_color_t COL_BTN = lv_color_hex(0x3A3A3A);
lv_color_t COL_BTN_PRESSED = lv_color_hex(0x4A4A4A);
lv_color_t COL_TEXT = lv_color_hex(0xFFFFFF);
lv_color_t COL_TEXT2 = lv_color_hex(0x888888);
lv_color_t COL_ACCENT = lv_color_hex(0xD4A84B);
lv_color_t COL_HEART = lv_color_hex(0xE85D5D);
lv_color_t COL_SELECTED = lv_color_hex(0x333333);

// ============================================================================
// Core Objects
// ============================================================================
SonosController sonos;
Preferences wifiPrefs;

// ============================================================================
// Display Settings
// ============================================================================
int brightness_level = 100;
int brightness_dimmed = 20;
int autodim_timeout = 30;
bool lyrics_enabled = true;
uint32_t last_touch_time = 0;
bool screen_dimmed = false;

// ============================================================================
// Screen Objects
// ============================================================================
lv_obj_t *scr_main = nullptr;
lv_obj_t *scr_devices = nullptr;
lv_obj_t *scr_queue = nullptr;
lv_obj_t *scr_settings = nullptr;
lv_obj_t *scr_wifi = nullptr;
lv_obj_t *scr_sources = nullptr;
lv_obj_t *scr_browse = nullptr;
lv_obj_t *scr_display = nullptr;
lv_obj_t *scr_ota = nullptr;
lv_obj_t *scr_groups = nullptr;
lv_obj_t *scr_general = nullptr;

// ============================================================================
// Main Screen UI Elements
// ============================================================================
lv_obj_t *img_album = nullptr;
lv_obj_t *lbl_title = nullptr;
lv_obj_t *lbl_artist = nullptr;
lv_obj_t *lbl_album = nullptr;
lv_obj_t *lbl_time = nullptr;
lv_obj_t *lbl_time_remaining = nullptr;
lv_obj_t *btn_play = nullptr;
lv_obj_t *btn_prev = nullptr;
lv_obj_t *btn_next = nullptr;
lv_obj_t *btn_mute = nullptr;
lv_obj_t *btn_shuffle = nullptr;
lv_obj_t *btn_repeat = nullptr;
lv_obj_t *btn_queue = nullptr;
lv_obj_t *slider_progress = nullptr;
lv_obj_t *slider_vol = nullptr;
lv_obj_t *panel_right = nullptr;
lv_obj_t *panel_art = nullptr;
lv_obj_t *img_next_album = nullptr;
lv_obj_t *lbl_next_title = nullptr;
lv_obj_t *lbl_next_artist = nullptr;
lv_obj_t *lbl_next_header = nullptr;
lv_obj_t *lbl_wifi_icon = nullptr;
lv_obj_t *lbl_device_name = nullptr;

// ============================================================================
// Lists and Status Labels
// ============================================================================
lv_obj_t *list_devices = nullptr;
lv_obj_t *list_queue = nullptr;
lv_obj_t *lbl_status = nullptr;
lv_obj_t *lbl_queue_status = nullptr;
lv_obj_t *list_groups = nullptr;
lv_obj_t *lbl_groups_status = nullptr;

// ============================================================================
// WiFi Screen Elements
// ============================================================================
lv_obj_t *art_placeholder = nullptr;
lv_obj_t *list_wifi = nullptr;
lv_obj_t *lbl_wifi_status = nullptr;
lv_obj_t *ta_password = nullptr;
lv_obj_t *kb = nullptr;
lv_obj_t *btn_wifi_scan = nullptr;
lv_obj_t *btn_wifi_connect = nullptr;
lv_obj_t *lbl_scan_text = nullptr;
lv_obj_t *btn_sonos_scan = nullptr;
lv_obj_t *spinner_scan = nullptr;
lv_obj_t *btn_groups_scan = nullptr;
lv_obj_t *spinner_groups_scan = nullptr;

// ============================================================================
// Album Art
// ============================================================================
lv_img_dsc_t art_dsc;
uint16_t* art_buffer = nullptr;
uint16_t* art_temp_buffer = nullptr;
String last_art_url = "";
String pending_art_url = "";
volatile bool art_ready = false;
SemaphoreHandle_t art_mutex = nullptr;
TaskHandle_t albumArtTaskHandle = nullptr;
volatile bool art_shutdown_requested = false;  // Signal album art to stop gracefully
volatile bool art_abort_download = false;      // Signal to abort current download (source changed)
volatile bool sonos_tasks_shutdown_requested = false;  // Signal Sonos tasks to stop for OTA
uint32_t dominant_color = 0x1a1a1a;
volatile bool color_ready = false;
int art_offset_x = 0;
int art_offset_y = 0;
bool is_sonos_radio_art = false;
bool pending_is_station_logo = false;
volatile unsigned long last_queue_fetch_time = 0;
SemaphoreHandle_t network_mutex = NULL;  // Created in main.cpp
volatile unsigned long last_network_end_ms = 0;  // Last network operation end time (for SDIO cooldown)

// ============================================================================
// UI State
// ============================================================================
String ui_title = "";
String ui_artist = "";
String ui_repeat = "";
int ui_vol = -1;
bool ui_playing = false;
bool ui_shuffle = false;
bool ui_muted = false;
bool dragging_vol = false;
bool dragging_prog = false;

// ============================================================================
// WiFi State
// ============================================================================
String selectedSSID = "";
int kb_mode = 0;
String wifiNetworks[20];
int wifiNetworkCount = 0;

// ============================================================================
// Browse State
// ============================================================================
String current_browse_id = "";
String current_browse_title = "";

// ============================================================================
// Groups State
// ============================================================================
int selected_group_coordinator = -1;

// ============================================================================
// OTA Update State
// ============================================================================
lv_obj_t* lbl_ota_status = nullptr;
lv_obj_t* lbl_ota_progress = nullptr;
lv_obj_t* lbl_current_version = nullptr;
lv_obj_t* lbl_latest_version = nullptr;
lv_obj_t* btn_check_update = nullptr;
lv_obj_t* btn_install_update = nullptr;
lv_obj_t* bar_ota_progress = nullptr;
lv_obj_t* dd_ota_channel = nullptr;
String latest_version = "";
String download_url = "";
int ota_channel = 0;  // 0=Stable, 1=Nightly
volatile bool ota_in_progress = false;  // Flag to skip non-essential tasks during OTA
SemaphoreHandle_t ota_progress_mutex = NULL;  // Created in main.cpp
