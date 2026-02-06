/**
 * General Settings Screen
 * Lyrics and other general preferences
 */

#include "ui_common.h"
#include "config.h"
#include "lyrics.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

void createGeneralScreen() {
    scr_general = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_general, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (General is index 6)
    lv_obj_t* content = createSettingsSidebar(scr_general, 6);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    // Title
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, "General");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_pad_bottom(lbl_title, 16, 0);

    // Lyrics toggle
    lv_obj_t* lbl_lyrics = lv_label_create(content);
    lv_label_set_text(lbl_lyrics, "Show Lyrics:");
    lv_obj_set_style_text_color(lbl_lyrics, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_lyrics, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(lbl_lyrics, 8, 0);

    lv_obj_t* lbl_lyrics_desc = lv_label_create(content);
    lv_label_set_text(lbl_lyrics_desc, "Display synced lyrics over album art");
    lv_obj_set_style_text_color(lbl_lyrics_desc, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_lyrics_desc, &lv_font_montserrat_14, 0);

    lv_obj_t* sw_lyrics = lv_switch_create(content);
    lv_obj_set_size(sw_lyrics, 50, 26);
    lv_obj_set_style_margin_top(sw_lyrics, 8, 0);
    // Track (background)
    lv_obj_set_style_radius(sw_lyrics, 13, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw_lyrics, lv_color_hex(0x333333), LV_PART_MAIN);
    // Indicator (colored fill when checked) - no padding so it fills the track
    lv_obj_set_style_bg_color(sw_lyrics, COL_ACCENT, (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    lv_obj_set_style_radius(sw_lyrics, 13, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(sw_lyrics, 0, LV_PART_INDICATOR);
    // Knob (circle)
    lv_obj_set_style_bg_color(sw_lyrics, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_radius(sw_lyrics, 11, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw_lyrics, -3, LV_PART_KNOB);
    if (lyrics_enabled) lv_obj_add_state(sw_lyrics, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_lyrics, [](lv_event_t* e) {
        lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
        lyrics_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        wifiPrefs.putBool("lyrics", lyrics_enabled);
        setLyricsVisible(lyrics_enabled && lyrics_ready);
    }, LV_EVENT_VALUE_CHANGED, NULL);
}
