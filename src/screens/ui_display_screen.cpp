/**
 * Display Settings Screen
 * Brightness control and auto-dimming configuration
 */

#include "ui_common.h"
#include "ui_fonts.h"

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
    lv_obj_set_style_text_font(lbl_title, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_pad_bottom(lbl_title, 16, 0);

    // Brightness
    lv_obj_t* lbl_brightness = lv_label_create(content);
    lv_label_set_text(lbl_brightness, "Brightness:");
    lv_obj_set_style_text_color(lbl_brightness, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_brightness, &font_text_16, 0);
    lv_obj_set_style_pad_top(lbl_brightness, 8, 0);

    static lv_obj_t* lbl_brightness_val;
    lbl_brightness_val = lv_label_create(content);
    lv_label_set_text_fmt(lbl_brightness_val, "%d%%", brightness_level);
    lv_obj_set_style_text_color(lbl_brightness_val, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_brightness_val, &font_text_14, 0);

    lv_obj_t* slider_brightness = lv_slider_create(content);
    lv_obj_set_width(slider_brightness, lv_pct(100));
    lv_obj_set_height(slider_brightness, SY(20));
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
    lv_obj_set_style_text_font(lbl_dim_timeout, &font_text_16, 0);

    static lv_obj_t* lbl_dim_timeout_val;
    lbl_dim_timeout_val = lv_label_create(content);
    lv_label_set_text_fmt(lbl_dim_timeout_val, "%d sec", autodim_timeout);
    lv_obj_set_style_text_color(lbl_dim_timeout_val, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_dim_timeout_val, &font_text_14, 0);

    lv_obj_t* slider_dim_timeout = lv_slider_create(content);
    lv_obj_set_width(slider_dim_timeout, lv_pct(100));
    lv_obj_set_height(slider_dim_timeout, SY(20));
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
    lv_obj_set_style_text_font(lbl_dimmed, &font_text_16, 0);

    static lv_obj_t* lbl_dimmed_brightness_val;
    lbl_dimmed_brightness_val = lv_label_create(content);
    lv_label_set_text_fmt(lbl_dimmed_brightness_val, "%d%%", brightness_dimmed);
    lv_obj_set_style_text_color(lbl_dimmed_brightness_val, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_dimmed_brightness_val, &font_text_14, 0);

    lv_obj_t* slider_dimmed_brightness = lv_slider_create(content);
    lv_obj_set_width(slider_dimmed_brightness, lv_pct(100));
    lv_obj_set_height(slider_dimmed_brightness, SY(20));
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
        wifiPrefs.putInt(NVS_KEY_BRIGHTNESS_DIM, brightness_dimmed);
        if (screen_dimmed) setBrightness(brightness_dimmed);
    }, LV_EVENT_VALUE_CHANGED, lbl_dimmed_brightness_val);

#if SCREEN_SIZE == 7
    // ── Panel type (7" only) ────────────────────────────────────────────────
    // GUITION ship two different LCD panels under the same JC1060P470C_I_W_Y
    // SKU. Nothing in the firmware can tell them apart — same JD9165 controller,
    // same 1024x600, same chip ID — so the user picks. Wrong choice shows as
    // vertical white stripes or a blank/flashing screen (#113).
    lv_obj_t* lbl_panel = lv_label_create(content);
    lv_label_set_text(lbl_panel, "Panel type:");
    lv_obj_set_style_text_color(lbl_panel, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_panel, &font_text_16, 0);
    lv_obj_set_style_pad_top(lbl_panel, SY(8), 0);

    lv_obj_t* lbl_panel_hint = lv_label_create(content);
    lv_label_set_text(lbl_panel_hint,
        "If the screen shows white stripes or stays blank, switch this. Restarts the device.");
    lv_obj_set_style_text_color(lbl_panel_hint, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_panel_hint, &font_text_12, 0);
    lv_obj_set_width(lbl_panel_hint, lv_pct(100));
    lv_label_set_long_mode(lbl_panel_hint, LV_LABEL_LONG_WRAP);

    lv_obj_t* dd_panel = lv_dropdown_create(content);
    lv_dropdown_set_options(dd_panel, "Newer screen\nOlder screen");
    lv_dropdown_set_selected(dd_panel, panel_variant == PANEL_VARIANT_OLD ? 1 : 0);
    lv_obj_set_width(dd_panel, lv_pct(100));
    lv_obj_set_style_bg_color(dd_panel, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_color(dd_panel, COL_TEXT, 0);
    lv_obj_set_style_text_font(dd_panel, &font_text_14, 0);
    lv_obj_set_style_radius(dd_panel, 8, 0);
    lv_obj_set_style_pad_all(dd_panel, 10, 0);
    lv_obj_set_style_margin_top(dd_panel, SY(6), 0);
    lv_obj_add_event_cb(dd_panel, [](lv_event_t* e) {
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        int sel = lv_dropdown_get_selected(dd) == 1 ? PANEL_VARIANT_OLD : PANEL_VARIANT_NEW;
        if (sel == panel_variant) return;
        panel_variant = sel;
        wifiPrefs.putInt(NVS_KEY_PANEL_VAR, panel_variant);
        // The panel init sequence and DSI timings are applied once at boot, so
        // the only way to switch is to restart. Blank the backlight first so a
        // mis-set panel doesn't flash garbage on the way down.
        Serial.printf("[Display] Panel variant -> %s, restarting\n",
                      panel_variant == PANEL_VARIANT_OLD ? "Old" : "New");
        display_set_brightness(0);
        vTaskDelay(pdMS_TO_TICKS(150));
        ESP.restart();
    }, LV_EVENT_VALUE_CHANGED, NULL);
#endif
}
