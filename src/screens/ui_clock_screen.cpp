/**
 * Clock / Screensaver Screen
 *
 * Full-screen clock with NTP time, optional loremflickr.com photo background.
 * Activates based on user-configured inactivity or playback-state triggers.
 * All Sonos art/lyrics tasks are stopped while the clock is shown.
 *
 * Architecture:
 *  - checkClockTrigger()  : called every loop() iteration, drives state machine
 *  - clockBgTask          : FreeRTOS task that downloads + decodes Flickr JPEG
 *  - exitClockScreen()    : called from touch handler or when music resumes
 */

#include "ui_common.h"
#include "config.h"
#include "ui_network_guard.h"
#include "clock_screen.h"

LV_FONT_DECLARE(lv_font_montserrat_140);
LV_FONT_DECLARE(lv_font_weathericons_80);
LV_FONT_DECLARE(lv_font_weathericons_32);
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Undefine shared macros before JPEGDEC
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <JPEGDEC.h>

// ============================================================================
// JPEGDEC callback globals for clock background (file-scoped, not shared)
// ============================================================================
static uint16_t* clk_jpeg_dest   = nullptr;  // Points to clock_bg_buffer during decode
static int       clk_jpeg_stride = 0;        // Full image width (pixels) for row offset
static int       clock_decoded_w = 0;        // Actual pixels-written width  (set by callback, NOT getWidth())
static int       clock_decoded_h = 0;        // Actual pixels-written height

static int IRAM_ATTR clockJpegCallback(JPEGDRAW* pDraw) {
    if (!clk_jpeg_dest) return 0;

    for (int y = 0; y < pDraw->iHeight; y++) {
        int dst_y = pDraw->y + y;
        if (dst_y < 0 || dst_y >= CLOCK_BG_HEIGHT) continue;
        int dst_x = pDraw->x;
        if (dst_x < 0 || dst_x >= CLOCK_BG_WIDTH) continue;
        int copy_w = min(pDraw->iWidth, CLOCK_BG_WIDTH - dst_x);
        memcpy(
            clk_jpeg_dest + dst_y * clk_jpeg_stride + dst_x,
            pDraw->pPixels + y * pDraw->iWidth,
            (size_t)copy_w * 2
        );
        // Track the rightmost and bottommost pixel actually written.
        // This captures the true decoded region regardless of what getWidth()/getHeight() report,
        // which matters if the JPEG is partially decoded (e.g. truncated download or unsupported
        // encoding). LV_IMAGE_ALIGN_STRETCH will then scale this real region to fill the widget.
        int right  = dst_x + copy_w;
        int bottom = dst_y + 1;
        if (right  > clock_decoded_w) clock_decoded_w = right;
        if (bottom > clock_decoded_h) clock_decoded_h = bottom;
    }
    return 1;  // Always continue — never abort decode mid-image
}

// ============================================================================
// Auto-detected location cache — fetched once per clock session via ip-api.com.
// Reset when clock exits so next session re-detects (avoids stale IP location).
// ============================================================================
static float clock_auto_lat = 0.0f;
static float clock_auto_lon = 0.0f;
static bool  clock_auto_loc_valid = false;  // true once ip-api.com responded OK

// ============================================================================
// Weather overlay widgets (file-scoped — created once in createClockScreen)
// Layout:
//   Top-left card (10,10 → ~370×135): city + big temp + condition + H/W
//   Top-right icon (screen x=600, y=12): 64px condition icon + "Today" label
//   Bottom strip (0,380 → 800×100): 6-hour hourly forecast columns
// ============================================================================
static lv_obj_t* clock_wx_tl_panel   = nullptr;  // Top-left container (transparent)
static lv_obj_t* clock_wx_city_lbl   = nullptr;  // City name — montserrat_20
static lv_obj_t* clock_wx_temp_lbl   = nullptr;  // "−5°C" — montserrat_48
static lv_obj_t* clock_wx_cond_lbl   = nullptr;  // "Partly Cloudy" — montserrat_18
static lv_obj_t* clock_wx_detail_lbl = nullptr;  // "H: 45%   W: 12 km/h" — montserrat_14
static lv_obj_t* clock_wx_icon       = nullptr;  // Condition icon -- lv_font_weathericons_80
static lv_obj_t* clock_wx_bottom     = nullptr;  // Bottom 6-hour strip (800x105, y=375)
static lv_obj_t* clock_wx_fc_day[6]  = {};       // "3pm" / "15h" etc -- montserrat_14
static lv_obj_t* clock_wx_fc_icon[6] = {};       // Condition icon -- lv_font_weathericons_32
static lv_obj_t* clock_wx_fc_temp[6] = {};       // "−5°" / "3°" — montserrat_16

// ── Top-right panel: UV index + feels-like + sunrise/sunset ──────────────────
static lv_obj_t* clock_wx_tr_panel   = nullptr;  // panel at (415,10)
static lv_obj_t* clock_wx_fl_lbl     = nullptr;  // "Feels like  -3°C"
static lv_obj_t* clock_wx_uv_lbl     = nullptr;  // "UV  7  High"
static lv_obj_t* clock_wx_rise_t_lbl = nullptr;  // "Rise  06:42"
static lv_obj_t* clock_wx_set_t_lbl  = nullptr;  // "Set   19:45"

// ============================================================================
// WMO weather code → human-readable condition string
// ============================================================================
static const char* wmoCondition(int code) {
    if (code == 0)  return "Clear";
    if (code <= 2)  return "Mainly Clear";
    if (code == 3)  return "Overcast";
    if (code <= 48) return "Fog";
    if (code <= 55) return "Drizzle";
    if (code <= 67) return "Rain";
    if (code <= 77) return "Snow";
    if (code <= 82) return "Showers";
    if (code <= 86) return "Snow Showers";
    if (code >= 95) return "Thunderstorm";
    return "Unknown";
}

// ============================================================================
// Weather condition icon glyph — Weather Icons font by Erik Flowers (SIL OFL 1.1)
// Glyphs encoded as UTF-8 from the font's private-use Unicode range (U+F000–U+F0FF).
// Font: lv_font_weathericons_64, generated via lv_font_conv at 64px bpp=2.
// ============================================================================
// UTF-8 encoding of Wi-* glyphs:
#define WI_DAY_SUNNY         "\xEF\x80\x8D"   // U+F00D  wi-day-sunny
#define WI_DAY_CLOUDY_HIGH   "\xEF\x81\xBD"   // U+F07D  wi-day-cloudy-high (mainly clear)
#define WI_DAY_CLOUDY        "\xEF\x80\x82"   // U+F002  wi-day-cloudy (partly cloudy)
#define WI_CLOUDY            "\xEF\x80\x93"   // U+F013  wi-cloudy (overcast)
#define WI_FOG               "\xEF\x80\x94"   // U+F014  wi-fog
#define WI_SPRINKLE          "\xEF\x80\x9C"   // U+F01C  wi-sprinkle (drizzle)
#define WI_RAIN              "\xEF\x80\x99"   // U+F019  wi-rain
#define WI_SHOWERS           "\xEF\x80\x9A"   // U+F01A  wi-showers (heavy rain)
#define WI_SLEET             "\xEF\x82\xB5"   // U+F0B5  wi-sleet
#define WI_SNOW              "\xEF\x80\x9B"   // U+F01B  wi-snow
#define WI_SNOW_WIND         "\xEF\x81\xA4"   // U+F064  wi-snow-wind (heavy snow/blizzard)
#define WI_THUNDERSTORM      "\xEF\x80\x9E"   // U+F01E  wi-thunderstorm
#define WI_NIGHT_CLEAR       "\xEF\x80\xAE"   // U+F02E  wi-night-clear
#define WI_NIGHT_CLOUDY_HIGH "\xEF\x81\xBE"   // U+F07E  wi-night-alt-cloudy-high
#define WI_NIGHT_PARTLY      "\xEF\x82\x81"   // U+F081  wi-night-alt-partly-cloudy
#define WI_SUNRISE           "\xEF\x81\x91"   // U+F051  wi-sunrise
#define WI_SUNSET            "\xEF\x81\x92"   // U+F052  wi-sunset

// Returns true if the given hour (0–23) falls outside sunrise..sunset.
// Parses clock_wx_sunrise / clock_wx_sunset ("HH:MM") — defaults to 6h/20h if not set.
static bool isNightHour(int hour) {
    int rise_h = 6, set_h = 20;
    if (clock_wx_sunrise[0] >= '0' && clock_wx_sunrise[0] <= '2')
        rise_h = (clock_wx_sunrise[0] - '0') * 10 + (clock_wx_sunrise[1] - '0');
    if (clock_wx_sunset[0] >= '0' && clock_wx_sunset[0] <= '2')
        set_h  = (clock_wx_sunset[0]  - '0') * 10 + (clock_wx_sunset[1]  - '0');
    return (hour < rise_h || hour >= set_h);
}

static const char* wmoGlyph(int code, bool night = false) {
    if (code == 0)  return night ? WI_NIGHT_CLEAR       : WI_DAY_SUNNY;
    if (code == 1)  return night ? WI_NIGHT_CLOUDY_HIGH : WI_DAY_CLOUDY_HIGH;
    if (code == 2)  return night ? WI_NIGHT_PARTLY      : WI_DAY_CLOUDY;
    if (code == 3)  return WI_CLOUDY;
    if (code <= 48) return WI_FOG;
    if (code <= 55) return WI_SPRINKLE;
    if (code == 56 || code == 57 || code == 66 || code == 67) return WI_SLEET;
    if (code == 61 || code == 63) return WI_RAIN;
    if (code == 65)               return WI_SHOWERS;
    if (code >= 71 && code <= 77) return WI_SNOW;
    if (code == 80 || code == 81) return WI_RAIN;
    if (code == 82)               return WI_SHOWERS;
    if (code == 85)               return WI_SNOW;
    if (code == 86)               return WI_SNOW_WIND;
    if (code >= 95)               return WI_THUNDERSTORM;
    return WI_CLOUDY;
}

// ============================================================================
// UV index helpers — WHO colour scale + risk label
// ============================================================================
static lv_color_t uvColor(int uv) {
    if (uv <= 2)  return lv_color_hex(0x56CB00);   // Low       — green
    if (uv <= 5)  return lv_color_hex(0xF9C000);   // Moderate  — yellow
    if (uv <= 7)  return lv_color_hex(0xF77800);   // High      — orange
    if (uv <= 10) return lv_color_hex(0xEF3636);   // Very High — red
    return             lv_color_hex(0x9E3FF3);     // Extreme   — violet
}
static const char* uvLabel(int uv) {
    if (uv <= 2)  return "Low";
    if (uv <= 5)  return "Moderate";
    if (uv <= 7)  return "High";
    if (uv <= 10) return "Very High";
    return             "Extreme";
}

// ============================================================================
// Apply weather data to all overlay widgets (main-thread only — LVGL calls safe)
// ============================================================================
static const char* tempUnit() { return clock_wx_fahrenheit ? "\xC2\xB0""F" : "\xC2\xB0""C"; }

static const char* hourLabel(int hour) {
    static char buf[6];
    if (clock_12h) {
        int h = hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, sizeof(buf), "%d%s", h, hour < 12 ? "am" : "pm");
    } else {
        snprintf(buf, sizeof(buf), "%dh", hour);
    }
    return buf;
}

static void applyWeatherToWidgets() {
    if (!clock_wx_tl_panel) return;
    if (clock_weather_enabled && clock_wx_valid) {
        char buf[64];
        lv_label_set_text(clock_wx_city_lbl, clock_wx_city_name[0] ? clock_wx_city_name : "---");
        snprintf(buf, sizeof(buf), "%d%s", clock_wx_temp, tempUnit());
        lv_label_set_text(clock_wx_temp_lbl, buf);
        lv_label_set_text(clock_wx_cond_lbl, wmoCondition(clock_wx_wmo));
        snprintf(buf, sizeof(buf), "H: %d%%   W: %d km/h", clock_wx_humidity, clock_wx_wind);
        lv_label_set_text(clock_wx_detail_lbl, buf);
        {
            struct tm _t = {};
            bool _night = getLocalTime(&_t, 0) ? isNightHour(_t.tm_hour) : false;
            if (clock_wx_icon) lv_label_set_text(clock_wx_icon, wmoGlyph(clock_wx_wmo, _night));
        }
        for (int i = 0; i < 6; i++) {
            lv_label_set_text(clock_wx_fc_day[i],  hourLabel(clock_wx_hourly[i].hour));
            lv_label_set_text(clock_wx_fc_icon[i], wmoGlyph(clock_wx_hourly[i].wmo,
                                                             isNightHour(clock_wx_hourly[i].hour)));
            snprintf(buf, sizeof(buf), "%d%s", clock_wx_hourly[i].temp, tempUnit());
            lv_label_set_text(clock_wx_fc_temp[i], buf);
        }
        lv_obj_clear_flag(clock_wx_tl_panel, LV_OBJ_FLAG_HIDDEN);
        if (clock_wx_bottom) lv_obj_clear_flag(clock_wx_bottom, LV_OBJ_FLAG_HIDDEN);

        // ── Top-right panel: UV + feels-like + sunrise/sunset ─────────────────
        if (clock_wx_tr_panel) {
            snprintf(buf, sizeof(buf), "Feels like  %d%s", clock_wx_apparent, tempUnit());
            lv_label_set_text(clock_wx_fl_lbl, buf);

            snprintf(buf, sizeof(buf), "UV  %d  %s", clock_wx_uv, uvLabel(clock_wx_uv));
            lv_label_set_text(clock_wx_uv_lbl, buf);
            lv_obj_set_style_text_color(clock_wx_uv_lbl, uvColor(clock_wx_uv), 0);

            snprintf(buf, sizeof(buf), "Rise  %s", clock_wx_sunrise);
            lv_label_set_text(clock_wx_rise_t_lbl, buf);
            snprintf(buf, sizeof(buf), "Set   %s", clock_wx_sunset);
            lv_label_set_text(clock_wx_set_t_lbl, buf);

            lv_obj_clear_flag(clock_wx_tr_panel, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(clock_wx_tl_panel, LV_OBJ_FLAG_HIDDEN);
        if (clock_wx_bottom)    lv_obj_add_flag(clock_wx_bottom,    LV_OBJ_FLAG_HIDDEN);
        if (clock_wx_tr_panel)  lv_obj_add_flag(clock_wx_tr_panel,  LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================================================
// Clock tick — updates time, date, and weather labels (lv_timer, main thread only)
// ============================================================================
static lv_timer_t* clock_tick_timer = nullptr;

// ── StandBy clock face ──────────────────────────────────────────────────────
// Oversized digits that overlap each other, drawn in two tones taken from the
// album artwork, with the colon as a pair of soft discs. Each digit is its own
// label so it can carry its own colour and sit partly on top of its neighbour —
// the semi-transparent text is what produces the blended overlap.
LV_FONT_DECLARE(lv_font_clock_240);

// The font is PROPORTIONAL, not monospace — '0' advances 163px but '1' only 94px.
// Positioning by advance width would therefore shuffle the whole row every time a
// 1 appeared. Instead each digit gets a fixed-width cell and is centred inside it,
// so the row never moves and the digits stay optically even.
#define SB_CELL      152    // fixed cell per digit, design units
#define SB_OVERLAP    28    // how far each cell sits over the previous one
#define SB_COLON_W    56    // width of the colon column
#define SB_DOT        32    // colon disc diameter
#define SB_INK       174    // real ink height of the font (line_height in the .c)
#define SB_TOP       ((480 - SB_INK) / 2 - 22)   // vertically centred, lifted for the date

static lv_obj_t* sb_digit[4] = { nullptr, nullptr, nullptr, nullptr };
static lv_obj_t* sb_dot[2]   = { nullptr, nullptr };
static lv_obj_t* sb_date     = nullptr;
static int       sb_base_x[4] = { 0, 0, 0, 0 };   // design-space x of each cell
static int       sb_colon_x   = 0;
static bool      sb_lead_hidden = false;

// Re-centres the row on the screen. With a hidden leading cell (12-hour times
// before 10) the visible glyphs would otherwise sit half a cell right of centre,
// which is exactly what "6:25" looked like — so everything shifts left by half a
// cell whenever that slot is empty.
static void standbyReflow(bool hide_leading) {
    const int dx = hide_leading ? (SB_CELL - SB_OVERLAP) / 2 : 0;
    for (int i = 0; i < 4; i++) {
        if (sb_digit[i]) lv_obj_set_x(sb_digit[i], SX(sb_base_x[i] - dx));
    }
    for (int i = 0; i < 2; i++) {
        if (sb_dot[i]) lv_obj_set_x(sb_dot[i], SX(sb_colon_x + (SB_COLON_W - SB_DOT) / 2 - dx));
    }
}

// Two tones + a highlight, derived from the artwork's dominant colour so the
// clock matches whatever is playing. Falls back to a blue close to the reference
// when there is no artwork yet.
static void standbyColours(lv_color_t* deep, lv_color_t* light, lv_color_t* dot) {
    uint32_t c = dominant_color;
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    if (r + g + b < 24) { r = 0x0A; g = 0x4A; b = 0xA8; }   // near-black art → blue

    // Push the hue to full intensity first. dominant_color is deliberately
    // darkened to ~40% for use as a background, which on black just looked muddy —
    // normalising against the strongest channel gives a saturated, poster-like
    // colour, and the two tones are then levels of that rather than dim/dimmer.
    int mx = r; if (g > mx) mx = g; if (b > mx) mx = b;
    if (mx > 0) { r = r * 255 / mx; g = g * 255 / mx; b = b * 255 / mx; }

    auto lvl = [](int v, int pct) {
        int x = v * pct / 100;
        return (uint8_t)(x < 0 ? 0 : (x > 255 ? 255 : x));
    };
    *deep  = lv_color_make(lvl(r, 58), lvl(g, 58), lvl(b, 58));
    *light = lv_color_make(lvl(r, 100), lvl(g, 100), lvl(b, 100));
    *dot   = lv_color_make(0xEE, 0xF2, 0xF7);
}

static void buildStandbyFace(lv_obj_t* parent) {
    for (int i = 0; i < 4; i++) sb_digit[i] = nullptr;
    sb_dot[0] = sb_dot[1] = nullptr;

    // Row is laid out symmetrically about the screen centre.
    const int total = SB_CELL * 4 + SB_COLON_W - SB_OVERLAP * 4;
    int x = (800 - total) / 2;

    auto addDigit = [&](int idx, int xpos) {
        sb_digit[idx] = lv_label_create(parent);
        lv_label_set_text(sb_digit[idx], "0");
        lv_obj_set_style_text_font(sb_digit[idx], &lv_font_clock_240, 0);
        // Fixed cell + centred text: the glyph floats to the middle of its cell,
        // so a narrow '1' next to a wide '0' still reads as an even row.
        lv_obj_set_width(sb_digit[idx], SX(SB_CELL));
        lv_obj_set_style_text_align(sb_digit[idx], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(sb_digit[idx], SX(xpos), SY(SB_TOP));
        lv_obj_add_flag(sb_digit[idx], LV_OBJ_FLAG_HIDDEN);
    };

    addDigit(0, x);  sb_base_x[0] = x;                x += SB_CELL - SB_OVERLAP;
    addDigit(1, x);  sb_base_x[1] = x;                x += SB_CELL - SB_OVERLAP;
    const int colon_x = x;  sb_colon_x = x;           x += SB_COLON_W - SB_OVERLAP;
    addDigit(2, x);  sb_base_x[2] = x;                x += SB_CELL - SB_OVERLAP;
    addDigit(3, x);  sb_base_x[3] = x;

    // Dots sit at ~38% and ~80% of the ink height, which is where a colon's
    // dots fall optically. Derived from SB_INK rather than guessed offsets.
    const int dot_y[2] = { SB_TOP + (SB_INK * 38) / 100 - SB_DOT / 2,
                           SB_TOP + (SB_INK * 80) / 100 - SB_DOT / 2 };
    for (int i = 0; i < 2; i++) {
        sb_dot[i] = lv_obj_create(parent);
        lv_obj_set_size(sb_dot[i], SMIN(SB_DOT), SMIN(SB_DOT));
        lv_obj_set_pos(sb_dot[i], SX(colon_x + (SB_COLON_W - SB_DOT) / 2), SY(dot_y[i]));
        lv_obj_set_style_radius(sb_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(sb_dot[i], 0, 0);
        lv_obj_set_style_shadow_width(sb_dot[i], 0, 0);
        lv_obj_clear_flag(sb_dot[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(sb_dot[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(sb_dot[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Sits directly under the digits, not pinned to the bottom of the screen —
    // bottom-aligning left it stranded ~125px below the time with a gap between.
    sb_date = lv_label_create(parent);
    lv_label_set_text(sb_date, "");
    lv_obj_set_style_text_font(sb_date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sb_date, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(sb_date, LV_OPA_60, 0);
    lv_obj_align(sb_date, LV_ALIGN_TOP_MID, 0, SY(SB_TOP + SB_INK + 14));
    lv_obj_add_flag(sb_date, LV_OBJ_FLAG_HIDDEN);
}

// Repaints the StandBy digits from the current artwork colour.
static void standbyRecolour(void) {
    lv_color_t deep, light, dot;
    standbyColours(&deep, &light, &dot);
    for (int i = 0; i < 4; i++) {
        if (!sb_digit[i]) continue;
        lv_obj_set_style_text_color(sb_digit[i], (i & 1) ? light : deep, 0);
        // Slightly transparent so overlapping strokes blend instead of masking.
        lv_obj_set_style_text_opa(sb_digit[i], (i & 1) ? 225 : 245, 0);
    }
    for (int i = 0; i < 2; i++) {
        if (!sb_dot[i]) continue;
        lv_obj_set_style_bg_color(sb_dot[i], dot, 0);
        lv_obj_set_style_bg_opa(sb_dot[i], 210, 0);
    }
}

// Shows whichever face is selected and hides the other.
static void applyClockStyle(void) {
    const bool standby = (clock_style == CLOCK_STYLE_STANDBY);
    auto vis = [](lv_obj_t* o, bool show) {
        if (!o) return;
        if (show) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    vis(clock_time_lbl, !standby);
    vis(clock_date_lbl, !standby);
    for (int i = 0; i < 4; i++) vis(sb_digit[i], standby);
    for (int i = 0; i < 2; i++) vis(sb_dot[i],   standby);
    vis(sb_date, standby);
    if (standby) standbyRecolour();
}

void clockStyleChanged(void) {
    applyClockStyle();
}

static void clock_tick_cb(lv_timer_t* /*timer*/) {
    if (!clock_time_lbl || !clock_date_lbl) return;

    // Update weather overlay if the bg task posted new data
    if (clock_weather_updated && clock_wx_tl_panel) {
        clock_weather_updated = false;
        applyWeatherToWidgets();
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) {
        // NTP not synced yet — show dashes
        lv_label_set_text(clock_time_lbl, "--:--");
        lv_label_set_text(clock_date_lbl, "Waiting for NTP...");
        return;
    }

    char time_str[8];
    if (clock_12h) {
        strftime(time_str, sizeof(time_str), "%I:%M", &timeinfo);
        // Strip leading zero: "09:30" → "9:30"
        const char* t = (time_str[0] == '0') ? time_str + 1 : time_str;
        lv_label_set_text(clock_time_lbl, t);
    } else {
        strftime(time_str, sizeof(time_str), "%H:%M", &timeinfo);
        lv_label_set_text(clock_time_lbl, time_str);
    }

    char date_str[32];
    strftime(date_str, sizeof(date_str), "%a, %b %d", &timeinfo);
    lv_label_set_text(clock_date_lbl, date_str);

    // StandBy face: drive the four digits individually. time_str is always
    // "HH:MM" here (the 12h variant only strips a leading zero, which we want to
    // keep as a blank slot so the row doesn't shift position on the hour).
    if (clock_style == CLOCK_STYLE_STANDBY && sb_digit[0]) {
        char h1[2] = { time_str[0], 0 };
        char h2[2] = { time_str[1], 0 };
        char m1[2] = { time_str[3], 0 };
        char m2[2] = { time_str[4], 0 };
        lv_label_set_text(sb_digit[0], h1);
        lv_label_set_text(sb_digit[1], h2);
        lv_label_set_text(sb_digit[2], m1);
        lv_label_set_text(sb_digit[3], m2);

        // The font carries digits only — no space glyph — so blanking the leading
        // zero with " " drew a tofu box. Hide the whole cell instead, and shift the
        // row so the remaining glyphs stay centred on the screen.
        const bool hide_lead = (clock_12h && h1[0] == '0');
        if (hide_lead) lv_obj_add_flag(sb_digit[0], LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_remove_flag(sb_digit[0], LV_OBJ_FLAG_HIDDEN);
        if (hide_lead != sb_lead_hidden) {
            sb_lead_hidden = hide_lead;
            standbyReflow(hide_lead);
        }
        if (sb_date) lv_label_set_text(sb_date, date_str);

        // Track the artwork colour as it changes between tracks.
        static uint32_t sb_last_colour = 0xFFFFFFFF;
        if (dominant_color != sb_last_colour) {
            sb_last_colour = dominant_color;
            standbyRecolour();
        }
    }
}

// ============================================================================
// Weather fetch — Open-Meteo API (no key, JSON, temp/humidity/wind/hourly).
// Step 1 (auto-detect only): GET http://ip-api.com/json → lat/lon via plain HTTP.
// Step 2: HTTPS to api.open-meteo.com → current + next 5 hours.
// Called from clockBgTask (FreeRTOS context); writes to clock_wx_* globals.
// ============================================================================
static void fetchClockWeather() {
    if (!clock_weather_enabled) return;

    // Step 1 — resolve coordinates ─────────────────────────────────────────
    float lat = 0.0f, lon = 0.0f;

    if (clock_weather_city_idx == 0) {
        // Auto-detect: use cached result from earlier this session if available
        if (clock_auto_loc_valid) {
            lat = clock_auto_lat;
            lon = clock_auto_lon;
        } else {
            // First fetch this session — call ip-api.com (plain HTTP, no TLS)
            if (!sdioPreWait("CLKWX", 0, &clock_bg_shutdown_requested)) return;

            if (xSemaphoreTake(network_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
                Serial.println("[CLKWX] No mutex for IP-locate");
                return;
            }
            if (clock_bg_shutdown_requested) { xSemaphoreGive(network_mutex); return; }

            WiFiClient ip_client;
            HTTPClient ip_http;
            ip_http.setTimeout(8000);
            bool ip_ok = false;
            if (ip_http.begin(ip_client, "http://ip-api.com/json?fields=lat,lon,city")) {
                int code = ip_http.GET();
                if (code == HTTP_CODE_OK) {
                    DynamicJsonDocument ip_doc(512);
                    WiFiClient* s = ip_http.getStreamPtr();
                    if (s && deserializeJson(ip_doc, *s) == DeserializationError::Ok) {
                        lat = ip_doc["lat"].as<float>();
                        lon = ip_doc["lon"].as<float>();
                        if (lat != 0.0f || lon != 0.0f) {
                            ip_ok = true;
                            clock_auto_lat = lat;
                            clock_auto_lon = lon;
                            clock_auto_loc_valid = true;
                            // Store city name for display
                            const char* cn = ip_doc["city"].as<const char*>();
                            if (cn && cn[0]) {
                                strlcpy(clock_wx_city_name, cn, sizeof(clock_wx_city_name));
                            }
                        }
                    }
                } else {
                    Serial.printf("[CLKWX] IP-locate HTTP %d\n", code);
                }
                ip_http.end();
                ip_client.stop();
            }
            last_network_end_ms = millis();
            xSemaphoreGive(network_mutex);

            if (!ip_ok) { Serial.println("[CLKWX] IP location failed"); return; }
            Serial.printf("[CLKWX] IP location: %.2f, %.2f\n", lat, lon);
        }
    } else if (clock_weather_city_idx == CLOCK_LOC_CUSTOM_IDX) {
        // Custom user-supplied coordinates (issue #74).
        // Read the atomic float globals directly — DO NOT touch NVS here. clockBgTask has a
        // PSRAM stack, and NVS reads internally call spi_flash_disable_interrupts_caches_
        // and_other_cpu() which asserts esp_task_stack_is_sane_cache_disabled() on PSRAM
        // stacks → cache_utils.c:129 panic. (4-byte aligned float reads are atomic.)
        float lat_val = clock_custom_lat;
        float lon_val = clock_custom_lon;
        if (lat_val == 0.0f && lon_val == 0.0f) {
            Serial.println("[CLKWX] Custom location selected but coords are unset");
            return;
        }
        if (lat_val < -90.0f  || lat_val > 90.0f  ||
            lon_val < -180.0f || lon_val > 180.0f) {
            Serial.printf("[CLKWX] Custom coords out of range: %.4f, %.4f\n", lat_val, lon_val);
            return;
        }
        lat = lat_val;
        lon = lon_val;
        // Prefer the user-supplied display name if set; otherwise fall back to "lat, lon".
        // Reading the char buffer is byte-level torn-read-safe (display-only, no crash).
        if (clock_custom_name[0] != '\0') {
            strlcpy(clock_wx_city_name, clock_custom_name, sizeof(clock_wx_city_name));
        } else {
            snprintf(clock_wx_city_name, sizeof(clock_wx_city_name), "%.2f, %.2f", lat, lon);
        }
        Serial.printf("[CLKWX] Using custom location: %.4f, %.4f (%s)\n", lat, lon, clock_wx_city_name);
    } else {
        lat = CLOCK_CITIES[clock_weather_city_idx].lat;
        lon = CLOCK_CITIES[clock_weather_city_idx].lon;
        // Use preset city label as display name
        strlcpy(clock_wx_city_name, CLOCK_CITIES[clock_weather_city_idx].label,
                sizeof(clock_wx_city_name));
    }

    // Step 2 — fetch Open-Meteo current conditions + next 6 future hours ──────
    // Request 7 hours: first entry is the current (already-started) hour, which
    // we skip when tm_min > 0, giving 6 strictly-future hours to display.
    // temperature_unit is passed directly so the API returns values in the right
    // unit — no client-side conversion needed.
    char url[350];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.2f&longitude=%.2f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,uv_index,apparent_temperature"
        "&hourly=weather_code,temperature_2m&forecast_hours=7"
        "&daily=sunrise,sunset&forecast_days=2"
        "&timezone=auto"
        "&temperature_unit=%s",
        lat, lon, clock_wx_fahrenheit ? "fahrenheit" : "celsius");

    for (int attempt = 1; attempt <= 2 && !clock_bg_shutdown_requested; attempt++) {
        // SDIO crash-defence: general + HTTPS cooldowns before Open-Meteo HTTPS.
        // M-3: this IS an HTTPS call (WiFiClientSecure below) — pass the HTTPS-cooldown
        // flag so mbedTLS DMA teardown residue drains, matching the lyrics HTTPS path.
        if (!sdioPreWait("CLKWX", SDIO_WAIT_HTTPS_COOLDOWN, &clock_bg_shutdown_requested)) return;

        if (xSemaphoreTake(network_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            Serial.println("[CLKWX] No mutex for Open-Meteo");
            return;
        }
        if (clock_bg_shutdown_requested) { xSemaphoreGive(network_mutex); return; }

        WiFiClientSecure wx_client;
        wx_client.setInsecure();
        HTTPClient wx_http;
        wx_http.setTimeout(15000);

        bool success = false;
        if (wx_http.begin(wx_client, url)) {
            int code = wx_http.GET();
            if (code == HTTP_CODE_OK) {
                // Response with hourly + daily + UV is ~900 bytes; 3072 is generous.
                String body = wx_http.getString();
                DynamicJsonDocument doc(3072);
                auto err = deserializeJson(doc, body);
                body = String();  // Free immediately after parse
                if (err == DeserializationError::Ok) {
                    int cur_temp = (int)roundf(doc["current"]["temperature_2m"].as<float>());
                    int humidity = doc["current"]["relative_humidity_2m"].as<int>();
                    int wmo      = doc["current"]["weather_code"].as<int>();
                    int wind     = (int)roundf(doc["current"]["wind_speed_10m"].as<float>());
                    int uv_idx   = (int)roundf(doc["current"]["uv_index"].as<float>());
                    int apparent = (int)roundf(doc["current"]["apparent_temperature"].as<float>());

                    // Sunrise / sunset — daily[0] is today, format "2026-03-22T06:42"
                    char sunrise_str[8] = "--:--";
                    char sunset_str[8]  = "--:--";
                    JsonArray d_rise = doc["daily"]["sunrise"].as<JsonArray>();
                    JsonArray d_set  = doc["daily"]["sunset"].as<JsonArray>();
                    if (d_rise.size() > 0) {
                        const char* s = d_rise[0].as<const char*>();
                        if (s && strlen(s) >= 16) { strncpy(sunrise_str, s + 11, 5); sunrise_str[5] = '\0'; }
                    }
                    if (d_set.size() > 0) {
                        const char* s = d_set[0].as<const char*>();
                        if (s && strlen(s) >= 16) { strncpy(sunset_str, s + 11, 5); sunset_str[5] = '\0'; }
                    }

                    // Hourly forecast — next 6 FUTURE hours.
                    // Time format: "2025-02-24T15:00" — hour at chars [11..12].
                    // We fetched 7 entries; skip index 0 if we're already past the
                    // current hour's start (tm_min > 0), so display starts from the
                    // next full hour rather than the already-running hour.
                    JsonArray h_time = doc["hourly"]["time"].as<JsonArray>();
                    JsonArray h_wmo  = doc["hourly"]["weather_code"].as<JsonArray>();
                    JsonArray h_temp = doc["hourly"]["temperature_2m"].as<JsonArray>();
                    int arr_sz = (int)h_time.size();  // should be 7

                    struct tm ti = {};
                    getLocalTime(&ti, 100);
                    int skip = (ti.tm_min > 0) ? 1 : 0;

                    ClockWxHour hourly[6];
                    for (int i = 0; i < 6; i++) {
                        int src = i + skip;
                        const char* ts = (src < arr_sz) ? h_time[src].as<const char*>() : nullptr;
                        hourly[i].hour = ts ? atoi(ts + 11) : 0;
                        hourly[i].wmo  = (src < arr_sz) ? h_wmo[src].as<int>()                 : wmo;
                        hourly[i].temp = (src < arr_sz) ? (int)roundf(h_temp[src].as<float>()) : cur_temp;
                    }

                    // NWP model (hourly) is more responsive to active precipitation than
                    // AWS observation (current.weather_code). Use hourly[skip] — the same
                    // entry as the first strip column — so icon/text match the strip.
                    if (arr_sz > skip) wmo = h_wmo[skip].as<int>();

                    clock_wx_temp     = cur_temp;
                    clock_wx_humidity = humidity;
                    clock_wx_wind     = wind;
                    clock_wx_wmo      = wmo;
                    clock_wx_uv       = uv_idx;
                    clock_wx_apparent = apparent;
                    strncpy(clock_wx_sunrise, sunrise_str, sizeof(clock_wx_sunrise));
                    strncpy(clock_wx_sunset,  sunset_str,  sizeof(clock_wx_sunset));
                    memcpy(clock_wx_hourly, hourly, sizeof(hourly));
                    clock_wx_valid        = true;
                    clock_weather_updated = true;

                    Serial.printf("[CLKWX] %d°C feels=%d°C uv=%d rise=%s set=%s wmo=%d hum=%d%% wind=%dkm/h city=%s\n",
                                  cur_temp, apparent, uv_idx, sunrise_str, sunset_str,
                                  wmo, humidity, wind, clock_wx_city_name);
                    success = true;
                } else {
                    Serial.printf("[CLKWX] JSON error: %s (attempt %d/2)\n", err.c_str(), attempt);
                }
            } else {
                Serial.printf("[CLKWX] HTTP %d (attempt %d/2)\n", code, attempt);
            }
            wx_http.end();
            wx_client.stop();
        }
        last_https_end_ms   = millis();
        last_network_end_ms = millis();
        xSemaphoreGive(network_mutex);

        if (success) return;
        vTaskDelay(pdMS_TO_TICKS(3000));  // pause before retry (also satisfies HTTPS cooldown)
    }
}

// ============================================================================
// Background picture download + JPEG decode task (FreeRTOS, core 0)
// ============================================================================
void clockBgTask(void* /*param*/) {
    // Give the clock screen a moment to fully render before first download
    vTaskDelay(pdMS_TO_TICKS(1500));
    clock_weather_needs_refetch = true;  // ensure weather fetches on first cycle

    while (!clock_bg_shutdown_requested) {
        // --- Download loremflickr.com JPEG (plain HTTP, no TLS, up to 3 attempts) ---
        // Skipped entirely when clock_picsum_enabled is false (weather-only mode).
        // Two-step: GET loremflickr.com → parse Location redirect → fetch actual photo.
        // loremflickr serves from its own cache (/cache/resized/...) via HTTP.
        // "Random" (no keyword) hits Flickr API directly = 500 since Flickr blocked them.
        // Fix: for Random mode pick a random keyword from the list so cache is always hit.
        // picsum.photos NOT used: serves progressive JPEG, JPEGDEC only decodes baseline.
        uint8_t* dl_buf = nullptr;
        int dl_total = 0;

        if (clock_picsum_enabled) {
            // DMA guard: skip photo if DMA is depleted. SDIO TX copy buffer (transport_drv.c:290)
            // fails during HTTP SYN when session DMA loss ≥ -66KB — confirmed log16 crash3.
            // Photo is non-critical; weather fetch runs regardless.
            if (heap_caps_get_free_size(MALLOC_CAP_DMA) < CLOCK_BG_MIN_DMA) {
                Serial.printf("[CLKBG] DMA too low (%uKB < %uKB) — skipping photo this cycle\n",
                              (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
                              (unsigned)(CLOCK_BG_MIN_DMA / 1024));
            } else {
                dl_buf = (uint8_t*)heap_caps_malloc(
                    CLOCK_BG_MAX_DL_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
        }

        if (dl_buf) {
            const char* kw = CLOCK_BG_KEYWORDS[clock_bg_kw_idx].kw;

            // "Random" = empty keyword → pick a random keyword from the list (skip index 0)
            // so loremflickr always serves from its keyword cache, never hits Flickr API
            if (kw[0] == '\0' && CLOCK_BG_KW_COUNT > 1) {
                kw = CLOCK_BG_KEYWORDS[1 + (esp_random() % (CLOCK_BG_KW_COUNT - 1))].kw;
            }

            const int MAX_ATTEMPTS = 3;

            for (int attempt = 1; attempt <= MAX_ATTEMPTS && dl_total == 0 && !clock_bg_shutdown_requested; attempt++) {

                // SDIO crash-defence: general + HTTPS cooldowns before BG photo download.
                if (!sdioPreWait("CLKBG", 0, &clock_bg_shutdown_requested)) break;

                if (xSemaphoreTake(network_mutex, pdMS_TO_TICKS(8000)) != pdTRUE) {
                    Serial.printf("[CLKBG] No mutex (attempt %d)\n", attempt);
                    break;
                }
                if (clock_bg_shutdown_requested) { xSemaphoreGive(network_mutex); break; }

                if (attempt > 1) Serial.printf("[CLKBG] Retry %d/%d\n", attempt, MAX_ATTEMPTS);

                // Step 1: HTTP GET loremflickr → parse Location redirect (relative or absolute)
                char lf_url[128];
                snprintf(lf_url, sizeof(lf_url),
                         "http://loremflickr.com/%d/%d/%s?lock=%u",
                         CLOCK_BG_WIDTH, CLOCK_BG_HEIGHT, kw, esp_random() % 10000);

                String photoUrl = "";
                {
                    WiFiClient lf_client;
                    HTTPClient lf_http;
                    lf_http.setTimeout(10000);
                    lf_http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
                    const char* hdrs[] = {"Location"};
                    lf_http.collectHeaders(hdrs, 1);

                    if (lf_http.begin(lf_client, lf_url)) {
                        int code = lf_http.GET();
                        if (code == 301 || code == 302 || code == 303) {
                            photoUrl = lf_http.header("Location");
                            // Relative path → make absolute (loremflickr serves /cache/resized/...)
                            if (photoUrl.startsWith("/"))
                                photoUrl = String("http://loremflickr.com") + photoUrl;
                            // Downgrade any HTTPS → HTTP (no TLS)
                            else if (photoUrl.startsWith("https://"))
                                photoUrl.replace("https://", "http://");
                        } else {
                            Serial.printf("[CLKBG] loremflickr %d (attempt %d/%d)\n", code, attempt, MAX_ATTEMPTS);
                        }
                        lf_http.end();
                        lf_client.stop();
                    }
                }
                last_network_end_ms = millis();

                // Step 2: Fetch the actual photo over plain HTTP
                if (photoUrl.length() > 0) {
                    Serial.printf("[CLKBG] Fetching: %s\n", photoUrl.c_str());
                    WiFiClient photo_client;
                    HTTPClient photo_http;
                    photo_http.setTimeout(15000);
                    photo_http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

                    if (photo_http.begin(photo_client, photoUrl)) {
                        int code = photo_http.GET();
                        if (code == HTTP_CODE_OK) {
                            WiFiClient* stream = photo_http.getStreamPtr();
                            uint32_t t0 = millis();
                            while (stream && dl_total < CLOCK_BG_MAX_DL_SIZE && !clock_bg_shutdown_requested) {
                                if (millis() - t0 > 20000) break;
                                if (!stream->connected() && !stream->available()) break;
                                int avail = stream->available();
                                if (avail <= 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
                                int n = stream->readBytes(dl_buf + dl_total,
                                                          min(avail, CLOCK_BG_MAX_DL_SIZE - dl_total));
                                if (n > 0) dl_total += n;
                                vTaskDelay(pdMS_TO_TICKS(5));
                            }
                            Serial.printf("[CLKBG] Downloaded %d bytes\n", dl_total);
                        } else {
                            Serial.printf("[CLKBG] Photo %d (attempt %d/%d)\n", code, attempt, MAX_ATTEMPTS);
                        }
                        photo_http.end();
                        photo_client.stop();
                    }
                    last_network_end_ms      = millis();
                    last_art_download_end_ms = millis();  // gate art/lyrics inter-download cooldowns
                }

                xSemaphoreGive(network_mutex);
            }
        }

        // --- Decode JPEG into clock_bg_buffer ---
        if (dl_total > 0 && clock_bg_buffer && !clock_bg_shutdown_requested) {
            Serial.printf("[CLKBG] Decoding %d bytes JPEG\n", dl_total);

            // Allocate JPEGDEC in PSRAM (~18KB struct — too large for task stack)
            JPEGDEC* jpeg = (JPEGDEC*)heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
            if (!jpeg) {
                Serial.println("[CLKBG] Failed to alloc JPEGDEC");
            } else {
                memset(jpeg, 0, sizeof(JPEGDEC));  // Zero-init (JPEGDEC is POD; equivalent to default construction)
                clk_jpeg_dest = clock_bg_buffer;

                if (jpeg->openRAM(dl_buf, dl_total, clockJpegCallback)) {
                    Serial.printf("[CLKBG] JPEG declared: %dx%d (expected %dx%d)\n",
                                  jpeg->getWidth(), jpeg->getHeight(),
                                  CLOCK_BG_WIDTH, CLOCK_BG_HEIGHT);
                    clk_jpeg_stride = CLOCK_BG_WIDTH;  // dest buffer stride (fixed, callback clips overflow)
                    clock_decoded_w = 0;               // reset — callback accumulates actual written region
                    clock_decoded_h = 0;
                    memset(clock_bg_buffer, 0, (size_t)CLOCK_BG_WIDTH * CLOCK_BG_HEIGHT * 2);
                    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);  // Match LVGL color format
                    int rc = jpeg->decode(0, 0, 0);            // 0 = full resolution
                    jpeg->close();
                    Serial.printf("[CLKBG] Decode rc=%d, actual pixels: %dx%d\n",
                                  rc, clock_decoded_w, clock_decoded_h);
                    if (rc == 1 && clock_decoded_w > 0 && clock_decoded_h > 0) {
                        clock_bg_ready = true;
                        Serial.println("[CLKBG] Background ready");
                    } else {
                        Serial.printf("[CLKBG] JPEG decode failed or empty\n");
                    }
                } else {
                    Serial.println("[CLKBG] JPEG open failed");
                }
                clk_jpeg_dest = nullptr;

                heap_caps_free(jpeg);  // jpeg->close() already released internal buffers
            }
        }

        if (dl_buf) {
            heap_caps_free(dl_buf);
            dl_buf = nullptr;
        }

        // Fetch weather on first cycle (last_wx_fetch_ms=0 forces immediate fetch).
        // Weather refresh is decoupled from photo refresh: it uses its own
        // CLOCK_WX_REFRESH_MIN interval so it never fetches more often than needed
        // regardless of the photo refresh rate (which can be as low as 1 min).
        const uint32_t WX_REFRESH_MS = (uint32_t)CLOCK_WX_REFRESH_MIN * 60000UL;
        static uint32_t last_wx_fetch_ms = 0;

        if (clock_weather_needs_refetch || millis() - last_wx_fetch_ms >= WX_REFRESH_MS) {
            clock_weather_needs_refetch = false;
            last_wx_fetch_ms = millis();
            fetchClockWeather();
        }

        // --- Wait photo refresh interval, checking shutdown every 500ms ---
        // Weather is also checked each tick: fetches when its own interval expires
        // or when unit toggle sets clock_weather_needs_refetch.
        uint32_t wait_ms = (uint32_t)clock_refresh_min * 60000UL;
        uint32_t waited  = 0;
        while (waited < wait_ms && !clock_bg_shutdown_requested) {
            vTaskDelay(pdMS_TO_TICKS(500));
            waited += 500;
            if (clock_weather_needs_refetch || millis() - last_wx_fetch_ms >= WX_REFRESH_MS) {
                clock_weather_needs_refetch = false;
                last_wx_fetch_ms = millis();
                fetchClockWeather();
            }
        }
    }

    Serial.println("[CLKBG] Task exiting");
    clockBgTaskHandle = nullptr;
    vTaskDelete(NULL);
}

// ============================================================================
// Create the clock screen (called once at boot, like all other screens)
// ============================================================================
void createClockScreen() {
    scr_clock = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_clock, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(scr_clock, LV_OBJ_FLAG_SCROLLABLE);

    // Background image (hidden until clockBgTask fetches a photo)
    clock_bg_img = lv_img_create(scr_clock);
    lv_obj_set_size(clock_bg_img, CLOCK_BG_WIDTH, CLOCK_BG_HEIGHT);
    lv_obj_set_pos(clock_bg_img, 0, 0);
    lv_obj_set_style_img_opa(clock_bg_img, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(clock_bg_img, LV_OBJ_FLAG_CLICKABLE);
    // Stretch image to fill widget — handles any decoded size (especially when
    // the JPEG dimensions differ from CLOCK_BG_WIDTH×CLOCK_BG_HEIGHT)
    lv_image_set_align(clock_bg_img, LV_IMAGE_ALIGN_STRETCH);

    // Semi-transparent dark overlay so text is always readable over photos
    lv_obj_t* overlay = lv_obj_create(scr_clock);
    lv_obj_set_size(overlay, CLOCK_BG_WIDTH, CLOCK_BG_HEIGHT);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, 160, 0);  // ~63% dark veil
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Time label — HH:MM in native 120px Montserrat (crisp, no scaling)
    clock_time_lbl = lv_label_create(scr_clock);
    lv_label_set_text(clock_time_lbl, "--:--");
    lv_obj_set_style_text_font(clock_time_lbl, &lv_font_montserrat_140, 0);
    lv_obj_set_style_text_color(clock_time_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(clock_time_lbl, LV_ALIGN_CENTER, 0, SY(-30));

    // Date label — "Thu, May 16" small and dim below the time
    clock_date_lbl = lv_label_create(scr_clock);
    lv_label_set_text(clock_date_lbl, "");
    lv_obj_set_style_text_font(clock_date_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(clock_date_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(clock_date_lbl, LV_ALIGN_CENTER, 0, SY(90));

    buildStandbyFace(scr_clock);
    applyClockStyle();

    // ── Weather overlay — hidden until first fetch ───────────────────────────
    
    
    

    // ── Top-left area (transparent — no card, no shadow) ──────────────────────
    clock_wx_tl_panel = lv_obj_create(scr_clock);
    lv_obj_set_pos(clock_wx_tl_panel, SX(10), SY(10));
    lv_obj_set_size(clock_wx_tl_panel, SX(390), SY(160));
    lv_obj_set_style_bg_opa(clock_wx_tl_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_wx_tl_panel, 0, 0);
    lv_obj_set_style_pad_all(clock_wx_tl_panel, 0, 0);
    lv_obj_clear_flag(clock_wx_tl_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clock_wx_tl_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(clock_wx_tl_panel, LV_OBJ_FLAG_HIDDEN);

    clock_wx_city_lbl = lv_label_create(clock_wx_tl_panel);
    lv_label_set_text(clock_wx_city_lbl, "---");
    lv_obj_set_style_text_font(clock_wx_city_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(clock_wx_city_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_pos(clock_wx_city_lbl, SX(10), SY(5));

    clock_wx_temp_lbl = lv_label_create(clock_wx_tl_panel);
    lv_label_set_text(clock_wx_temp_lbl, "--°C");
    lv_obj_set_style_text_font(clock_wx_temp_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(clock_wx_temp_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_pos(clock_wx_temp_lbl, SX(10), SY(32));

    clock_wx_cond_lbl = lv_label_create(clock_wx_tl_panel);
    lv_label_set_text(clock_wx_cond_lbl, "");
    lv_obj_set_style_text_font(clock_wx_cond_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(clock_wx_cond_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_pos(clock_wx_cond_lbl, SX(10), SY(95));

    clock_wx_detail_lbl = lv_label_create(clock_wx_tl_panel);
    lv_label_set_text(clock_wx_detail_lbl, "");
    lv_obj_set_style_text_font(clock_wx_detail_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clock_wx_detail_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_pos(clock_wx_detail_lbl, SX(10), SY(118));

    // Today icon — 80px, close to the right of the temp number
    clock_wx_icon = lv_label_create(clock_wx_tl_panel);
    lv_label_set_text(clock_wx_icon, WI_DAY_SUNNY);
    lv_obj_set_style_text_font(clock_wx_icon, &lv_font_weathericons_80, 0);
    lv_obj_set_style_text_color(clock_wx_icon, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_pos(clock_wx_icon, SX(155), SY(10));
    lv_obj_clear_flag(clock_wx_icon, LV_OBJ_FLAG_CLICKABLE);

    // ── Bottom strip: 6-hour hourly forecast (no background, no separator) ─────
    clock_wx_bottom = lv_obj_create(scr_clock);
    lv_obj_set_pos(clock_wx_bottom, 0, SY(375));
    lv_obj_set_size(clock_wx_bottom, SX(800), SY(105));
    lv_obj_set_style_bg_opa(clock_wx_bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_wx_bottom, 0, 0);
    lv_obj_set_style_pad_all(clock_wx_bottom, 0, 0);
    lv_obj_clear_flag(clock_wx_bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clock_wx_bottom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(clock_wx_bottom, LV_OBJ_FLAG_HIDDEN);

    // 6 equal columns (133px each, last extends to 800)
    for (int i = 0; i < 6; i++) {
        int col_x = i * 133;
        int col_w = (i < 5) ? 133 : (800 - 5 * 133);  // last column gets remainder

        // Day name (Mon / Tue …)
        clock_wx_fc_day[i] = lv_label_create(clock_wx_bottom);
        lv_obj_set_width(clock_wx_fc_day[i], SX(col_w));
        lv_obj_set_pos(clock_wx_fc_day[i], SX(col_x), SY(7));
        lv_obj_set_style_text_align(clock_wx_fc_day[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(clock_wx_fc_day[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(clock_wx_fc_day[i], lv_color_hex(0x999999), 0);
        lv_label_set_text(clock_wx_fc_day[i], "---");

        // Condition icon (32px Weather Icons glyph)
        clock_wx_fc_icon[i] = lv_label_create(clock_wx_bottom);
        lv_obj_set_width(clock_wx_fc_icon[i], SX(col_w));
        lv_obj_set_pos(clock_wx_fc_icon[i], SX(col_x), SY(24));
        lv_obj_set_style_text_align(clock_wx_fc_icon[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(clock_wx_fc_icon[i], &lv_font_weathericons_32, 0);
        lv_obj_set_style_text_color(clock_wx_fc_icon[i], lv_color_hex(0xAAAAAA), 0);
        lv_label_set_text(clock_wx_fc_icon[i], WI_DAY_SUNNY);

        // Temperature
        clock_wx_fc_temp[i] = lv_label_create(clock_wx_bottom);
        lv_obj_set_width(clock_wx_fc_temp[i], SX(col_w));
        lv_obj_set_pos(clock_wx_fc_temp[i], SX(col_x), SY(70));
        lv_obj_set_style_text_align(clock_wx_fc_temp[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(clock_wx_fc_temp[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(clock_wx_fc_temp[i], lv_color_hex(0xAAAAAA), 0);
        lv_label_set_text(clock_wx_fc_temp[i], "--°");
    }

    // ── Top-right panel: feels-like + UV + sunrise/sunset ────────────────────
    // Right-aligned column, flush to the right edge (mirrors left panel's 10px margin).
    // Panel x=490, width=300 → right edge at 790 (10px from screen edge).
    // All labels span full panel width and are right-aligned.
    clock_wx_tr_panel = lv_obj_create(scr_clock);
    lv_obj_set_pos(clock_wx_tr_panel, SX(490), SY(10));
    lv_obj_set_size(clock_wx_tr_panel, SX(300), SY(110));
    lv_obj_set_style_bg_opa(clock_wx_tr_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_wx_tr_panel, 0, 0);
    lv_obj_set_style_pad_all(clock_wx_tr_panel, 0, 0);
    lv_obj_clear_flag(clock_wx_tr_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clock_wx_tr_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(clock_wx_tr_panel, LV_OBJ_FLAG_HIDDEN);

    // Row 0 — Feels like
    clock_wx_fl_lbl = lv_label_create(clock_wx_tr_panel);
    lv_label_set_text(clock_wx_fl_lbl, "Feels like  --");
    lv_obj_set_width(clock_wx_fl_lbl, SX(300));
    lv_obj_set_style_text_align(clock_wx_fl_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(clock_wx_fl_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(clock_wx_fl_lbl, lv_color_hex(0x999999), 0);
    lv_obj_set_pos(clock_wx_fl_lbl, 0, 0);

    // Row 1 — UV index
    clock_wx_uv_lbl = lv_label_create(clock_wx_tr_panel);
    lv_label_set_text(clock_wx_uv_lbl, "UV  --");
    lv_obj_set_width(clock_wx_uv_lbl, SX(300));
    lv_obj_set_style_text_align(clock_wx_uv_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(clock_wx_uv_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(clock_wx_uv_lbl, lv_color_hex(0x999999), 0);
    lv_obj_set_pos(clock_wx_uv_lbl, 0, SY(26));

    // Row 2 — Sunrise
    clock_wx_rise_t_lbl = lv_label_create(clock_wx_tr_panel);
    lv_label_set_text(clock_wx_rise_t_lbl, "Rise  --:--");
    lv_obj_set_width(clock_wx_rise_t_lbl, SX(300));
    lv_obj_set_style_text_align(clock_wx_rise_t_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(clock_wx_rise_t_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clock_wx_rise_t_lbl, lv_color_hex(0x777777), 0);
    lv_obj_set_pos(clock_wx_rise_t_lbl, 0, SY(56));

    // Row 3 — Sunset
    clock_wx_set_t_lbl = lv_label_create(clock_wx_tr_panel);
    lv_label_set_text(clock_wx_set_t_lbl, "Set   --:--");
    lv_obj_set_width(clock_wx_set_t_lbl, SX(300));
    lv_obj_set_style_text_align(clock_wx_set_t_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(clock_wx_set_t_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clock_wx_set_t_lbl, lv_color_hex(0x777777), 0);
    lv_obj_set_pos(clock_wx_set_t_lbl, 0, SY(78));

    // (Tap-to-dismiss: whole screen is clickable, so no separate hint needed)

    // Touch anywhere on the screen to dismiss
    lv_obj_add_flag(scr_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_clock, [](lv_event_t* /*e*/) {
        exitClockScreen();
    }, LV_EVENT_CLICKED, NULL);
}

// ============================================================================
// exitClockScreen — called from touch handler OR when music auto-resumes.
// Starts the CLOCK_EXITING state; cleanup finishes in checkClockTrigger().
// MUST be called from the main thread (LVGL context).
// ============================================================================
void exitClockScreen() {
    if (clock_state != CLOCK_ACTIVE) return;

    Serial.println("[CLOCK] Exiting clock screen");
    clock_state = CLOCK_EXITING;
    clock_exiting_start_ms = millis();

    // Stop the 1-second tick timer immediately
    if (clock_tick_timer) {
        lv_timer_delete(clock_tick_timer);
        clock_tick_timer = nullptr;
    }

    // Hide weather overlay immediately
    if (clock_wx_tl_panel) lv_obj_add_flag(clock_wx_tl_panel, LV_OBJ_FLAG_HIDDEN);
    if (clock_wx_bottom)   lv_obj_add_flag(clock_wx_bottom,   LV_OBJ_FLAG_HIDDEN);
    clock_weather_updated = false;
    // Reset auto-detect cache so next session re-checks (device may have moved)
    clock_auto_loc_valid = false;

    // Tell background task to stop
    clock_bg_shutdown_requested = true;

    // Record exit time so we don't immediately re-trigger
    last_clock_exit_ms = millis();
    resetScreenTimeout();

    // Restart art + lyrics tasks — Sonos may have a pending art URL
    art_shutdown_requested    = false;
    lyrics_shutdown_requested = false;  // Was set on clock entry; must clear on exit
    last_art_url      = "";  // Force art re-fetch — track may have changed during clock
    lyrics_last_track = "";  // Force lyrics re-fetch — display cleared during clock (issue #62)
    if (!albumArtTaskHandle) {
        createArtTask();  // PSRAM stack — frees 20KB internal SRAM for SDIO/WiFi DMA
    }

    // Transition back to main screen
    lv_screen_load_anim(scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

// ============================================================================
// checkClockTrigger — call every loop() iteration.
// Drives the clock state machine without blocking the main thread.
// ============================================================================
void checkClockTrigger() {
    switch (clock_state) {

        // ------------------------------------------------------------------
        case CLOCK_IDLE: {
            // Periodic debug: log which condition is blocking (once per 30s)
            {
                static uint32_t last_dbg_ms = 0;
                if (millis() - last_dbg_ms >= 30000) {
                    last_dbg_ms = millis();
                    uint32_t inact_ms  = (uint32_t)clock_timeout_min * 60000UL;
                    uint32_t since_exit  = millis() - last_clock_exit_ms;
                    uint32_t since_touch = millis() - last_touch_time;
                    bool trig = (clock_mode == CLOCK_MODE_INACTIVITY) ? true
                              : (clock_mode == CLOCK_MODE_PAUSED)     ? !ui_playing
                              : (clock_mode == CLOCK_MODE_NOTHING)    ? (!ui_playing && ui_title.isEmpty())
                              : false;
                    Serial.printf("[CLOCK DBG] mode=%d timeout=%dmin | exit_ok=%d(+%lus) disabled=%d inact_ok=%d(%lu/%lus) trig=%d playing=%d art_dl=%d title='%s'\n",
                        clock_mode, clock_timeout_min,
                        (since_exit >= CLOCK_EXIT_COOLDOWN_MS),  since_exit/1000,
                        (clock_mode == CLOCK_MODE_DISABLED),
                        (since_touch >= inact_ms),               since_touch/1000, inact_ms/1000,
                        trig,
                        ui_playing, (int)art_download_in_progress, ui_title.c_str());
                }
            }

            // Not re-triggering soon after a manual dismiss
            if (millis() - last_clock_exit_ms < CLOCK_EXIT_COOLDOWN_MS) return;
            if (clock_mode == CLOCK_MODE_DISABLED) return;

            uint32_t inactivity_ms = (uint32_t)clock_timeout_min * 60000UL;
            if (millis() - last_touch_time < inactivity_ms) return;

            // Track how long ui_playing has been continuously false.
            // PAUSED/NOTHING modes debounce for 2s to ignore the brief isPlaying=false
            // (~300-500ms) that Sonos reports during HLS track transitions.
            static bool   s_was_not_playing   = false;
            static uint32_t s_not_playing_since = 0;
            if (!ui_playing) {
                if (!s_was_not_playing) {
                    s_was_not_playing   = true;
                    s_not_playing_since = millis();
                }
            } else {
                s_was_not_playing   = false;
                s_not_playing_since = 0;
            }
            uint32_t not_playing_ms = s_was_not_playing ? millis() - s_not_playing_since : 0;

            // "paused and stable" = not-playing for 2s AND no art download in progress.
            // art_download_in_progress is true during every HLS track transition (set by
            // requestAlbumArt() on track change, cleared only after the art cycle completes).
            // This prevents the screensaver firing mid-transition when ui_playing is
            // briefly false but music is about to resume on the new track.
            bool paused_and_stable = (not_playing_ms >= 2000) && !art_download_in_progress;

            bool trigger = false;
            switch (clock_mode) {
                case CLOCK_MODE_INACTIVITY: trigger = true;                                           break;
                case CLOCK_MODE_PAUSED:     trigger = paused_and_stable;                              break;
                case CLOCK_MODE_NOTHING:    trigger = paused_and_stable && ui_title.isEmpty();        break;
            }
            if (!trigger) return;

            Serial.println("[CLOCK] Trigger — entering clock screen");
            clock_state            = CLOCK_ENTERING;
            clock_entering_start_ms = millis();

            // Signal art + lyrics to stop
            art_shutdown_requested    = true;
            lyrics_shutdown_requested = true;
            break;
        }

        // ------------------------------------------------------------------
        case CLOCK_ENTERING: {
            bool art_done    = (albumArtTaskHandle == nullptr);
            bool lyrics_done = (lyricsTaskHandle   == nullptr);
            bool timed_out   = (millis() - clock_entering_start_ms > CLOCK_ENTER_TIMEOUT_MS);

            // Wait until both tasks have exited, or until the timeout expires
            if ((!art_done || !lyrics_done) && !timed_out) return;

            // Apply timezone via POSIX TZ string
            setenv("TZ", CLOCK_ZONES[clock_tz_idx].posix, 1);
            tzset();

            // Pre-populate weather overlay with last known data (bg task refreshes shortly)
            clock_weather_updated = false;
            applyWeatherToWidgets();

            // Allocate and zero pixel buffer for background (800×480 RGB565 = 768 KB in PSRAM)
            if (clock_picsum_enabled && !clock_bg_buffer) {
                size_t buf_sz = (size_t)CLOCK_BG_WIDTH * CLOCK_BG_HEIGHT * 2;
                clock_bg_buffer = (uint16_t*)heap_caps_malloc(
                    buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!clock_bg_buffer) {
                    Serial.println("[CLOCK] Failed to allocate bg buffer — photo disabled");
                } else {
                    memset(clock_bg_buffer, 0, buf_sz);  // Zero so partial decodes show black, not garbage
                }
            }

            // Start background task for photo download and/or weather fetch.
            // Run if either feature is enabled (task skips photo section when picsum is off).
            if (clock_picsum_enabled || clock_weather_enabled) {
                clock_bg_shutdown_requested = false;
                clock_bg_ready              = false;
                if (clock_picsum_enabled) memset(&clock_bg_dsc, 0, sizeof(clock_bg_dsc));
                // Allocate stack in PSRAM to free 8KB of DMA SRAM for SDIO RX buffers.
                // Same pattern as art task (20KB) and lyrics task (8KB).
                // Safe: clockBgTask never calls NVS/flash write functions.
                if (!clkbg_task_stack) {
                    clkbg_task_stack = (StackType_t*)heap_caps_malloc(
                        CLOCK_BG_TASK_STACK, MALLOC_CAP_SPIRAM);
                }
                // Guard against a second create on the SAME static TCB + stack. CLOCK_EXITING
                // gives up after 2s, but a photo GET can block for 15s+, so the task can
                // still be alive when the next screensaver entry lands here. Re-initialising
                // a live TCB puts two tasks on one 8KB stack → scheduler list corruption.
                // (Every other task site in the project already guards on its handle.)
                if (clockBgTaskHandle) {
                    Serial.println("[CLOCK] BG task still running from previous session — not recreating");
                } else if (clkbg_task_stack) {
                    clockBgTaskHandle = xTaskCreateStaticPinnedToCore(
                        clockBgTask, "ClkBg",
                        CLOCK_BG_TASK_STACK / sizeof(StackType_t),
                        NULL, 1, clkbg_task_stack, &clkbgTaskTCB, 0);
                } else {
                    // PSRAM alloc failed — fall back to internal SRAM
                    xTaskCreatePinnedToCore(
                        clockBgTask, "ClkBg",
                        CLOCK_BG_TASK_STACK, NULL, 1,
                        &clockBgTaskHandle, 0);
                }
            }

            // Start 1-second clock tick
            clock_tick_timer = lv_timer_create(clock_tick_cb, 1000, NULL);
            clock_tick_cb(nullptr);  // Immediate first update

            clock_state = CLOCK_ACTIVE;
            lv_screen_load_anim(scr_clock, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
            Serial.println("[CLOCK] Clock screen active");
            break;
        }

        // ------------------------------------------------------------------
        case CLOCK_ACTIVE: {
            // Auto-dismiss when music starts playing — but NOT for CLOCK_MODE_INACTIVITY.
            // That mode shows the clock regardless of play state (dismiss by touch only).
            // For PAUSED / NOTHING modes the clock only made sense because nothing was playing,
            // so resuming playback should close it.
            if (ui_playing && clock_mode != CLOCK_MODE_INACTIVITY) {
                Serial.println("[CLOCK] Music playing — auto-exiting clock");
                exitClockScreen();
                break;
            }

            // Apply new background image when the bg task has one ready
            if (clock_bg_ready && clock_bg_buffer && clock_bg_img) {
                clock_bg_ready = false;

                memset(&clock_bg_dsc, 0, sizeof(clock_bg_dsc));
                // Use actual decoded dimensions — LV_IMAGE_ALIGN_STRETCH will scale to widget size.
                // stride is the fixed buffer row width (800px), not the decoded image width.
                clock_bg_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
                clock_bg_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
                clock_bg_dsc.header.w      = (uint32_t)(clock_decoded_w > 0 ? clock_decoded_w : CLOCK_BG_WIDTH);
                clock_bg_dsc.header.h      = (uint32_t)(clock_decoded_h > 0 ? clock_decoded_h : CLOCK_BG_HEIGHT);
                clock_bg_dsc.header.stride = CLOCK_BG_WIDTH * 2;  // bytes per row in PSRAM buffer
                clock_bg_dsc.data_size     = CLOCK_BG_WIDTH * CLOCK_BG_HEIGHT * 2;
                clock_bg_dsc.data          = (const uint8_t*)clock_bg_buffer;

                lv_img_set_src(clock_bg_img, &clock_bg_dsc);
                lv_obj_set_style_img_opa(clock_bg_img, LV_OPA_TRANSP, 0);  // Start hidden
                lv_obj_invalidate(clock_bg_img);

                // Fade in the new photo over 1.5 seconds
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, clock_bg_img);
                lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
                lv_anim_set_duration(&a, 300);
                lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
                    lv_obj_set_style_img_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
                });
                lv_anim_start(&a);
            }
            break;
        }

        // ------------------------------------------------------------------
        case CLOCK_EXITING: {
            // Wait for the background task to finish writing its buffer
            bool bg_done  = (clockBgTaskHandle == nullptr);
            bool timed_out = (millis() - clock_exiting_start_ms > 2000);
            bool bg_was_running = (clock_picsum_enabled || clock_weather_enabled);

            if (!bg_done && !timed_out && bg_was_running) return;

            // Hide the background image BEFORE freeing the buffer to prevent
            // LVGL from rendering a dangling pointer during the transition.
            if (clock_bg_img) {
                lv_obj_set_style_img_opa(clock_bg_img, LV_OPA_TRANSP, 0);
                lv_img_set_src(clock_bg_img, nullptr);
            }

            // Free the pixel buffer ONLY if the background task has actually exited.
            // On the timeout path the task is still running and may be mid-JPEG-decode:
            // it captured clock_bg_buffer before its last shutdown check and memsets +
            // decodes 768KB into it with no re-check, so freeing here was a use-after-free
            // into PSRAM. Leaking it until the task exits is the safe trade — the next
            // entry reuses the existing allocation rather than allocating a second one.
            if (clock_bg_buffer && clockBgTaskHandle == nullptr) {
                heap_caps_free(clock_bg_buffer);
                clock_bg_buffer = nullptr;
            } else if (clock_bg_buffer) {
                Serial.println("[CLOCK] BG task still alive — retaining buffer (freed on next clean exit)");
            }
            // Zero out descriptor so it no longer references freed memory
            memset(&clock_bg_dsc, 0, sizeof(clock_bg_dsc));

            clock_state = CLOCK_IDLE;
            Serial.println("[CLOCK] Exit complete");
            break;
        }
    }
}
