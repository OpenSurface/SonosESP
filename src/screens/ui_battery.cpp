/**
 * Battery badge (issue #165). The rules live in include/battery.h; this file
 * only draws them.
 */
#include "ui_battery.h"
#include "battery.h"
#include "config.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "amber.h"
#include "amber_battery_icons.h"

// Room for every row of the Speakers screen and the Rooms overlay at once. A
// badge past this still draws; it just is not refreshed live.
#define BADGE_MAX 64

static lv_obj_t*   s_badges[BADGE_MAX];
static lv_timer_t* s_timer = nullptr;

static void blinkExec(void* obj, int32_t v) {
    lv_obj_set_style_text_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

// text_opa rather than opa: no layer to allocate and blend, so a blinking badge
// costs the same to draw as a still one.
static void setBlink(lv_obj_t* b, bool on) {
    const bool running = lv_anim_get(b, blinkExec) != nullptr;
    if (on == running) return;
    if (!on) {
        lv_anim_delete(b, blinkExec);
        lv_obj_set_style_text_opa(b, LV_OPA_COVER, 0);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_exec_cb(&a, blinkExec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&a, BATTERY_BLINK_MS);
    lv_anim_set_reverse_duration(&a, BATTERY_BLINK_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static const char* glyphText(battery::Glyph g) {
    switch (g) {
        case battery::GLYPH_FULL:     return AMB_BAT_FULL;
        case battery::GLYPH_MEDIUM:   return AMB_BAT_MEDIUM;
        case battery::GLYPH_LOW:      return AMB_BAT_LOW;
        case battery::GLYPH_WARNING:  return AMB_BAT_WARNING;
        case battery::GLYPH_CHARGING: return AMB_BAT_CHARGING;
        default:                      return AMB_BAT_EMPTY;
    }
}

// Both are no-ops when nothing changed. The timer runs refresh() on every badge
// every second, and LVGL invalidates on a HIDDEN-flag removal or a style write
// even when the value is the same - so without these checks a visible badge
// would be redrawn once a second for nothing.
static void setHidden(lv_obj_t* b, bool hidden) {
    if (lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
}

static void setColor(lv_obj_t* b, lv_color_t c) {
    if (lv_color_eq(lv_obj_get_style_text_color(b, LV_PART_MAIN), c)) return;
    lv_obj_set_style_text_color(b, c, 0);
}

static void refresh(lv_obj_t* b) {
    const int idx = (int)(intptr_t)lv_obj_get_user_data(b);
    const battery::View v = batteryViewFor(sonos.getDevice(idx));
    if (!v.present) {
        setBlink(b, false);
        setHidden(b, true);
        return;
    }
    setHidden(b, false);

    char txt[24];
    const char* g = glyphText(battery::glyphFor(v));
    if (v.stale || v.level < 0) snprintf(txt, sizeof(txt), "%s --", g);
    else                        snprintf(txt, sizeof(txt), "%s %d%%", g, v.level);
    if (strcmp(lv_label_get_text(b), txt) != 0) lv_label_set_text(b, txt);

    // Gold for low, as the restart list uses it: Amber has no red, and gold is
    // already how this UI says "look here". Faint when the number is stale.
    const bool warn = battery::warn(v);
    setColor(b, v.stale ? AMB_FAINT : (warn ? AMB_ACCENT : AMB_TEXT3));
    setBlink(b, warn);
}

static void tick(lv_timer_t*) {
    batterySimTick();
    for (lv_obj_t* b : s_badges) {
        if (b) refresh(b);
    }
}

static void onDelete(lv_event_t* e) {
    lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
    for (lv_obj_t*& slot : s_badges) {
        if (slot == b) slot = nullptr;
    }
}

lv_obj_t* batteryBadgeCreate(lv_obj_t* parent, int deviceIndex) {
    lv_obj_t* b = lv_label_create(parent);
    lv_obj_set_style_text_font(b, &font_batt_16, 0);
    lv_obj_set_style_text_color(b, AMB_TEXT3, 0);
    lv_label_set_text(b, "");
    lv_obj_set_user_data(b, (void*)(intptr_t)deviceIndex);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);

    for (lv_obj_t*& slot : s_badges) {
        if (!slot) { slot = b; break; }
    }
    lv_obj_add_event_cb(b, onDelete, LV_EVENT_DELETE, nullptr);
    if (!s_timer) s_timer = lv_timer_create(tick, 1000, nullptr);

    refresh(b);
    return b;
}
