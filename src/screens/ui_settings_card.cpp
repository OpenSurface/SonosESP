/**
 * Settings card helpers — implementations for ui_settings_card.h.
 * Shared by ui_general_screen.cpp, ui_clock_settings.cpp, and any future
 * settings screen that wants the same grouped-card dark look.
 */
#include "ui_settings_card.h"
#include "ui_common.h"
#include "ui_fonts.h"
#include "studio_icons.h"
#include "studio.h"

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
        lv_obj_set_style_text_color(lbl, ST_TEXT, 0);

        lv_obj_t* underline = lv_obj_create(card);
        lv_obj_set_size(underline, SX(36), SY(2));
        lv_obj_set_style_bg_color(underline, ST_ACCENT, 0);
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
    lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
    lv_obj_set_style_pad_top(lbl, SY(6), 0);
}

// Returns the label so callers can retarget its text later (the clock-face
// selector rewrites its description when the face changes).
lv_obj_t* addDescLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_text_12, 0);
    lv_obj_set_style_text_color(lbl, ST_TEXT3, 0);
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
    lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    if (!action_text) return nullptr;

    lv_obj_t* btn = lv_button_create(row);
    lv_obj_set_size(btn, SX(110), SY(38));
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn, ST_ACCENT, 0);
    lv_obj_set_style_radius(btn, SY(19), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* lbl_btn = lv_label_create(btn);
    lv_label_set_text(lbl_btn, action_text);
    // Near-black on gold. ST_ON_ACCENT rather than pure black: the canvas pairs
    // #1A1408 with #E0B252, which reads warmer against it.
    lv_obj_set_style_text_color(lbl_btn, ST_ON_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_btn, &font_icon_16, 0);
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
    lv_obj_set_style_text_color(lbl, ST_ACCENT, 0);
    return lbl;
}

lv_obj_t* addSlider(lv_obj_t* parent, int min, int max, int value) {
    lv_obj_t* s = lv_slider_create(parent);
    lv_obj_set_width(s, lv_pct(100));
    lv_obj_set_height(s, SY(20));
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, ST_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, ST_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, ST_ACCENT, LV_PART_KNOB);
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
    lv_obj_set_style_bg_color(sw, ST_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, ST_ACCENT,
        (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
    lv_obj_set_style_radius(sw, 13, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(sw, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, ST_TEXT, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, 11, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, SMIN(-3), LV_PART_KNOB);
    if (initial) lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
}

// ============================================================================
// Setting rows — label block left, control right
// ----------------------------------------------------------------------------
// See the block comment in ui_settings_card.h for why the stacked form was
// replaced. Both builders below share one skeleton:
//
//   row (flex ROW, cross-axis CENTER, hairline underneath)
//     +- block (flex COLUMN, flex_grow 1)   title, then optional description
//     +- slot  (SIZE_CONTENT)               whatever the caller creates
//
// The description wraps, so the row's height is CONTENT and the hairline
// follows it down rather than clipping a two-line description.
// ============================================================================

// Shared skeleton. Returns the row; `out_block` receives the left column.
static lv_obj_t* settingRowShell(lv_obj_t* parent, bool separator, lv_obj_t** out_block) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 0, 0);
    lv_obj_set_style_pad_ver(row, SY(11), 0);
    lv_obj_set_style_pad_column(row, SX(16), 0);
    lv_obj_set_style_border_width(row, separator ? 1 : 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, ST_CARD, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* block = lv_obj_create(row);
    lv_obj_remove_style_all(block);
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(block, 1);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(block, SY(3), 0);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_CLICKABLE);

    *out_block = block;
    return row;
}

// The row's title, at the canvas's 16/500.
static void settingRowTitle(lv_obj_t* block, const char* text) {
    lv_obj_t* lbl = lv_label_create(block);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_text_16, 0);
    lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
}

// The row's description, wrapping to the block's width.
static void settingRowDescCreate(lv_obj_t* block, const char* text) {
    if (!text) return;
    lv_obj_t* lbl = lv_label_create(block);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_text_12, 0);
    lv_obj_set_style_text_color(lbl, ST_TEXT3, 0);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
}

lv_obj_t* addSettingRow(lv_obj_t* parent, const char* title, const char* desc,
                        bool separator) {
    lv_obj_t* block = nullptr;
    lv_obj_t* row = settingRowShell(parent, separator, &block);
    settingRowTitle(block, title);
    settingRowDescCreate(block, desc);

    lv_obj_t* slot = lv_obj_create(row);
    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slot, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_CLICKABLE);
    return slot;
}

lv_obj_t* settingRowDesc(lv_obj_t* slot) {
    if (!slot) return nullptr;
    lv_obj_t* row = lv_obj_get_parent(slot);
    if (!row || lv_obj_get_child_count(row) == 0) return nullptr;
    lv_obj_t* block = lv_obj_get_child(row, 0);      // the label column
    // Child 0 is the title; a description, when there is one, is child 1.
    if (!block || lv_obj_get_child_count(block) < 2) return nullptr;
    return lv_obj_get_child(block, 1);
}

lv_obj_t* addSliderRow(lv_obj_t* parent, const char* title, const char* desc,
                       bool separator, lv_obj_t** out_value) {
    // A slider spans the row rather than sitting beside its label, so this one
    // is a COLUMN: the title line (with the value right-aligned on it) above the
    // full-width track the caller adds.
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 0, 0);
    lv_obj_set_style_pad_ver(row, SY(11), 0);
    lv_obj_set_style_pad_row(row, SY(4), 0);
    lv_obj_set_style_border_width(row, separator ? 1 : 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, ST_CARD, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* head = lv_obj_create(row);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl = lv_label_create(head);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &font_text_16, 0);
    lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t* val = lv_label_create(head);
    lv_label_set_text(val, "");
    lv_obj_set_style_text_font(val, &font_text_14, 0);
    lv_obj_set_style_text_color(val, ST_ACCENT, 0);
    if (out_value) *out_value = val;

    if (desc) {
        lv_obj_t* d = lv_label_create(row);
        lv_label_set_text(d, desc);
        lv_obj_set_style_text_font(d, &font_text_12, 0);
        lv_obj_set_style_text_color(d, ST_TEXT3, 0);
        lv_obj_set_width(d, lv_pct(100));
        lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    }
    return row;
}
