/**
 * Battery badge (issue #165). The rules live in include/battery.h; this file
 * only draws them.
 *
 * A badge is a row of two labels - the glyph, then the number - centred on the
 * cross axis. It used to be one label, glyph plus text, and then the digits
 * were placed by the icon font's metrics and sat a few pixels below the middle
 * of the battery. As two flex children each is centred in its own box, and the
 * battery body is drawn centred in its glyph box, so they line up.
 */
#include "ui_battery.h"
#include "battery.h"
#include "config.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "amber.h"
#include "amber_battery_icons.h"

// Every row of the Speakers screen and the Rooms overlay at once, plus the
// header. A badge past this still draws; it just is not refreshed live.
#define BADGE_MAX 72

struct Badge {
    lv_obj_t* box;       // the row - what callers position, and what hides and blinks
    lv_obj_t* glyph;
    lv_obj_t* num;
    int       idx;       // sonos.getDevice() index, or BATTERY_BADGE_CURRENT
    bool      compact;   // no "%" - the Amber header
};

static Badge       s_badges[BADGE_MAX];
static lv_timer_t* s_timer = nullptr;

// Both labels at once. text_opa rather than the row's opa: no layer to allocate
// and blend, so a blinking badge costs the same to draw as a still one.
static void blinkExec(void* obj, int32_t v) {
    lv_obj_t* box = (lv_obj_t*)obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(box); i++) {
        lv_obj_set_style_text_opa(lv_obj_get_child(box, (int32_t)i), (lv_opa_t)v, 0);
    }
}

static void setBlink(lv_obj_t* box, bool on) {
    const bool running = lv_anim_get(box, blinkExec) != nullptr;
    if (on == running) return;
    if (!on) {
        lv_anim_delete(box, blinkExec);
        blinkExec(box, LV_OPA_COVER);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
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

static lv_color_t toneColor(battery::Tone t) {
    switch (t) {
        // Green is otherwise the canvas's colour for live playback only. A
        // healthy battery is the one deliberate exception: a traffic light is
        // read at a glance from across the room, which is the point of it.
        case battery::TONE_GOOD: return AMB_LIVE;
        case battery::TONE_MID:  return AMB_ACCENT;   // Amber's yellow
        case battery::TONE_LOW:  return COL_ERROR;    // the palette's red
        default:                 return AMB_FAINT;    // stale: a guess, so no colour
    }
}

// All no-ops when nothing changed. The timer runs refresh() on every badge
// every second, and LVGL invalidates on a HIDDEN-flag removal, a style write or
// a set_text even when the value is the same - so without these checks a
// visible badge would be redrawn once a second for nothing.
static void setHidden(lv_obj_t* o, bool hidden) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void setColor(lv_obj_t* o, lv_color_t c) {
    if (lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) return;
    lv_obj_set_style_text_color(o, c, 0);
}

static void setText(lv_obj_t* o, const char* t) {
    if (strcmp(lv_label_get_text(o), t) != 0) lv_label_set_text(o, t);
}

static void refresh(const Badge& b) {
    SonosDevice* d = b.idx == BATTERY_BADGE_CURRENT ? sonos.getCurrentDevice()
                                                    : sonos.getDevice(b.idx);
    const battery::View v = batteryViewFor(d);
    if (!v.present) {
        setBlink(b.box, false);
        setHidden(b.box, true);
        return;
    }
    setHidden(b.box, false);

    setText(b.glyph, glyphText(battery::glyphFor(v)));
    char num[8];
    if (v.stale || v.level < 0) snprintf(num, sizeof(num), "--");
    else if (b.compact)         snprintf(num, sizeof(num), "%d", v.level);
    else                        snprintf(num, sizeof(num), "%d%%", v.level);
    setText(b.num, num);

    const lv_color_t c = toneColor(battery::toneFor(v));
    setColor(b.glyph, c);
    setColor(b.num, c);
    setBlink(b.box, battery::warn(v));
}

static void tick(lv_timer_t*) {
    batterySimTick();
    for (const Badge& b : s_badges) {
        if (b.box) refresh(b);
    }
}

static void onDelete(lv_event_t* e) {
    lv_obj_t* box = (lv_obj_t*)lv_event_get_target(e);
    for (Badge& b : s_badges) {
        if (b.box == box) b.box = nullptr;
    }
}

lv_obj_t* batteryBadgeCreate(lv_obj_t* parent, int deviceIndex, bool compact) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, compact ? SX(3) : SX(4), 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* glyph = lv_label_create(box);
    lv_obj_set_style_text_font(glyph, &font_batt_16, 0);
    lv_label_set_text(glyph, "");

    lv_obj_t* num = lv_label_create(box);
    lv_obj_set_style_text_font(num, compact ? &font_text_12 : &font_text_14, 0);
    lv_label_set_text(num, "");

    const Badge made = { box, glyph, num, deviceIndex, compact };
    for (Badge& b : s_badges) {
        if (!b.box) {
            b = made;
            lv_obj_add_event_cb(box, onDelete, LV_EVENT_DELETE, nullptr);
            break;
        }
    }
    if (!s_timer) s_timer = lv_timer_create(tick, 1000, nullptr);

    refresh(made);
    return box;
}
