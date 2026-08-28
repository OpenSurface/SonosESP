/**
 * "Monolith" screensaver face — Nocturne visual system (2a in the design handoff).
 *
 * Hours stacked over minutes, flush left, with a details column to the right and
 * a 6-hour forecast rail along the bottom.
 *
 * Adapted from 1024x768 (4:3) to this project's 800x480 design space. The design
 * stacks two 320px digits — 68% of its height. Our panels are much shorter, so
 * the stack reuses the existing clock font (174px ink on 4") at a tight line
 * spacing and the rail is slimmed to fit. Hours are neutral, minutes accent, as
 * specified.
 */

#include "ui_common.h"
#include "config.h"
#include "clock_screen.h"
#include "clock_face.h"
#include "ui_fonts.h"
#include "nocturne.h"
#include <string.h>   // memset — clearing the cached widget pointers

const char* wmoCondition(int code);            // ui_clock_screen.cpp
const char* wmoGlyph(int code, bool night);    // ui_clock_screen.cpp

LV_FONT_DECLARE(lv_font_weathericons_32);
LV_FONT_DECLARE(lv_font_weathericons_64);

// Layout, 800x480 design units.
#define ML_PAD_X     46
#define ML_HEAD_Y    20
#define ML_STACK_Y   72
#define ML_LINE      (NOC_INK_MD + 8)     // full ink + gap: the lines must not touch
#define ML_COL_X     430
#define ML_RAIL_Y    396
#define ML_RULE_Y    380

static lv_obj_t* ml_root = nullptr;
static lv_obj_t* ml_city = nullptr;
static lv_obj_t* ml_date = nullptr;
static lv_obj_t* ml_hh   = nullptr;
static lv_obj_t* ml_mm   = nullptr;
static lv_obj_t* ml_icon = nullptr;
static lv_obj_t* ml_temp = nullptr;
static lv_obj_t* ml_cond = nullptr;
static lv_obj_t* ml_val[4] = {};        // Humidity / Wind / UV / Sun
static lv_obj_t* ml_rail_hr[6]   = {};
static lv_obj_t* ml_rail_icon[6] = {};
static lv_obj_t* ml_rail_tmp[6]  = {};

// Every widget here is a descendant of ml_root, so LVGL destroys them all
// with it. The cached pointers must be cleared too: the build guard keys off
// ml_root, so a stale non-null root would suppress the rebuild and then hand
// out dangling children.
static void ml_root_deleted(lv_event_t*) {
    ml_root = nullptr;
    ml_city = nullptr;
    ml_date = nullptr;
    ml_hh = nullptr;
    ml_mm = nullptr;
    ml_icon = nullptr;
    ml_temp = nullptr;
    ml_cond = nullptr;
    memset(ml_val, 0, sizeof(ml_val));
    memset(ml_rail_hr, 0, sizeof(ml_rail_hr));
    memset(ml_rail_icon, 0, sizeof(ml_rail_icon));
    memset(ml_rail_tmp, 0, sizeof(ml_rail_tmp));
}

void buildMonolithFace(lv_obj_t* parent) {
    if (ml_root) return;

    ml_root = nocFaceRoot(parent);
    lv_obj_add_event_cb(ml_root, ml_root_deleted, LV_EVENT_DELETE, nullptr);

    // ── Header ──────────────────────────────────────────────────────────────
    ml_city = nocLabel(ml_root, &font_text_14, NOC_N400, "");
    lv_obj_set_style_text_letter_space(ml_city, 2, 0);
    lv_obj_set_pos(ml_city, SX(ML_PAD_X), SY(ML_HEAD_Y));

    ml_date = nocLabel(ml_root, &font_text_14, NOC_N400, "");
    lv_obj_set_style_text_letter_space(ml_date, 2, 0);
    lv_obj_align(ml_date, LV_ALIGN_TOP_RIGHT, -SX(ML_PAD_X), SY(ML_HEAD_Y));

    // ── Stacked clock — hours neutral, minutes accent ───────────────────────
    ml_hh = nocLabel(ml_root, &NOC_FONT_MD, NOC_TEXT, "--");
    lv_obj_set_style_text_letter_space(ml_hh, -5, 0);
    lv_obj_set_pos(ml_hh, SX(ML_PAD_X - 6), SY(ML_STACK_Y));

    ml_mm = nocLabel(ml_root, &NOC_FONT_MD, NOC_ACCENT, "--");
    lv_obj_set_style_text_letter_space(ml_mm, -5, 0);
    lv_obj_set_pos(ml_mm, SX(ML_PAD_X - 6), SY(ML_STACK_Y + ML_LINE));

    // Vertical divider between the stack and the details column.
    lv_obj_t* div = lv_obj_create(ml_root);
    lv_obj_set_size(div, 1, SY(268));
    lv_obj_set_pos(div, SX(ML_COL_X - 34), SY(74));
    lv_obj_set_style_bg_color(div, NOC_N500, 0);
    lv_obj_set_style_bg_opa(div, 90, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);

    // ── Details column ──────────────────────────────────────────────────────
    ml_icon = nocLabel(ml_root, &lv_font_weathericons_64, NOC_N400, "");
    lv_obj_set_pos(ml_icon, SX(ML_COL_X), SY(66));

    ml_temp = nocLabel(ml_root, &font_text_48, NOC_TEXT, "--°");
    lv_obj_set_pos(ml_temp, SX(ML_COL_X + 84), SY(74));

    ml_cond = nocLabel(ml_root, &font_text_16, NOC_N400, "");
    lv_obj_set_pos(ml_cond, SX(ML_COL_X), SY(146));

    // 2x2 detail grid: label above value.
    static const char* kLabels[4] = { "HUMIDITY", "WIND", "UV", "SUN" };
    for (int i = 0; i < 4; i++) {
        const int cx = ML_COL_X + (i % 2) * 168;
        const int cy = 190 + (i / 2) * 74;
        lv_obj_t* k = nocLabel(ml_root, &font_text_12, NOC_N500, kLabels[i]);
        lv_obj_set_style_text_letter_space(k, 2, 0);
        lv_obj_set_pos(k, SX(cx), SY(cy));
        ml_val[i] = nocLabel(ml_root, &font_text_20, NOC_TEXT, "--");
        lv_obj_set_pos(ml_val[i], SX(cx), SY(cy + 20));
    }

    // ── Forecast rail ───────────────────────────────────────────────────────
    lv_obj_t* rule = lv_obj_create(ml_root);
    lv_obj_set_size(rule, SX(800 - ML_PAD_X * 2), 1);
    lv_obj_set_pos(rule, SX(ML_PAD_X), SY(ML_RULE_Y));
    lv_obj_set_style_bg_color(rule, NOC_ACCENT, 0);
    lv_obj_set_style_bg_opa(rule, 90, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);

    lv_obj_t* rail = lv_obj_create(ml_root);
    lv_obj_remove_style_all(rail);
    lv_obj_set_size(rail, SX(800 - ML_PAD_X * 2), SY(56));
    lv_obj_set_pos(rail, SX(ML_PAD_X), SY(ML_RAIL_Y));
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 6; i++) {
        lv_obj_t* cell = lv_obj_create(rail);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, SX(104), SY(56));
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(cell, SX(7), 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        ml_rail_hr[i]   = nocLabel(cell, &font_text_12, NOC_N500, "--");
        ml_rail_icon[i] = nocLabel(cell, &lv_font_weathericons_32, NOC_N400, "");
        ml_rail_tmp[i]  = nocLabel(cell, &font_text_16, NOC_TEXT, "--");
    }

    // Taps must reach scr_clock so exitClockScreen() can fire.
    nocMakeInert(ml_root);
}

lv_obj_t* monolithRoot(void) { return ml_root; }

void monolithTick(const struct tm* now) {
    if (!ml_root || !now) return;

    char buf[64];
    strftime(buf, sizeof(buf), clock_12h ? "%I" : "%H", now);
    lv_label_set_text(ml_hh, buf);
    strftime(buf, sizeof(buf), "%M", now);
    lv_label_set_text(ml_mm, buf);
    strftime(buf, sizeof(buf), "%A %d %B", now);
    lv_label_set_text(ml_date, buf);
    lv_label_set_text(ml_city, clock_wx_city_name[0] ? clock_wx_city_name : "");

    if (!clock_wx_valid) return;

    lv_label_set_text(ml_icon, wmoGlyph(clock_wx_wmo, false));
    lv_label_set_text_fmt(ml_temp, "%d°", clock_wx_temp);
    lv_label_set_text_fmt(ml_cond, "%s · feels like %d°",
                          wmoCondition(clock_wx_wmo), clock_wx_apparent);

    lv_label_set_text_fmt(ml_val[0], "%d%%",     clock_wx_humidity);
    lv_label_set_text_fmt(ml_val[1], "%d km/h",  clock_wx_wind);
    lv_label_set_text_fmt(ml_val[2], "%d",       clock_wx_uv);
    lv_label_set_text_fmt(ml_val[3], "%s-%s",    clock_wx_sunrise, clock_wx_sunset);

    for (int i = 0; i < 6; i++) {
        lv_label_set_text_fmt(ml_rail_hr[i], "%dh", i + 1);
        lv_label_set_text(ml_rail_icon[i], wmoGlyph(clock_wx_hourly[i].wmo, false));
        lv_label_set_text_fmt(ml_rail_tmp[i], "%d°", clock_wx_hourly[i].temp);
    }
}
