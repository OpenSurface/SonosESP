/**
 * Settings card helpers — implementations for ui_settings_card.h.
 * Shared by ui_general_screen.cpp, ui_clock_settings.cpp, and any future
 * settings screen that wants the same grouped-card dark look.
 */
#include "ui_settings_card.h"
#include "ui_common.h"
#include "ui_fonts.h"

lv_obj_t* addCard(lv_obj_t* parent, const char* title) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, SET_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, SET_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, SMIN(16), 0);
    lv_obj_set_style_pad_row(card, SY(8), 0);
    lv_obj_set_style_margin_bottom(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_font(lbl, &font_text_20, 0);
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);

        lv_obj_t* underline = lv_obj_create(card);
        lv_obj_set_size(underline, SX(36), SY(2));
        lv_obj_set_style_bg_color(underline, COL_ACCENT, 0);
        lv_obj_set_style_bg_opa(underline, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(underline, 1, 0);
        lv_obj_set_style_border_width(underline, 0, 0);
        lv_obj_set_style_margin_bottom(underline, 4, 0);
        lv_obj_clear_flag(underline, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(underline, LV_OBJ_FLAG_CLICKABLE);
    }

    return card;
}

void addSettingLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_text_14, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_pad_top(lbl, SY(6), 0);
}

// Returns the label so callers can retarget its text later (the clock-face
// selector rewrites its description when the face changes).
lv_obj_t* addDescLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_text_12, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT2, 0);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    return lbl;
}

// ============================================================================
// Screen header
// ----------------------------------------------------------------------------
// Before this helper the settings screens built their heading three different
// ways, and the title's vertical centre landed in a different place in each:
//
//   bare label at y=0        centre ~14px   Sources, Firmware Update
//   label as first flex kid  centre ~14px   General, Display, Clock
//   label in a SY(40) row    centre  20px   Speakers, Groups
//   label in a SY(44) row    centre  22px   WiFi
//
// so the heading jumped as you moved between tabs. The two Scan buttons had
// drifted too: SX(110)xSY(40) r20 on Speakers/Groups, SX(100)xSY(34) r17 on WiFi.
//
// One row height, one title alignment, one button spec. Screens that position
// their content absolutely below the header already use SY(50), which still
// clears the SY(40) row.
// ============================================================================
lv_obj_t* addScreenHeader(lv_obj_t* parent, const char* title, const char* action_text) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, SY(40));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_margin_bottom(row, SY(12), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    if (!action_text) return nullptr;

    lv_obj_t* btn = lv_button_create(row);
    lv_obj_set_size(btn, SX(110), SY(38));
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn, SY(19), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* lbl_btn = lv_label_create(btn);
    lv_label_set_text(lbl_btn, action_text);
    // Black on the gold accent - a contrast pairing, not a themed surface.
    lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_btn, &lv_font_mdi_16, 0);
    lv_obj_center(lbl_btn);
    return btn;
}

lv_obj_t* screenHeaderActionLabel(lv_obj_t* action_btn) {
    if (!action_btn || lv_obj_get_child_count(action_btn) == 0) return nullptr;
    return lv_obj_get_child(action_btn, 0);   // addScreenHeader() adds exactly one
}

lv_obj_t* addValueLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_text_14, 0);
    lv_obj_set_style_text_color(lbl, COL_ACCENT, 0);
    return lbl;
}

lv_obj_t* addSlider(lv_obj_t* parent, int min, int max, int value) {
    lv_obj_t* s = lv_slider_create(parent);
    lv_obj_set_width(s, lv_pct(100));
    lv_obj_set_height(s, SY(20));
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, COL_SELECTED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_radius(s, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(s, 10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(s, SMIN(2), LV_PART_KNOB);

    // Inset the travel by the knob radius.
    //
    // LVGL centres the knob on the value position within the MAIN part's inner
    // area, and lets it overhang the ends. With no horizontal padding the knob at
    // minimum is centred exactly on the track's left edge, so half of it hangs
    // outside the control — visible on Display's "Auto-dim after: 0 sec" and on
    // Clock's "Inactivity timeout", where the knob sits left of every other
    // element's margin. Padding MAIN by the knob's radius moves the travel inward
    // so both end positions land fully inside the track.
    lv_obj_set_style_pad_hor(s, SY(20) / 2 + SMIN(2), LV_PART_MAIN);
    return s;
}

lv_obj_t* addSwitch(lv_obj_t* parent, bool initial) {
    lv_obj_t* sw = lv_switch_create(parent);
    lv_obj_set_size(sw, SX(50), SY(26));
    lv_obj_set_style_margin_top(sw, 4, 0);
    lv_obj_set_style_radius(sw, 13, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COL_SELECTED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COL_ACCENT,
        (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
    lv_obj_set_style_radius(sw, 13, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(sw, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, 11, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, SMIN(-3), LV_PART_KNOB);
    if (initial) lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
}
