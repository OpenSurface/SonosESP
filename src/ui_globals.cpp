/**
 * UI Global Variables
 * All shared state for the Sonos Controller UI
 */

#include "ui_common.h"

// ============================================================================
// Color Theme
// ============================================================================
// Every value here already shipped — the ones below the original nine were in
// the code as bare literals, repeated across screens with nothing naming them.
// Naming them is what stops the next screen from inventing a tenth grey.
lv_color_t COL_SCREEN = lv_color_hex(0x121212);
lv_color_t COL_BG = lv_color_hex(0x1A1A1A);
lv_color_t COL_CARD = lv_color_hex(0x2A2A2A);
lv_color_t COL_CARD2 = lv_color_hex(0x252525);
lv_color_t COL_MENU = lv_color_hex(0x222222);
lv_color_t COL_BTN = lv_color_hex(0x3A3A3A);
lv_color_t COL_BTN_PRESSED = lv_color_hex(0x4A4A4A);
lv_color_t COL_SELECTED = lv_color_hex(0x333333);
lv_color_t COL_BORDER = lv_color_hex(0x555555);
lv_color_t COL_TEXT = lv_color_hex(0xFFFFFF);
lv_color_t COL_TEXT2 = lv_color_hex(0x888888);
lv_color_t COL_TEXT3 = lv_color_hex(0xAAAAAA);
lv_color_t COL_ACCENT = lv_color_hex(0xD4A84B);
lv_color_t COL_ERROR = lv_color_hex(0xFF6B6B);
lv_color_t COL_OK = lv_color_hex(0x4ECB71);
lv_color_t COL_WARN = lv_color_hex(0xFFA500);
lv_color_t COL_HEART = lv_color_hex(0xE85D5D);
lv_color_t COL_ERROR_SURFACE      = lv_color_hex(0x8B0000);
lv_color_t COL_OK_SURFACE         = lv_color_hex(0x1E3A1E);
lv_color_t COL_OK_SURFACE_PRESSED = lv_color_hex(0x2A5A2A);
lv_color_t COL_OK_STRONG          = lv_color_hex(0x2E7D32);

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
// Defaults on: this shipped as always-on for the Classic theme, so an existing
// device must look identical until its owner decides otherwise.
bool blur_bg_enabled = true;
// Issue #85: WiFi creds entered in the setup dialog, awaiting persistence. On a flaky
// radio the dialog's connect loop can time out before WL_CONNECTED, so its immediate save
// never runs. checkWiFiReconnect() persists these to NVS once the link is actually up.
String pending_wifi_ssid;
String pending_wifi_pass;
volatile bool wifi_creds_need_save = false;
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
lv_obj_t *lbl_lyrics_status = nullptr;
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
lv_obj_t *pw_strip = nullptr;
lv_obj_t *lbl_pw_ssid = nullptr;
lv_obj_t *spinner_wifi_scan = nullptr;
lv_obj_t *btn_sonos_scan = nullptr;
lv_obj_t *spinner_scan = nullptr;
lv_obj_t *btn_groups_scan = nullptr;
lv_obj_t *spinner_groups_scan = nullptr;

// ============================================================================
// Album Art — Rendering State
// ============================================================================
lv_img_dsc_t art_dsc;
uint16_t* art_buffer = nullptr;
uint16_t* art_temp_buffer = nullptr;
String last_art_url = "";
String pending_art_url = "";
String lyrics_last_track = "";  // Globalised so clock exit can reset it (issue #62)
volatile bool art_ready = false;
volatile bool art_show_placeholder = false;  // Signal UI to show placeholder (art permanently failed)
SemaphoreHandle_t art_mutex = nullptr;
uint32_t dominant_color = 0x1a1a1a;
volatile bool color_ready = false;
int art_offset_x = 0;
int art_offset_y = 0;
bool is_sonos_radio_art = false;
bool pending_is_station_logo = false;

// Blur background
lv_img_dsc_t blur_bg_dsc;
uint16_t*    blur_bg_buf   = nullptr;
volatile bool blur_bg_ready = false;
// True only while blur_bg_buf holds the blur for the CURRENTLY loaded artwork.
// blur_bg_buf is allocated once and lives for the session, so a non-null pointer
// says nothing about whether the pixels are valid (or even initialised — it comes
// from heap_caps_malloc). themeSet() needs this to avoid republishing a previous
// track's blur, or raw uninitialised PSRAM, when the theme changes.
volatile bool blur_bg_valid = false;
lv_obj_t*    img_blur_bg   = nullptr;

// Line-in mode widgets (created hidden in createMainScreen, shown by setLineInMode)
lv_obj_t*    lbl_linein_icon     = nullptr;  // 80px waveform icon, accent colour, pulsing
lv_obj_t*    lbl_linein_subtitle = nullptr;  // "LIVE AUDIO" label below icon

// TV audio mode widgets (created hidden in createMainScreen, shown by setTvAudioMode)
lv_obj_t*    lbl_tv_icon         = nullptr;  // 80px television icon, accent colour, pulsing
lv_obj_t*    lbl_tv_subtitle     = nullptr;  // "TV AUDIO" label below icon

// Current ambient bright color (3× brightened dominant color) — shared between
// color_anim_cb (writer) and updateUI (reader, for shuffle/repeat inactive state)
lv_color_t g_ambient_bright = lv_color_hex(0xD4A84B);  // COL_ACCENT as default until first art

// ============================================================================
// Network Tasks — FreeRTOS Handles and Shutdown Signals
// ============================================================================
TaskHandle_t albumArtTaskHandle = nullptr;
StaticTask_t albumArtTaskTCB;               // TCB in internal SRAM (tiny, ~88 bytes)
StackType_t* art_task_stack = nullptr;      // Stack in PSRAM — allocated once in createArtTask()
volatile bool art_shutdown_requested = false;  // Signal album art task to stop gracefully
volatile bool art_abort_download = false;      // Signal to abort current download (source changed)
TaskHandle_t lyricsTaskHandle = nullptr;
StaticTask_t lyricsTaskTCB;                 // TCB in internal SRAM
StackType_t* lyrics_task_stack = nullptr;   // Stack in PSRAM — allocated once in initLyrics()
volatile bool lyrics_shutdown_requested = false;  // Signal lyrics task to stop for OTA
volatile bool sonos_tasks_shutdown_requested = false;  // Signal Sonos tasks to stop for OTA

// ============================================================================
// SDIO Crash Defence — Network Timing Globals
// (See MEMORY.md and ui_network_guard.h for full crash-defence architecture)
// ============================================================================
SemaphoreHandle_t network_mutex = NULL;  // Created in main.cpp; serialises all WiFi/HTTPS ops
volatile unsigned long last_network_end_ms  = 0;  // Last network op end (200ms general cooldown)
volatile unsigned long last_https_end_ms    = 0;  // Last HTTPS session end (3000ms TLS residue)
volatile unsigned long last_queue_fetch_time = 0; // Last updateQueue() end (3000ms Browse residue)
volatile bool          art_download_in_progress = false; // True during download — suppresses SOAP polling
volatile bool          art_dma_recovery_requested = false; // Set by art task; mainAppTask handles WiFi stop+reconnect/restart (PSRAM-stack tasks cannot call esp_restart/NVS)
volatile unsigned long last_art_download_end_ms = 0; // Set after large HTTP downloads; gates lyrics/queue/inter-download cooldowns
volatile unsigned long last_track_change_ms  = 0; // Set by requestAlbumArt(); 2000ms NOTIFY settle
volatile unsigned long last_transient_500_ms = 0; // Set by sendSOAP() on 500; 3000ms HLS storm gate

// On-demand queue window fetch (set by ev_queue / refresh button; consumed by polling task)
volatile bool queue_fetch_requested   = false;
volatile int  queue_fetch_start_index = 0;    // 0-based SOAP StartingIndex for the window

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
volatile bool ui_force_refresh = false;
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
// Clock / Screensaver State
// ============================================================================
#include "clock_screen.h"

int  clock_mode           = CLOCK_DEFAULT_MODE;
int  clock_timeout_min    = CLOCK_DEFAULT_TIMEOUT;
int  clock_tz_idx         = CLOCK_DEFAULT_TZ_IDX;
bool clock_picsum_enabled = (bool)CLOCK_DEFAULT_PICSUM;
int  clock_refresh_min    = CLOCK_DEFAULT_REFRESH;
int  clock_bg_kw_idx      = CLOCK_DEFAULT_KW_IDX;
bool clock_12h            = (bool)CLOCK_DEFAULT_12H;
int  clock_style          = CLOCK_DEFAULT_STYLE;
// 7" LCD panel variant (PANEL_VARIANT_*). Read from NVS in setup() BEFORE
// display_init(), because the panel init sequence depends on it.
int  panel_variant        = PANEL_VARIANT_DEFAULT;
bool clock_weather_enabled  = (bool)CLOCK_DEFAULT_WEATHER_EN;
int  clock_weather_city_idx = CLOCK_DEFAULT_WEATHER_CITY;
bool clock_wx_fahrenheit    = (bool)CLOCK_DEFAULT_WEATHER_FAHR;
// Custom location override (issue #74) — atomic-safe globals (read by bg task, written by UI)
volatile float clock_custom_lat = 0.0f;
volatile float clock_custom_lon = 0.0f;
char clock_custom_name[64] = "";  // optional display-only label for the saver screen
int           clock_wx_temp     = 0;
int           clock_wx_humidity = 0;
int           clock_wx_wind     = 0;
int           clock_wx_wmo      = 0;
ClockWxHour   clock_wx_hourly[6] = {};
char          clock_wx_city_name[64] = "";
bool          clock_wx_valid    = false;
int           clock_wx_uv       = 0;
int           clock_wx_apparent = 0;
char          clock_wx_sunrise[8] = "--:--";
char          clock_wx_sunset[8]  = "--:--";
volatile bool clock_weather_updated       = false;
volatile bool clock_weather_needs_refetch = false;

ClockState clock_state             = CLOCK_IDLE;
uint32_t   clock_entering_start_ms = 0;
uint32_t   clock_exiting_start_ms  = 0;
uint32_t   last_clock_exit_ms      = 0;

TaskHandle_t         clockBgTaskHandle          = nullptr;
StaticTask_t         clkbgTaskTCB;                         // TCB in internal SRAM (tiny, ~88 bytes)
StackType_t*         clkbg_task_stack           = nullptr; // Stack in PSRAM — allocated once, reused across sessions
volatile bool        clock_bg_shutdown_requested = false;
volatile bool        clock_bg_ready             = false;
uint16_t*            clock_bg_buffer            = nullptr;
lv_img_dsc_t         clock_bg_dsc;

lv_obj_t* scr_clock          = nullptr;
lv_obj_t* scr_clock_settings = nullptr;
lv_obj_t* clock_bg_img       = nullptr;

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
