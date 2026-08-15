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
