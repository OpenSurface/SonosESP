/**
 * Studio boot sequence (artboard 1a) — implementation for ui_boot_screen.h.
 *
 * ── Why there is not a single lv_anim in this file ──────────────────────────
 * The design specifies the whole sequence as opacity/translate tweens "LVGL can
 * run natively". It can — but not HERE. LVGL's clock is not free-running during
 * setup(): there is no tick task and no lv_tick_set_cb, so time only advances
 * where blocking code calls lv_tick_inc() by hand (main.cpp, ui_handlers.cpp and
 * sonos_discovery.cpp all do this). An lv_anim started from setup() would
 * advance ~10ms per bootScreenProgress() call, so a 400ms fade would need forty
 * of them and would land somewhere random in the middle of Sonos discovery — or,
 * on a fast boot, never start at all.
 *
 * bootFade() therefore drives its own frames: step the opacity, advance the
 * tick, run the handler, repeat. Same effect, ~180ms, and it actually runs from
 * where it is called. Once the tick is free-running (post-setup) this file could
 * go back to lv_anim, but nothing here needs it.
 *
 * The equaliser is drawn at rest rather than animated for that reason plus a
 * second one: animating it would block setup() purely for decoration while the
 * first artwork decodes. It reads as a level meter caught mid-frame.
 *
 * ── Grid, in 800x480 design space (SX/SY scale it to the panel) ─────────────
 *   margin        44 either side
 *   header row    y  38, 24 tall  (wordmark + version; no logo mark)
 *   progress      y  96, 3 tall, 712 wide
 *   check lines   y 150, 520 wide, 24 tall, 18 apart
 *   footer        y 422, left margin
 *
 * The canvas puts a level meter bottom-right. It is gone: it cannot animate here
 * (see the tick note above), so it was four static bars that conveyed nothing
 * and drew the eye away from the lines that do.
 */

#include "ui_common.h"
#include "ui_boot_screen.h"
#include "ui_fonts.h"
#include "studio_icons.h"
#include "ui_icons.h"
#include "studio.h"
#include "config.h"

// ── Grid ────────────────────────────────────────────────────────────────────
#define BT_PAD        44
#define BT_HEAD_Y     38
#define BT_PROG_Y     96
#define BT_PROG_W     (800 - BT_PAD * 2)      // 712
#define BT_CHECK_Y    150
#define BT_CHECK_W    520
#define BT_ROW_H      24
#define BT_ROW_GAP    18
#define BT_CHECKS_H   (BOOT_CHECK_COUNT * BT_ROW_H + (BOOT_CHECK_COUNT - 1) * BT_ROW_GAP)
#define BT_STATUS_Y   (BT_CHECK_Y + BT_CHECKS_H + 22)
#define BT_FOOT_Y     422

// The footer names the panel it is actually running on rather than the 4" the
// artboard was drawn at. PANEL_SIZE_LABEL comes from config.h, where it sits
// beside the rest of the per-panel constants — this file used to re-derive the
// identical string from SCREEN_SIZE, so a third variant would have had to be
// added in two places and only one of them has the #error fallback.

static lv_obj_t* bt_scr      = nullptr;
static lv_obj_t* bt_wordmark = nullptr;   // stage 1
static lv_obj_t* bt_header   = nullptr;   // stage 2 — everything else
static lv_obj_t* bt_fill     = nullptr;   // progress indicator
static lv_obj_t* bt_status   = nullptr;
static lv_obj_t* bt_row[BOOT_CHECK_COUNT]   = {};
static lv_obj_t* bt_value[BOOT_CHECK_COUNT] = {};
static bool      bt_revealed = false;
// A check can land while the wordmark is still up. Its value is held here and
// applied at reveal, so nothing is lost by showing the wordmark for longer.
static bool      bt_pending[BOOT_CHECK_COUNT] = {};

// Wall-clock at bootScreenCreate(). Screen construction turned out to be fast
// enough that revealing straight after it still flashed the wordmark past, so
// the reveal waits out the remainder of this.
static uint32_t  bt_shown_ms = 0;
#define BT_WORDMARK_MIN_MS 2200

// ── Frame pump ──────────────────────────────────────────────────────────────
// One redraw's worth of time. 15ms is the smallest step that still reads as
// motion rather than a slideshow at this fade length.
static void bootPump(uint32_t ms) {
    lv_tick_inc(ms);
    lv_timer_handler();
    lv_refr_now(NULL);
}

// Hand-rolled opacity fade — see the file header for why this is not lv_anim.
static void bootFade(lv_obj_t* obj, lv_opa_t from, lv_opa_t to, int steps) {
    if (!obj || steps < 1) return;
    for (int i = 1; i <= steps; i++) {
        lv_obj_set_style_opa(obj, (lv_opa_t)(from + (to - from) * i / steps), 0);
        bootPump(15);
    }
}

// The wordmark: "Sonos" in the text colour, "ESP" in the palette gold.
//
// TWO LABELS, not one with inline markup. LVGL's recolour markup is a per-build
// option and changes how the string is parsed; two labels in a flex row need
// nothing enabled, keep the two halves independently styleable, and cannot be
// broken by a stray '#' arriving in a future string.
//
// pad_column is 0 and both halves carry the same negative tracking, so they join
// as one word rather than reading as "Sonos ESP".
static lv_obj_t* bootWordmark(lv_obj_t* parent, const lv_font_t* font, int track) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* a = stLabel(row, font, ST_TEXT, "Sonos");
    lv_obj_set_style_text_letter_space(a, track, 0);
    lv_obj_t* b = stLabel(row, font, ST_ACCENT, "ESP");
    lv_obj_set_style_text_letter_space(b, track, 0);
    return row;
}

void bootScreenCreate(void) {
    bt_revealed = false;
    bt_shown_ms = millis();
    bt_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bt_scr, ST_BG, 0);
    lv_obj_set_style_border_width(bt_scr, 0, 0);
    lv_obj_set_style_pad_all(bt_scr, 0, 0);
    lv_obj_remove_flag(bt_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(bt_scr);

    // ── Stage 1: wordmark ───────────────────────────────────────────────────
    bt_wordmark = lv_obj_create(bt_scr);
    lv_obj_remove_style_all(bt_wordmark);
    lv_obj_set_size(bt_wordmark, SX(800), SY(120));
    lv_obj_set_pos(bt_wordmark, 0, SY(186));
    lv_obj_set_flex_flow(bt_wordmark, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bt_wordmark, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(bt_wordmark, SY(16), 0);
    lv_obj_remove_flag(bt_wordmark, LV_OBJ_FLAG_SCROLLABLE);

    bootWordmark(bt_wordmark, &font_text_48, -1);
    stCaption(bt_wordmark, ST_TEXT3, "TOUCHSCREEN SONOS CONTROLLER", 5);

    // ── Stage 2: header, progress, checks, meter, footer ────────────────────
    // Built now but held transparent, so the reveal is a fade and not a layout
    // pass at the moment a subsystem reports.
    bt_header = lv_obj_create(bt_scr);
    lv_obj_remove_style_all(bt_header);
    lv_obj_set_size(bt_header, SX(800), SY(480));
    lv_obj_set_pos(bt_header, 0, 0);
    lv_obj_set_style_opa(bt_header, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(bt_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(bt_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* head = lv_obj_create(bt_header);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, SX(400), SY(24));
    lv_obj_set_pos(head, SX(BT_PAD), SY(BT_HEAD_Y));
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, SX(12), 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    bootWordmark(head, &font_text_20, 0);
    lv_obj_t* ver = stCaption(head, ST_TEXT3, "v" FIRMWARE_VERSION, 2);
    lv_obj_set_style_pad_left(ver, SX(8), 0);

    // Progress hairline. The canvas groove is 0x221F1B — one green step from
    // ST_LINE, which RGB565 cannot resolve, so the token is used instead.
    lv_obj_t* groove = stRoundRect(bt_header, BT_PROG_W, 3, 2, ST_LINE);
    lv_obj_set_pos(groove, SX(BT_PAD), SY(BT_PROG_Y));

    bt_fill = stRoundRect(bt_header, BT_PROG_W, 3, 2, ST_ACCENT);
    lv_obj_set_pos(bt_fill, SX(BT_PAD), SY(BT_PROG_Y));
    lv_obj_set_width(bt_fill, 0);

    // ── Check lines ─────────────────────────────────────────────────────────
    static const char* kLabel[BOOT_CHECK_COUNT] = {
        "Display", "Wi-Fi", "Speakers"
    };
    for (int i = 0; i < BOOT_CHECK_COUNT; i++) {
        lv_obj_t* row = lv_obj_create(bt_header);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, SX(BT_CHECK_W), SY(BT_ROW_H));
        lv_obj_set_pos(row, SX(BT_PAD), SY(BT_CHECK_Y + i * (BT_ROW_H + BT_ROW_GAP)));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, SX(14), 0);
        lv_obj_set_style_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* chip = stRoundRect(row, 22, 22, 11, ST_ACCENT_WASH);
        lv_obj_t* tick = stLabel(chip, &font_icon_16, ST_ACCENT, ST_SC_CHECK);
        lv_obj_center(tick);

        stLabel(row, &font_text_16, ST_TEXT, kLabel[i]);

        lv_obj_t* rule = stRect(row, 2, 1, ST_LINE_SOFT);
        lv_obj_set_flex_grow(rule, 1);

        bt_value[i] = stLabel(row, &font_text_14, ST_TEXT3, "");
        bt_row[i] = row;
    }

    bt_status = stLabel(bt_header, &font_text_14, ST_ACCENT, "");
    lv_obj_set_pos(bt_status, SX(BT_PAD), SY(BT_STATUS_Y));
    lv_obj_add_flag(bt_status, LV_OBJ_FLAG_HIDDEN);

    // ── Footer ──────────────────────────────────────────────────────────────
    lv_obj_t* foot = stLabel(bt_header, &font_text_12, ST_TEXT3,
                             "ESP32-P4 · " PANEL_SIZE_LABEL " · 16 MB flash / 32 MB PSRAM");
    lv_obj_set_pos(foot, SX(BT_PAD), SY(BT_FOOT_Y));

    lv_refr_now(NULL);
}

void bootScreenReveal(void) {
    if (bt_revealed || !bt_header) return;
    bt_revealed = true;

    // Hold the wordmark for its minimum. This is dead time on a fast boot, but it
    // is dead time with the brand on screen rather than a list that has already
    // finished — and it is bounded, so a SLOW boot never waits here at all.
    while (millis() - bt_shown_ms < BT_WORDMARK_MIN_MS) bootPump(20);

    lv_obj_remove_flag(bt_header, LV_OBJ_FLAG_HIDDEN);
    // Cross-fade in one pass so the screen is never blank between the two stages.
    for (int i = 1; i <= 12; i++) {
        lv_opa_t up = (lv_opa_t)(LV_OPA_COVER * i / 12);
        lv_obj_set_style_opa(bt_header, up, 0);
        lv_obj_set_style_opa(bt_wordmark, (lv_opa_t)(LV_OPA_COVER - up), 0);
        bootPump(15);
    }
    lv_obj_add_flag(bt_wordmark, LV_OBJ_FLAG_HIDDEN);

    // Land whatever reported while the wordmark was up, in order.
    for (int i = 0; i < BOOT_CHECK_COUNT; i++) {
        if (!bt_pending[i] || !bt_row[i]) continue;
        bt_pending[i] = false;
        bootFade(bt_row[i], LV_OPA_TRANSP, LV_OPA_COVER, 6);
    }
}

void bootScreenProgress(int percent) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    // Width is scaled before the divide, so the trailing 100 is a percentage
    // divisor and not a raw pixel count. Hoisted so ui_lint.py can see that.
    const int32_t fill_w = SX(BT_PROG_W) * percent / 100;
    if (bt_fill) lv_obj_set_width(bt_fill, fill_w);
    bootPump(10);
}

void bootScreenCheck(BootCheck which, const char* value) {
    if (which < 0 || which >= BOOT_CHECK_COUNT) return;
    if (!bt_row[which]) return;

    if (value && bt_value[which]) lv_label_set_text(bt_value[which], value);

    // Before the reveal the header is still transparent, so fading a row into it
    // would be invisible work. Record it and let bootScreenReveal() land it.
    if (!bt_revealed) { bt_pending[which] = true; return; }
    bootFade(bt_row[which], LV_OPA_TRANSP, LV_OPA_COVER, 8);
}

void bootScreenStatus(const char* msg) {
    if (!bt_status) return;
    if (!msg) {
        lv_obj_add_flag(bt_status, LV_OBJ_FLAG_HIDDEN);
    } else {
        bootScreenReveal();
        lv_label_set_text(bt_status, msg);
        lv_obj_remove_flag(bt_status, LV_OBJ_FLAG_HIDDEN);
    }
    lv_refr_now(NULL);
}

void bootScreenFinish(lv_obj_t* next) {
    if (!bt_scr) return;

    // Fade the boot content down before the swap, so the hand-off reads as a
    // dissolve rather than a cut. The player is loaded first only after the fade
    // completes — loading it early would show it at full brightness underneath.
    bootFade(bt_header, LV_OPA_COVER, LV_OPA_TRANSP, 10);

    if (next) lv_screen_load(next);
    lv_obj_del(bt_scr);

    bt_scr = bt_wordmark = bt_header = bt_fill = bt_status = nullptr;
    for (int i = 0; i < BOOT_CHECK_COUNT; i++) {
        bt_row[i] = nullptr; bt_value[i] = nullptr; bt_pending[i] = false;
    }
    bt_revealed = false;
}
