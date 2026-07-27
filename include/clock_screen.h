/**
 * Clock / Screensaver Feature
 * NTP clock with optional loremflickr.com photo background
 * Declarations, timezone list, and state management
 */

#ifndef CLOCK_SCREEN_H
#define CLOCK_SCREEN_H

#include <Arduino.h>
#include "lvgl.h"
#include "config.h"

// ============================================================================
// Timezone List (82 zones — IANA name + POSIX TZ string)
// ============================================================================
struct ClockZone {
    const char* name;
    const char* posix;
};

static const ClockZone CLOCK_ZONES[] = {
    // UTC
    {"UTC",                              "UTC0"},
    // Africa
    {"Africa/Abidjan",                   "GMT0"},
    {"Africa/Addis_Ababa",               "EAT-3"},
    {"Africa/Cairo",                     "EET-2"},
    {"Africa/Casablanca",                "WET0"},
    {"Africa/Johannesburg",              "SAST-2"},
    {"Africa/Lagos",                     "WAT-1"},
    {"Africa/Nairobi",                   "EAT-3"},
    // Americas
    {"America/Adak",                     "HAST10HADT,M3.2.0,M11.1.0"},
    {"America/Anchorage",                "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Argentina/Buenos_Aires",   "ART3"},
    {"America/Bogota",                   "COT5"},
    {"America/Caracas",                  "VET4:30"},
    {"America/Chicago",                  "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver",                   "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Halifax",                  "AST4ADT,M3.2.0,M11.1.0"},
    {"America/Lima",                     "PET5"},
    {"America/Los_Angeles",              "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City",              "CST6CDT,M4.1.0,M10.5.0"},
    {"America/New_York",                 "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Phoenix",                  "MST7"},
    {"America/Santiago",                 "CLT4CLST,M10.2.6/24,M3.2.6/24"},
    {"America/Sao_Paulo",                "BRT3BRST,M10.3.0/0,M2.3.0/0"},
    {"America/St_Johns",                 "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Toronto",                  "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver",                "PST8PDT,M3.2.0,M11.1.0"},
    // Asia
    {"Asia/Baghdad",                     "AST-3"},
    {"Asia/Bangkok",                     "ICT-7"},
    {"Asia/Colombo",                     "IST-5:30"},
    {"Asia/Dhaka",                       "BDT-6"},
    {"Asia/Dubai",                       "GST-4"},
    {"Asia/Ho_Chi_Minh",                 "ICT-7"},
    {"Asia/Hong_Kong",                   "HKT-8"},
    {"Asia/Jakarta",                     "WIB-7"},
    {"Asia/Karachi",                     "PKT-5"},
    {"Asia/Kathmandu",                   "NPT-5:45"},
    {"Asia/Kolkata",                     "IST-5:30"},
    {"Asia/Kuala_Lumpur",                "MYT-8"},
    {"Asia/Kuwait",                      "AST-3"},
    {"Asia/Manila",                      "PST-8"},
    {"Asia/Riyadh",                      "AST-3"},
    {"Asia/Seoul",                       "KST-9"},
    {"Asia/Shanghai",                    "CST-8"},
    {"Asia/Singapore",                   "SGT-8"},
    {"Asia/Taipei",                      "CST-8"},
    {"Asia/Tehran",                      "IRST-3:30IRDT,80/0,264/0"},
    {"Asia/Tokyo",                       "JST-9"},
    {"Asia/Yekaterinburg",               "YEKT-5"},
    // Atlantic
    {"Atlantic/Azores",                  "AZOT1AZOST,M3.5.0/0,M10.5.0/1"},
    {"Atlantic/Reykjavik",               "GMT0"},
    // Australia
    {"Australia/Adelaide",               "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane",               "AEST-10"},
    {"Australia/Darwin",                 "ACST-9:30"},
    {"Australia/Melbourne",              "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth",                  "AWST-8"},
    {"Australia/Sydney",                 "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    // Europe
    {"Europe/Amsterdam",                 "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens",                    "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Berlin",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels",                  "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Bucharest",                 "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Budapest",                  "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen",                "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Dublin",                    "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Helsinki",                  "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul",                  "TRT-3"},
    {"Europe/Kiev",                      "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Lisbon",                    "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/London",                    "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Madrid",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Moscow",                    "MSK-3"},
    {"Europe/Oslo",                      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris",                     "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome",                      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm",                 "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Tallinn",                   "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Vienna",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
    // Pacific
    {"Pacific/Auckland",                 "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Fiji",                     "FJT-12"},
    {"Pacific/Guam",                     "ChST-10"},
    {"Pacific/Honolulu",                 "HST10"},
    {"Pacific/Port_Moresby",             "PGT-10"},
};

static const int CLOCK_ZONES_COUNT = (int)(sizeof(CLOCK_ZONES) / sizeof(CLOCK_ZONES[0]));

// ============================================================================
// Photo background keyword list (used by settings dropdown + clockBgTask URL)
// ============================================================================
struct ClockBgKeyword {
    const char* label;  // Shown in settings dropdown
    const char* kw;     // Appended to loremflickr URL ("" = no keyword = truly random)
};

static const ClockBgKeyword CLOCK_BG_KEYWORDS[] = {
    {"Random",        ""},
    {"Landscape",     "landscape"},
    {"Nature",        "nature"},
    {"City",          "city"},
    {"Architecture",  "architecture"},
    {"Ocean",         "ocean"},
    {"Mountain",      "mountain"},
    {"Forest",        "forest"},
    {"Sunset",        "sunset"},
    {"Abstract",      "abstract"},
    {"Travel",        "travel"},
    {"Space",         "space"},
    {"Winter",        "winter"},
    {"Autumn",        "autumn"},
    {"Beach",         "beach"},
    {"Desert",        "desert"},
    {"Waterfall",     "waterfall"},
    {"Aurora",        "aurora"},
    {"Night",         "night"},
    {"Minimalism",    "minimalism"},
    {"Fog",           "fog"},
    {"Flowers",       "flowers"},
    {"Aerial",        "aerial"},
    {"Rain",          "rain"},
    {"Village",       "village"},
};
static const int CLOCK_BG_KW_COUNT = (int)(sizeof(CLOCK_BG_KEYWORDS) / sizeof(CLOCK_BG_KEYWORDS[0]));

// ── Weather widget city list ─────────────────────────────────────────────────
// lat/lon = coordinates for Open-Meteo API. lat=0/lon=0 = auto-detect via ip-api.com.
// Labels use "Region/City" prefix so the dropdown is easy to navigate.
// Sorted alphabetically by city within each region.
struct ClockCity { const char* label; float lat; float lon; };
static const ClockCity CLOCK_CITIES[] = {
    {"Auto-detect",              0.00f,   0.00f},
    // Americas — alphabetical by city
    {"Americas/Bogota",          4.71f,  -74.07f},
    {"Americas/Buenos Aires",  -34.61f,  -58.38f},
    {"Americas/Chicago",        41.85f,  -87.65f},
    {"Americas/Lima",          -12.05f,  -77.04f},
    {"Americas/Los Angeles",    34.05f, -118.24f},
    {"Americas/Mexico City",    19.43f,  -99.13f},
    {"Americas/Miami",          25.77f,  -80.19f},
    {"Americas/Montreal",       45.50f,  -73.57f},
    {"Americas/New York",       40.71f,  -74.01f},
    {"Americas/Santiago",      -33.45f,  -70.67f},
    {"Americas/Sao Paulo",     -23.55f,  -46.63f},
    {"Americas/Toronto",        43.65f,  -79.38f},
    {"Americas/Vancouver",      49.25f, -123.12f},
    // Europe — alphabetical by city
    {"Europe/Amsterdam",        52.37f,    4.90f},
    {"Europe/Athens",           37.98f,   23.73f},
    {"Europe/Berlin",           52.52f,   13.40f},
    {"Europe/Brussels",         50.85f,    4.35f},
    {"Europe/Bucharest",        44.43f,   26.10f},
    {"Europe/Budapest",         47.50f,   19.04f},
    {"Europe/Copenhagen",       55.68f,   12.57f},
    {"Europe/Dublin",           53.33f,   -6.25f},
    {"Europe/Helsinki",         60.17f,   24.94f},
    {"Europe/Istanbul",         41.01f,   28.95f},
    {"Europe/Kyiv",             50.45f,   30.52f},
    {"Europe/Lisbon",           38.72f,   -9.14f},
    {"Europe/London",           51.51f,   -0.13f},
    {"Europe/Madrid",           40.42f,   -3.70f},
    {"Europe/Moscow",           55.75f,   37.62f},
    {"Europe/Oslo",             59.91f,   10.75f},
    {"Europe/Paris",            48.86f,    2.35f},
    {"Europe/Prague",           50.09f,   14.42f},
    {"Europe/Rome",             41.89f,   12.49f},
    {"Europe/Stockholm",        59.33f,   18.07f},
    {"Europe/Vienna",           48.21f,   16.37f},
    {"Europe/Warsaw",           52.23f,   21.01f},
    {"Europe/Zurich",           47.38f,    8.54f},
    // Africa — alphabetical by city
    {"Africa/Cairo",            30.06f,   31.25f},
    {"Africa/Casablanca",       33.59f,   -7.62f},
    {"Africa/Johannesburg",    -26.20f,   28.04f},
    {"Africa/Lagos",             6.45f,    3.40f},
    {"Africa/Nairobi",          -1.29f,   36.82f},
    // Middle East — alphabetical by city
    {"Middle East/Abu Dhabi",   24.47f,   54.37f},
    {"Middle East/Doha",        25.29f,   51.53f},
    {"Middle East/Dubai",       25.20f,   55.27f},
    {"Middle East/Riyadh",      24.69f,   46.72f},
    {"Middle East/Tel Aviv",    32.07f,   34.79f},
    // Asia — alphabetical by city
    {"Asia/Bangkok",            13.75f,  100.52f},
    {"Asia/Beijing",            39.91f,  116.39f},
    {"Asia/Delhi",              28.66f,   77.23f},
    {"Asia/Dhaka",              23.72f,   90.41f},
    {"Asia/Hong Kong",          22.33f,  114.19f},
    {"Asia/Jakarta",            -6.21f,  106.85f},
    {"Asia/Karachi",            24.86f,   67.01f},
    {"Asia/Kuala Lumpur",        3.14f,  101.69f},
    {"Asia/Manila",             14.60f,  121.00f},
    {"Asia/Mumbai",             19.08f,   72.88f},
    {"Asia/Seoul",              37.57f,  127.00f},
    {"Asia/Shanghai",           31.22f,  121.46f},
    {"Asia/Singapore",           1.35f,  103.82f},
    {"Asia/Taipei",             25.05f,  121.52f},
    {"Asia/Tokyo",              35.69f,  139.69f},
    // Pacific — alphabetical by city
    {"Pacific/Auckland",       -36.87f,  174.77f},
    {"Pacific/Melbourne",      -37.82f,  144.97f},
    {"Pacific/Sydney",         -33.87f,  151.21f},
};
static const int CLOCK_CITY_COUNT = (int)(sizeof(CLOCK_CITIES) / sizeof(CLOCK_CITIES[0]));

// ============================================================================
// Clock State Machine
// ============================================================================
enum ClockState {
    CLOCK_IDLE,       // Normal operation — monitoring for trigger conditions
    CLOCK_ENTERING,   // Waiting for art/lyrics tasks to stop before showing clock
    CLOCK_ACTIVE,     // Clock screen is displayed
    CLOCK_EXITING,    // Clock dismissed — waiting for bg task to stop before cleanup
};

// ============================================================================
// Clock Settings (loaded from NVS on boot)
// ============================================================================
extern int  clock_mode;           // CLOCK_MODE_* enum value
extern int  clock_timeout_min;    // Minutes of inactivity before clock appears
extern int  clock_tz_idx;         // Index into CLOCK_ZONES[]
extern bool clock_picsum_enabled; // true = download random photo background
extern int  clock_refresh_min;    // Minutes between background photo refreshes
extern int  clock_bg_kw_idx;      // Index into CLOCK_BG_KEYWORDS[]
extern bool clock_12h;            // true = 12h AM/PM format, false = 24h
extern int  clock_style;          // CLOCK_STYLE_* — which clock face to draw
extern bool clock_weather_enabled;   // true = show weather widget
extern int  clock_weather_city_idx;  // Index into CLOCK_CITIES[], OR == CLOCK_CITY_COUNT for custom location (issue #74)
extern bool clock_wx_fahrenheit;     // true = display temps in °F

// Custom location override (issue #74) — used when clock_weather_city_idx == CLOCK_LOC_CUSTOM_IDX.
// Stored as `volatile float` (32-bit aligned = atomic single-word read on RISC-V) so the bg task
// can read them without a mutex / without going through NVS (NVS reads from a PSRAM-stacked task
// crash on the cache-disable assert — see ui_clock_screen.cpp fetchClockWeather custom branch).
// Sentinel value 0.0f = unset (lat/lon = exactly 0.0 is off the coast of Ghana — acceptable).
extern volatile float clock_custom_lat;
extern volatile float clock_custom_lon;
// Optional display-only label for the saver screen. Fixed char buffer (no String) so the bg task
// can read it without a flash op. Per-byte torn reads are display-cosmetic, not crash-relevant.
extern char clock_custom_name[64];
// Sentinel index that means "use custom_lat/lon" instead of CLOCK_CITIES[idx]
#define CLOCK_LOC_CUSTOM_IDX  CLOCK_CITY_COUNT

// Weather data — written by bg task, read by UI tick (flag guards LVGL calls)
struct ClockWxHour { int wmo; int temp; int hour; };  // hour 0-23
extern int            clock_wx_temp;          // Current temperature °C
extern int            clock_wx_humidity;      // Current humidity %
extern int            clock_wx_wind;          // Current wind speed km/h
extern int            clock_wx_wmo;           // Current WMO weather code
extern ClockWxHour    clock_wx_hourly[6];     // Next 6 hours forecast
extern char          clock_wx_city_name[64]; // Display city name (from ip-api or ClockCity label)
extern bool          clock_wx_valid;               // true = data received at least once
extern int           clock_wx_uv;                  // UV index 0–11+
extern int           clock_wx_apparent;            // Feels-like temperature (same unit as clock_wx_temp)
extern char          clock_wx_sunrise[8];          // "HH:MM\0" local sunrise time
extern char          clock_wx_sunset[8];           // "HH:MM\0" local sunset time
extern volatile bool clock_weather_updated;        // Set by bg task, cleared by tick callback
extern volatile bool clock_weather_needs_refetch;  // Set by settings; bg task re-fetches immediately

// ============================================================================
// Clock Runtime State
// ============================================================================
extern ClockState clock_state;
extern uint32_t   clock_entering_start_ms;   // When CLOCK_ENTERING began
extern uint32_t   clock_exiting_start_ms;    // When CLOCK_EXITING began
extern uint32_t   last_clock_exit_ms;        // Last time clock was dismissed

// ============================================================================
// Clock Background Task
// ============================================================================
extern TaskHandle_t clockBgTaskHandle;
extern StaticTask_t clkbgTaskTCB;
extern StackType_t* clkbg_task_stack;
extern volatile bool clock_bg_shutdown_requested;
extern volatile bool clock_bg_ready;     // New image decoded and ready to display
extern uint16_t*     clock_bg_buffer;    // PSRAM pixel buffer (800×480 RGB565)
extern lv_img_dsc_t  clock_bg_dsc;      // LVGL image descriptor pointing to buffer

// ============================================================================
// Clock Screen UI Objects (set during createClockScreen)
// ============================================================================
extern lv_obj_t* scr_clock;
extern lv_obj_t* scr_clock_settings;
extern lv_obj_t* clock_bg_img;      // Background image widget
extern lv_obj_t* clock_time_lbl;    // HH:MM:SS label
extern lv_obj_t* clock_date_lbl;    // Day, Month DD YYYY label

// ============================================================================
// Function Declarations
// ============================================================================
void createClockScreen();
void createClockSettingsScreen();
void checkClockTrigger();
void exitClockScreen();
void clockBgTask(void* param);
void clockStyleChanged();   // re-apply CLOCK_STYLE_* to the built clock screen

#endif // CLOCK_SCREEN_H
