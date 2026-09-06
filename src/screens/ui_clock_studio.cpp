/**
 * "Studio" screensaver face — artboard 1b of "SonosESP Boot + Screensaver".
 *
 * Hours over minutes flush left with a seconds hairline beneath, a weather
 * column to the right, a six-hour rail along its bottom, and the paused track
 * in the corner so the panel still answers "what was playing".
 *
 * ── How this differs from Monolith, which it is a redraw of ────────────────
 *   - warm Studio palette instead of the cool Nocturne one (studio.h)
 *   - FLAT: photo_bg is false, so the photo fetch is gated off and the face
 *     paints its own ground. It is the first face to opt out; see the
 *     `noc_backdrop` flag added to ClockFaceDef for how that is honoured.
 *   - the date moves above the stack; city and condition move into the right
 *     column's header, where the canvas puts them
 *   - a seconds hairline under the minutes — the only per-second repaint here
 *   - rail cells stack vertically (hour over icon over temperature) rather than
 *     running as a row
 *   - a now-playing chip bottom left, which Monolith has no equivalent of
 *
 * ── Adapting 800x480 ───────────────────────────────────────────────────────
 * The canvas sets both digit rows at 150px with an .86 line-height, so each
 * line box is ~129px. NOC_FONT_MD's ink is 128 in design space, so the stack
 * transfers almost exactly; ST_LINE_H keeps the same "ink plus a gap" spacing
 * Monolith uses so the two rows never touch. Everything else is placed
 * absolutely in design space and scaled by SX/SY, as the rest of the project is.
 */

#include "ui_common.h"
#include "config.h"
#include "clock_screen.h"
#include "clock_face.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "nocturne.h"
#include "studio.h"
#include "studio_icons.h"
#include <string.h>
#include <ctype.h>

const char* wmoCondition(int code);            // ui_clock_screen.cpp

// ── Sky glyphs ──────────────────────────────────────────────────────────────
// This face uses the canvas's own three sky icons rather than the project's
// weather font, because the canvas draws exactly three and the point of this
// face is to match it.
//
// That is a real reduction in detail: lv_font_weathericons_* distinguishes fog,
// sleet, snow and thunder, and this collapses all of them onto rain. The other
// faces are unaffected — they still call wmoGlyph() and keep the full set.
static const char* studioSky(int wmo) {
    if (wmo <= 1)  return ST_WX_SUN;     // 0 clear, 1 mainly clear
    if (wmo <= 48) return ST_WX_CLOUD;   // 2-3 cloud, 45/48 fog
    return ST_WX_RAIN;                   // 51+ drizzle, rain, snow, storm
}

// ── Grid, 800x480 design space ──────────────────────────────────────────────
#define SD_PAD_L      40
#define SD_DATE_Y     74
#define SD_STACK_Y    104
#define SD_LINE_H     (NOC_INK_MD + 8)          // 136 — ink plus a gap
#define SD_SEC_Y      386
#define SD_SEC_W      230
#define SD_NOW_Y      420                       // now-playing chip
#define SD_NOW_W      290

#define SD_COL_X      344                       // the vertical divider
#define SD_R          378                       // right column content origin
#define SD_R_RIGHT    760
#define SD_RW         (SD_R_RIGHT - SD_R)       // 382
#define SD_HEAD_Y     34
#define SD_WX_Y       64
#define SD_GRID_Y     168
#define SD_GRID_PITCH 62
#define SD_RAIL_RULE  388
#define SD_RAIL_Y     398
#define SD_RAIL_H     74

static lv_obj_t* sd_root = nullptr;
static lv_obj_t* sd_date = nullptr;
static lv_obj_t* sd_hh   = nullptr;
static lv_obj_t* sd_mm   = nullptr;
static lv_obj_t* sd_sec  = nullptr;   // seconds hairline fill
static lv_obj_t* sd_city = nullptr;
static lv_obj_t* sd_cond = nullptr;
static lv_obj_t* sd_icon = nullptr;
static lv_obj_t* sd_temp = nullptr;
static lv_obj_t* sd_feel = nullptr;
static lv_obj_t* sd_val[4] = {};
static lv_obj_t* sd_rail_hr[6]   = {};
static lv_obj_t* sd_rail_icon[6] = {};
static lv_obj_t* sd_rail_tmp[6]  = {};
static lv_obj_t* sd_now_icon  = nullptr;
static lv_obj_t* sd_now_state = nullptr;
static lv_obj_t* sd_now_title = nullptr;

// Everything is a descendant of sd_root, so LVGL frees it all with the root.
// The cached pointers must be cleared too: the build guard keys off sd_root, so
// a stale non-null root would suppress the rebuild and hand out dangling children.
static void sd_root_deleted(lv_event_t*) {
    sd_root = nullptr;
    sd_date = sd_hh = sd_mm = sd_sec = nullptr;
    sd_city = sd_cond = sd_icon = sd_temp = sd_feel = nullptr;
    sd_now_icon = sd_now_state = sd_now_title = nullptr;
    memset(sd_val, 0, sizeof(sd_val));
    memset(sd_rail_hr, 0, sizeof(sd_rail_hr));
    memset(sd_rail_icon, 0, sizeof(sd_rail_icon));
    memset(sd_rail_tmp, 0, sizeof(sd_rail_tmp));
}

// The face's own ground. Flat warm when there is no photo; a dark scrim when
// there is, so the image reads through while the white/gold type stays legible
// on top of a bright shot. Called by applyClockStyle() on every style change, so
// toggling Photo Background takes effect without a rebuild.
void studioFaceBackdrop(lv_obj_t* root, bool over_photo) {
    if (!root) return;
    lv_obj_set_style_bg_color(root, ST_BG, 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_NONE, 0);
    // 60% is the same weight the Nocturne faces use over a photo, and was picked
    // there to survive a bright image without crushing it to black.
    lv_obj_set_style_bg_opa(root, over_photo ? 152 : LV_OPA_COVER, 0);
}

void buildStudioFace(lv_obj_t* parent) {
    if (sd_root) return;

    // Not nocFaceRoot(): that paints the Nocturne gradient, and this face is
    // specified flat. Same contract otherwise — full bleed, at the origin,
    // created hidden for applyClockStyle() to reveal.
    sd_root = lv_obj_create(parent);
    lv_obj_set_size(sd_root, SX(800), SY(480));
    lv_obj_set_pos(sd_root, 0, 0);
    studioFaceBackdrop(sd_root, false);   // applyClockStyle() re-applies with the real state
    lv_obj_set_style_border_width(sd_root, 0, 0);
    lv_obj_set_style_pad_all(sd_root, 0, 0);
    lv_obj_set_style_radius(sd_root, 0, 0);
    lv_obj_remove_flag(sd_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sd_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(sd_root, sd_root_deleted, LV_EVENT_DELETE, nullptr);

    // ── Left column: date, stacked clock, seconds ───────────────────────────
    sd_date = stCaption(sd_root, ST_TEXT3, "", 4);
    lv_obj_set_pos(sd_date, SX(SD_PAD_L), SY(SD_DATE_Y));

    sd_hh = stLabel(sd_root, &NOC_FONT_MD, ST_TEXT, "--");
    lv_obj_set_style_text_letter_space(sd_hh, -5, 0);
    lv_obj_set_pos(sd_hh, SX(SD_PAD_L - 6), SY(SD_STACK_Y));

    sd_mm = stLabel(sd_root, &NOC_FONT_MD, ST_ACCENT, "--");
    lv_obj_set_style_text_letter_space(sd_mm, -5, 0);
    lv_obj_set_pos(sd_mm, SX(SD_PAD_L - 6), SY(SD_STACK_Y + SD_LINE_H));

    // Seconds. The canvas groove is 0x221F1B, one green step from ST_LINE —
    // a difference RGB565 cannot represent — so the token stands in for it.
    lv_obj_t* sec_groove = stRoundRect(sd_root, SD_SEC_W, 3, 2, ST_LINE);
    lv_obj_set_pos(sec_groove, SX(SD_PAD_L), SY(SD_SEC_Y));
    sd_sec = stRoundRect(sd_root, SD_SEC_W, 3, 2, ST_TEXT3);
    lv_obj_set_pos(sd_sec, SX(SD_PAD_L), SY(SD_SEC_Y));
    lv_obj_set_width(sd_sec, 0);

    // ── Now-playing chip, bottom left ───────────────────────────────────────
    // Text only: the canvas shows a 34px artwork thumbnail here, but art_dsc is
    // rebuilt by the art task under art_mutex and the clock tick has no business
    // taking that lock once a second. The pause glyph plus the room carries the
    // same "what was playing" answer without reaching into decoded artwork.
    lv_obj_t* now = lv_obj_create(sd_root);
    lv_obj_remove_style_all(now);
    lv_obj_set_size(now, SX(SD_NOW_W), SY(38));
    lv_obj_set_pos(now, SX(SD_PAD_L), SY(SD_NOW_Y));
    lv_obj_remove_flag(now, LV_OBJ_FLAG_SCROLLABLE);

    // The canvas puts its pause glyph ahead of the state caption.
    sd_now_icon = stLabel(now, &font_icon_16, ST_TEXT3, "");
    lv_obj_set_pos(sd_now_icon, 0, SY(-1));

    sd_now_state = stCaption(now, ST_TEXT3, "", 3);
    lv_obj_set_pos(sd_now_state, SX(18), 0);

    sd_now_title = stLabel(now, &font_text_14, ST_TEXT2, "");
    lv_obj_set_pos(sd_now_title, 0, SY(17));
    lv_obj_set_width(sd_now_title, SX(SD_NOW_W));
    lv_label_set_long_mode(sd_now_title, LV_LABEL_LONG_DOT);

    // ── Divider ─────────────────────────────────────────────────────────────
    lv_obj_t* div = stRect(sd_root, 1, 480, ST_LINE_SOFT);
    lv_obj_set_pos(div, SX(SD_COL_X), 0);

    // ── Right column header: city / condition ───────────────────────────────
    sd_city = stCaption(sd_root, ST_TEXT3, "", 4);
    lv_obj_set_pos(sd_city, SX(SD_R), SY(SD_HEAD_Y));

    sd_cond = stCaption(sd_root, ST_TEXT3, "", 4);
    lv_obj_set_width(sd_cond, SX(SD_RW));
    lv_obj_set_style_text_align(sd_cond, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(sd_cond, SX(SD_R), SY(SD_HEAD_Y));

    // ── Current conditions ──────────────────────────────────────────────────
    // A flex row: "feels like" has to sit after a temperature whose width
    // changes with the value, so the row measures rather than guesses.
    lv_obj_t* wx = lv_obj_create(sd_root);
    lv_obj_remove_style_all(wx);
    lv_obj_set_size(wx, SX(SD_RW), SY(76));
    lv_obj_set_pos(wx, SX(SD_R), SY(SD_WX_Y));
    lv_obj_set_flex_flow(wx, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wx, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(wx, SX(16), 0);
    lv_obj_remove_flag(wx, LV_OBJ_FLAG_SCROLLABLE);

    sd_icon = stLabel(wx, &font_icon_wx_64, ST_TEXT2, "");
    sd_temp = stLabel(wx, &font_text_48, ST_TEXT, "--°");
    sd_feel = stLabel(wx, &font_text_14, ST_TEXT3, "");
    lv_obj_set_style_pad_bottom(sd_feel, SY(8), 0);

    // ── 2x2 detail grid ─────────────────────────────────────────────────────
    static const char* kLabels[4] = { "HUMIDITY", "WIND", "UV INDEX", "SUN" };
    for (int i = 0; i < 4; i++) {
        const int cx = SD_R + (i % 2) * (SD_RW / 2);
        const int cy = SD_GRID_Y + (i / 2) * SD_GRID_PITCH;
        lv_obj_t* k = stCaption(sd_root, ST_TEXT3, kLabels[i], 3);
        lv_obj_set_pos(k, SX(cx), SY(cy));
        sd_val[i] = stLabel(sd_root, &font_text_20, ST_TEXT, "--");
        lv_obj_set_pos(sd_val[i], SX(cx), SY(cy + 20));
    }

    // ── Six-hour rail ───────────────────────────────────────────────────────
    lv_obj_t* rule = stRect(sd_root, SD_RW, 1, ST_LINE_SOFT);
    lv_obj_set_pos(rule, SX(SD_R), SY(SD_RAIL_RULE));

    lv_obj_t* rail = lv_obj_create(sd_root);
    lv_obj_remove_style_all(rail);
    lv_obj_set_size(rail, SX(SD_RW), SY(SD_RAIL_H));
    lv_obj_set_pos(rail, SX(SD_R), SY(SD_RAIL_Y));
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 6; i++) {
        lv_obj_t* cell = lv_obj_create(rail);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, SX(SD_RW / 6), SY(SD_RAIL_H));
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, SY(5), 0);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        sd_rail_hr[i]   = stCaption(cell, ST_TEXT3, "--", 2);
        sd_rail_icon[i] = stLabel(cell, &font_icon_wx_32, ST_TEXT2, "");
        sd_rail_tmp[i]  = stLabel(cell, &font_text_14, ST_TEXT, "--");
    }

    // Taps must reach scr_clock so exitClockScreen() can fire.
    nocMakeInert(sd_root);
}

lv_obj_t* studioFaceRoot(void) { return sd_root; }

void studioFaceTick(const struct tm* now) {
    if (!sd_root || !now) return;

    char buf[64];
    strftime(buf, sizeof(buf), clock_12h ? "%I" : "%H", now);
    lv_label_set_text(sd_hh, buf);
    strftime(buf, sizeof(buf), "%M", now);
    lv_label_set_text(sd_mm, buf);

    // "FRIDAY 5 SEPTEMBER" — the canvas sets this uppercase and tracked. %-d is
    // not portable to newlib, so the day is formatted separately and the month
    // name uppercased in place.
    strftime(buf, sizeof(buf), "%A %d %B", now);
    for (char* c = buf; *c; c++) *c = (char)toupper((unsigned char)*c);
    lv_label_set_text(sd_date, buf);

    // Seconds hairline. 0..59 mapped across the full width, so it reads as
    // filling over the minute rather than snapping back one step early.
    const int32_t sec_w = SX(SD_SEC_W) * (now->tm_sec + 1) / 60;
    lv_obj_set_width(sd_sec, sec_w);

    // ── Now playing ─────────────────────────────────────────────────────────
    SonosDevice* dev = sonos.getCurrentDevice();
    if (dev && dev->currentTrack.length()) {
        snprintf(buf, sizeof(buf), "%s · %s",
                 dev->isPlaying ? "PLAYING" : "PAUSED",
                 dev->roomName.length() ? dev->roomName.c_str() : "SONOS");
        for (char* c = buf; *c; c++) *c = (char)toupper((unsigned char)*c);
        lv_label_set_text(sd_now_icon, dev->isPlaying ? ST_IC_PLAY : ST_IC_PAUSE);
        lv_label_set_text(sd_now_state, buf);
        lv_label_set_text(sd_now_title, dev->currentTrack.c_str());
    } else {
        lv_label_set_text(sd_now_icon, "");
        lv_label_set_text(sd_now_state, "");
        lv_label_set_text(sd_now_title, "");
    }

    // ── Weather ─────────────────────────────────────────────────────────────
    lv_label_set_text(sd_city, clock_wx_city_name[0] ? clock_wx_city_name : "");
    if (!clock_wx_valid) return;

    // The condition reads as a tracked caption here, so it is uppercased to
    // match the canvas rather than carrying wmoCondition()'s sentence case.
    snprintf(buf, sizeof(buf), "%s", wmoCondition(clock_wx_wmo));
    for (char* c = buf; *c; c++) *c = (char)toupper((unsigned char)*c);
    lv_label_set_text(sd_cond, buf);

    lv_label_set_text(sd_icon, studioSky(clock_wx_wmo));
    lv_label_set_text_fmt(sd_temp, "%d°", clock_wx_temp);
    lv_label_set_text_fmt(sd_feel, "feels %d°", clock_wx_apparent);

    lv_label_set_text_fmt(sd_val[0], "%d%%",    clock_wx_humidity);
    lv_label_set_text_fmt(sd_val[1], "%d km/h", clock_wx_wind);
    lv_label_set_text_fmt(sd_val[2], "%d",      clock_wx_uv);
    lv_label_set_text_fmt(sd_val[3], "%s - %s", clock_wx_sunrise, clock_wx_sunset);

    for (int i = 0; i < 6; i++) {
        lv_label_set_text_fmt(sd_rail_hr[i], "%dH", i + 1);
        lv_label_set_text(sd_rail_icon[i], studioSky(clock_wx_hourly[i].wmo));
        lv_label_set_text_fmt(sd_rail_tmp[i], "%d°", clock_wx_hourly[i].temp);
    }
}
