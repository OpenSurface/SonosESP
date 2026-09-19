/**
 * Sleep timer button (issue #173). The rules live in the speaker; this only
 * draws what SonosController::sleepTimerRemaining() reports.
 *
 * Built like the battery badge: a small registry of live buttons and ONE timer
 * that refreshes all of them once a second, so a reading that lands between
 * frames shows up without any screen being rebuilt. The same tick refreshes the
 * sleep sheet, which is the only other thing that displays the countdown.
 */
#include "ui_sleep_button.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "amber.h"
#include "amber_battery_icons.h"   // AMB_ST_SLEEP
#include "sleep_timer.h"

// Three players exist at most once each, and only one is built at a time; the
// spare slots cost 16 bytes each and save a resize if a theme ever shows two.
#define SLEEP_BTN_MAX 4

struct SleepBtn {
    lv_obj_t* box;
    lv_obj_t* ico;
    lv_obj_t* lbl;   // null for SLEEP_BTN_ROUND
};

static SleepBtn    s_btns[SLEEP_BTN_MAX];
static lv_timer_t* s_timer = nullptr;

// All no-ops when nothing changed. LVGL invalidates on a same-value style or
// text write, and this runs every second whether or not a timer is armed.
static void setText(lv_obj_t* o, const char* t) {
    if (o && strcmp(lv_label_get_text(o), t) != 0) lv_label_set_text(o, t);
}

static void setColor(lv_obj_t* o, lv_color_t c) {
    if (!o) return;
    if (lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) return;
    lv_obj_set_style_text_color(o, c, 0);
}

static void refresh(const SleepBtn& b) {
    if (!b.box) return;
    const int  left  = sonos.sleepTimerRemaining();
    const bool armed = left > 0;

    if (b.lbl) {
        char txt[16];
        if (armed) snprintf(txt, sizeof(txt), "%d min", sleep_timer::minutesLeft(left));
        else       snprintf(txt, sizeof(txt), "Sleep");
        setText(b.lbl, txt);
    }

    // Amber is the flat theme: it has its own warm surfaces. The others sit on
    // artwork, where the header treatment is a dark disc with a faint ring.
    const bool flat = !themeUsesArtAccent();
    const lv_color_t fg     = armed ? themeAccentColor() : themeMutedColor();
    const lv_color_t border = flat ? (armed ? AMB_ACCENT_DIM : AMB_BORDER) : fg;

    setColor(b.lbl, fg);
    setColor(b.ico, fg);
    if (!lv_color_eq(lv_obj_get_style_border_color(b.box, LV_PART_MAIN), border)) {
        lv_obj_set_style_border_color(b.box, border, 0);
        if (flat) lv_obj_set_style_bg_color(b.box, armed ? AMB_ACCENT_WASH : AMB_CARD, 0);
        else      lv_obj_set_style_border_opa(b.box, armed ? LV_OPA_70 : LV_OPA_40, 0);
    }
}

static void tick(lv_timer_t*) {
    for (const SleepBtn& b : s_btns) refresh(b);
    amberRefreshSleep();   // the sheet's minutes, when it is open
}

static void onDelete(lv_event_t* e) {
    lv_obj_t* box = (lv_obj_t*)lv_event_get_target(e);
    for (SleepBtn& b : s_btns) {
        if (b.box == box) b = { nullptr, nullptr, nullptr };
    }
}

lv_obj_t* sleepButtonCreate(lv_obj_t* parent, SleepButtonStyle style) {
    const bool flat = !themeUsesArtAccent();

    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    if (flat) {
        lv_obj_set_style_bg_color(b, AMB_CARD, 0);
    } else {
        lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    }

    if (style == SLEEP_BTN_ROUND) {
        lv_obj_set_size(b, SMIN(44), SMIN(44));
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
        // Content width: "Sleep" and "45 min" are different lengths, and the
        // callers align it to an edge, so it can size to whichever it shows.
        lv_obj_set_size(b, LV_SIZE_CONTENT, SY(36));
        lv_obj_set_style_radius(b, SMIN(18), 0);
        lv_obj_set_style_pad_left(b, SX(12), 0);
        lv_obj_set_style_pad_right(b, SX(16), 0);
        lv_obj_set_style_pad_column(b, SX(6), 0);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    // Reached for in the dark, and on Immersive it sits in the bottom strip
    // where a fingertip lands least accurately.
    lv_obj_set_ext_click_area(b, SMIN(8));
    lv_obj_add_event_cb(b, [](lv_event_t*) { amberShowSleep(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(b, onDelete, LV_EVENT_DELETE, NULL);

    SleepBtn made = { b, nullptr, nullptr };
    made.ico = lv_label_create(b);
    lv_obj_set_style_text_font(made.ico, &font_batt_16, 0);
    lv_label_set_text(made.ico, AMB_ST_SLEEP);
    if (style == SLEEP_BTN_PILL) {
        made.lbl = lv_label_create(b);
        lv_obj_set_style_text_font(made.lbl, &font_text_14, 0);
        lv_label_set_text(made.lbl, "Sleep");
    }

    for (SleepBtn& slot : s_btns) {
        if (!slot.box) { slot = made; break; }
    }
    if (!s_timer) s_timer = lv_timer_create(tick, 1000, nullptr);

    refresh(made);
    return b;
}
