/**
 * Display Settings Screen
 * Brightness control and auto-dimming configuration
 */

#include "ui_common.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// Display Settings Screen
// ============================================================================
void createDisplaySettingsScreen() {
    scr_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_display, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Display is index 4)
    lv_obj_t* content = createSettingsSidebar(scr_display, 4);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    // Title
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, "Display");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_pad_bottom(lbl_title, 16, 0);

    // Brightness
    lv_obj_t* lbl_brightness = lv_label_create(content);
    lv_label_set_text(lbl_brightness, "Brightness:");
    lv_obj_set_style_text_color(lbl_brightness, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_brightness, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(lbl_brightness, 8, 0);

    static lv_obj_t* lbl_brightness_val;
    lbl_brightness_val = lv_label_create(content);
    lv_label_set_text_fmt(lbl_brightness_val, "%d%%", brightness_level);
    lv_obj_set_style_text_color(lbl_brightness_val, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_brightness_val, &lv_font_montserrat_14, 0);

    lv_obj_t* slider_brightness = lv_slider_create(content);
    lv_obj_set_width(slider_brightness, lv_pct(100));
    lv_obj_set_height(slider_brightness, 20);
    lv_slider_set_range(slider_brightness, 10, 100);
    lv_slider_set_value(slider_brightness, brightness_level, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_brightness, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_brightness, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_brightness, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_brightness, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_brightness, 10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider_brightness, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_top(slider_brightness, 4, 0);
    lv_obj_set_style_pad_bottom(slider_brightness, 16, 0);
    lv_obj_add_event_cb(slider_brightness, [](lv_event_t* e) {
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        int val = lv_slider_get_value(slider);
        setBrightness(val);
        lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", val);
    }, LV_EVENT_VALUE_CHANGED, lbl_brightness_val);

    // Dim timeout
    lv_obj_t* lbl_dim_timeout = lv_label_create(content);
    lv_label_set_text(lbl_dim_timeout, "Auto-dim after:");
    lv_obj_set_style_text_color(lbl_dim_timeout, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_dim_timeout, &lv_font_montserrat_16, 0);

    static lv_obj_t* lbl_dim_timeout_val;
    lbl_dim_timeout_val = lv_label_create(content);
    lv_label_set_text_fmt(lbl_dim_timeout_val, "%d sec", autodim_timeout);
    lv_obj_set_style_text_color(lbl_dim_timeout_val, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_dim_timeout_val, &lv_font_montserrat_14, 0);

    lv_obj_t* slider_dim_timeout = lv_slider_create(content);
    lv_obj_set_width(slider_dim_timeout, lv_pct(100));
    lv_obj_set_height(slider_dim_timeout, 20);
    lv_slider_set_range(slider_dim_timeout, 0, 300);
    lv_slider_set_value(slider_dim_timeout, autodim_timeout, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_dim_timeout, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_dim_timeout, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_dim_timeout, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_dim_timeout, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_dim_timeout, 10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider_dim_timeout, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_top(slider_dim_timeout, 4, 0);
    lv_obj_set_style_pad_bottom(slider_dim_timeout, 16, 0);
    lv_obj_add_event_cb(slider_dim_timeout, [](lv_event_t* e) {
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        autodim_timeout = lv_slider_get_value(slider);
        lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d sec", autodim_timeout);
        wifiPrefs.putInt("autodim_sec", autodim_timeout);
    }, LV_EVENT_VALUE_CHANGED, lbl_dim_timeout_val);

    // Dimmed brightness
    lv_obj_t* lbl_dimmed = lv_label_create(content);
    lv_label_set_text(lbl_dimmed, "Dimmed brightness:");
    lv_obj_set_style_text_color(lbl_dimmed, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_dimmed, &lv_font_montserrat_16, 0);

    static lv_obj_t* lbl_dimmed_brightness_val;
    lbl_dimmed_brightness_val = lv_label_create(content);
    lv_label_set_text_fmt(lbl_dimmed_brightness_val, "%d%%", brightness_dimmed);
    lv_obj_set_style_text_color(lbl_dimmed_brightness_val, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_dimmed_brightness_val, &lv_font_montserrat_14, 0);

    lv_obj_t* slider_dimmed_brightness = lv_slider_create(content);
    lv_obj_set_width(slider_dimmed_brightness, lv_pct(100));
    lv_obj_set_height(slider_dimmed_brightness, 20);
    lv_slider_set_range(slider_dimmed_brightness, 5, 50);
    lv_slider_set_value(slider_dimmed_brightness, brightness_dimmed, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_dimmed_brightness, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_dimmed_brightness, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_dimmed_brightness, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_dimmed_brightness, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_dimmed_brightness, 10, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider_dimmed_brightness, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_top(slider_dimmed_brightness, 4, 0);
    lv_obj_set_style_pad_bottom(slider_dimmed_brightness, 16, 0);
    lv_obj_add_event_cb(slider_dimmed_brightness, [](lv_event_t* e) {
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        brightness_dimmed = lv_slider_get_value(slider);
        lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", brightness_dimmed);
        wifiPrefs.putInt("brightness_dimmed", brightness_dimmed);
        if (screen_dimmed) setBrightness(brightness_dimmed);
    }, LV_EVENT_VALUE_CHANGED, lbl_dimmed_brightness_val);
}
