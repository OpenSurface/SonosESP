/**
 * "Horizon" screensaver face — Nocturne visual system (2c in the design handoff).
 *
 * Centred clock over an ambient accent glow rising from the bottom edge, a
 * one-line weather summary, and the 6-hour forecast as pill chips.
 *
 * Adapted from a 1024x768 (4:3) design to this project's 800x480 design space,
 * which is much wider and shorter. The clock is NOT scaled down to the design's
 * proportion — it reuses the existing clock font the rest of the UI already
 * ships (174px ink on 4", 218px on 7") — so the surrounding elements are
 * tightened instead. Two deliberate losses vs the spec, both LVGL limits:
 *   - no text-shadow on labels, so the clock's 90px accent glow is approximated
 *     by the backdrop gradient rather than a halo around the glyphs;
 *   - the radial "rising sun" glow becomes a vertical linear gradient.
 */

#include "ui_common.h"
#include "config.h"
#include "clock_screen.h"
#include "clock_face.h"
#include "ui_fonts.h"

// Nocturne tokens (nocturne-styles.css)
#define NOC_BG        lv_color_hex(0x161826)
#define NOC_TEXT      lv_color_hex(0xE9E9ED)
#define NOC_ACCENT    lv_color_hex(0x9184D9)
#define NOC_ACCENT_9  lv_color_hex(0x2A2545)   // deep accent for the horizon wash
#define NOC_N400      lv_color_hex(0x9A9AA6)
#define NOC_N500      lv_color_hex(0x7C7C8A)

// The clock font tier — same one StandBy uses (see ui_clock_screen.cpp).
#if defined(SCREEN_SIZE) && SCREEN_SIZE == 7
    LV_FONT_DECLARE(lv_font_clock_300);
    #define HZ_FONT  lv_font_clock_300
    #define HZ_INK   218
#else
    LV_FONT_DECLARE(lv_font_clock_240);
    #define HZ_FONT  lv_font_clock_240
    #define HZ_INK   174
#endif

const char* wmoCondition(int code);            // ui_clock_screen.cpp
const char* wmoGlyph(int code, bool night);    // ui_clock_screen.cpp

LV_FONT_DECLARE(lv_font_weathericons_32);
LV_FONT_DECLARE(lv_font_weathericons_64);

// Layout, 800x480 design units.
#define HZ_PAD_X       44
#define HZ_HEAD_Y      22
#define HZ_CLOCK_Y     ((480 - HZ_INK) / 2 - 74)   // lifted: details + chips sit below
#define HZ_DETAIL_Y    (HZ_CLOCK_Y + HZ_INK + 12)
#define HZ_CHIP_Y      396
#define HZ_CHIP_H      58
#define HZ_HORIZON_Y   360

static lv_obj_t* hz_root    = nullptr;
static lv_obj_t* hz_city    = nullptr;
static lv_obj_t* hz_date    = nullptr;
static lv_obj_t* hz_time    = nullptr;
static lv_obj_t* hz_icon    = nullptr;
static lv_obj_t* hz_temp    = nullptr;
static lv_obj_t* hz_cond    = nullptr;
static lv_obj_t* hz_detail  = nullptr;
static lv_obj_t* hz_chip_hr[6]   = {};
static lv_obj_t* hz_chip_icon[6] = {};
static lv_obj_t* hz_chip_tmp[6]  = {};

static lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t col,
                         const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

void buildHorizonFace(lv_obj_t* parent) {
    if (hz_root) return;                 // built once, then shown/hidden

    hz_root = lv_obj_create(parent);
    lv_obj_set_size(hz_root, SX(800), SY(480));
    lv_obj_set_pos(hz_root, 0, 0);
    lv_obj_set_style_border_width(hz_root, 0, 0);
    lv_obj_set_style_pad_all(hz_root, 0, 0);
    lv_obj_set_style_radius(hz_root, 0, 0);
    lv_obj_clear_flag(hz_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hz_root, LV_OBJ_FLAG_HIDDEN);

    // Backdrop: flat Nocturne base with the accent wash rising from the bottom.
    // A vertical linear gradient stands in for the design's radial glow — LVGL
    // has no radial gradient in the RGB565 render path we build with.
    lv_obj_set_style_bg_color(hz_root, NOC_BG, 0);
    lv_obj_set_style_bg_grad_color(hz_root, NOC_ACCENT_9, 0);
    lv_obj_set_style_bg_grad_dir(hz_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(hz_root, LV_OPA_COVER, 0);

    // Horizon rule — 1px accent line the glow sits under.
    lv_obj_t* rule = lv_obj_create(hz_root);
    lv_obj_set_size(rule, SX(800 - HZ_PAD_X * 2), 1);
    lv_obj_set_pos(rule, SX(HZ_PAD_X), SY(HZ_HORIZON_Y));
    lv_obj_set_style_bg_color(rule, NOC_ACCENT, 0);
    lv_obj_set_style_bg_opa(rule, 110, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);

    // ── Header ──────────────────────────────────────────────────────────────
    hz_city = mkLabel(hz_root, &font_text_14, NOC_N400, "");
    lv_obj_set_style_text_letter_space(hz_city, 2, 0);
    lv_obj_set_pos(hz_city, SX(HZ_PAD_X), SY(HZ_HEAD_Y));

    hz_date = mkLabel(hz_root, &font_text_14, NOC_N400, "");
    lv_obj_set_style_text_letter_space(hz_date, 2, 0);
    lv_obj_align(hz_date, LV_ALIGN_TOP_RIGHT, -SX(HZ_PAD_X), SY(HZ_HEAD_Y));

    // ── Clock ───────────────────────────────────────────────────────────────
    hz_time = mkLabel(hz_root, &HZ_FONT, NOC_TEXT, "--:--");
    lv_obj_set_style_text_letter_space(hz_time, -6, 0);
    lv_obj_set_y(hz_time, SY(HZ_CLOCK_Y));
    lv_obj_set_style_align(hz_time, LV_ALIGN_TOP_MID, 0);

    // ── One-line weather summary ────────────────────────────────────────────
    lv_obj_t* row = lv_obj_create(hz_root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, SX(800), SY(46));
    lv_obj_set_pos(row, 0, SY(HZ_DETAIL_Y));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, SX(12), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    hz_icon   = mkLabel(row, &lv_font_weathericons_32, NOC_ACCENT, "");
    hz_temp   = mkLabel(row, &font_text_24, NOC_TEXT, "--°");
    hz_cond   = mkLabel(row, &font_text_16, NOC_N400, "");
    hz_detail = mkLabel(row, &font_text_14, NOC_N500, "");

    // ── Forecast pill chips ─────────────────────────────────────────────────
    lv_obj_t* chips = lv_obj_create(hz_root);
    lv_obj_remove_style_all(chips);
    lv_obj_set_size(chips, SX(800 - HZ_PAD_X * 2), SY(HZ_CHIP_H));
    lv_obj_set_pos(chips, SX(HZ_PAD_X), SY(HZ_CHIP_Y));
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chips, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 6; i++) {
        lv_obj_t* chip = lv_obj_create(chips);
        lv_obj_set_size(chip, SX(112), SY(HZ_CHIP_H));
        lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(chip, NOC_ACCENT_9, 0);
        lv_obj_set_style_bg_opa(chip, 130, 0);
        lv_obj_set_style_border_color(chip, NOC_ACCENT, 0);
        lv_obj_set_style_border_opa(chip, 100, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(chip, SX(6), 0);

        hz_chip_hr[i]   = mkLabel(chip, &font_text_12, NOC_N500, "--");
        hz_chip_icon[i] = mkLabel(chip, &lv_font_weathericons_32, NOC_N400, "");
        hz_chip_tmp[i]  = mkLabel(chip, &font_text_14, NOC_TEXT, "--");
    }
}

// Shown/hidden by applyClockStyle().
void horizonSetVisible(bool show) {
    if (!hz_root) return;
    if (show) lv_obj_remove_flag(hz_root, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(hz_root, LV_OBJ_FLAG_HIDDEN);
}

void horizonTick(const struct tm* now) {
    if (!hz_root || !now) return;

    char buf[96];
    strftime(buf, sizeof(buf), clock_12h ? "%I:%M" : "%H:%M", now);
    // Strip the leading zero in 12h so the row stays optically centred.
    const char* t = (clock_12h && buf[0] == '0') ? buf + 1 : buf;
    lv_label_set_text(hz_time, t);

    strftime(buf, sizeof(buf), "%A %d %B", now);
    lv_label_set_text(hz_date, buf);

    lv_label_set_text(hz_city, clock_wx_city_name[0] ? clock_wx_city_name : "");

    if (!clock_wx_valid) return;

    const char* unit = clock_wx_fahrenheit ? "F" : "C";
    lv_label_set_text(hz_icon, wmoGlyph(clock_wx_wmo, false));
    lv_label_set_text_fmt(hz_temp, "%d°", clock_wx_temp);
    lv_label_set_text(hz_cond, wmoCondition(clock_wx_wmo));
    lv_label_set_text_fmt(hz_detail, "Feels %d° · H %d%% · W %d km/h · UV %d · %s / %s",
                          clock_wx_apparent, clock_wx_humidity, clock_wx_wind,
                          clock_wx_uv, clock_wx_sunrise, clock_wx_sunset);
    (void)unit;

    for (int i = 0; i < 6; i++) {
        lv_label_set_text_fmt(hz_chip_hr[i], "%dh", i + 1);
        lv_label_set_text(hz_chip_icon[i], wmoGlyph(clock_wx_hourly[i].wmo, false));
        lv_label_set_text_fmt(hz_chip_tmp[i], "%d°", clock_wx_hourly[i].temp);
    }
}
