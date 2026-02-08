/**
 * WiFi Settings Screen
 * Network scanning and connection management
 */

#include "ui_common.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// WiFi Screen
// ============================================================================
void createWiFiScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (WiFi is index 5)
    lv_obj_t* content = createSettingsSidebar(scr_wifi, 5);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title + Scan button row
    lv_obj_t* title_row = lv_obj_create(content);
    lv_obj_set_size(title_row, lv_pct(100), 40);
    lv_obj_set_pos(title_row, 0, 0);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(title_row);
    lv_label_set_text(lbl_title, "WiFi");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    btn_wifi_scan = lv_button_create(title_row);
    lv_obj_set_size(btn_wifi_scan, 90, 32);
    lv_obj_align(btn_wifi_scan, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_wifi_scan, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_scan, 16, 0);
    lv_obj_set_style_shadow_width(btn_wifi_scan, 0, 0);
    lv_obj_add_event_cb(btn_wifi_scan, ev_wifi_scan, LV_EVENT_CLICKED, NULL);
    lbl_scan_text = lv_label_create(btn_wifi_scan);
    lv_label_set_text(lbl_scan_text, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_color(lbl_scan_text, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_scan_text, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_scan_text);

    // Status label
    lbl_wifi_status = lv_label_create(content);
    lv_obj_set_pos(lbl_wifi_status, 0, 50);
    lv_label_set_text(lbl_wifi_status, "Tap Scan to find networks");
    lv_obj_set_style_text_color(lbl_wifi_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_montserrat_12, 0);

    // WiFi list (left column)
    list_wifi = lv_list_create(content);
    lv_obj_set_size(list_wifi, 280, 360);
    lv_obj_set_pos(list_wifi, 0, 75);
    lv_obj_set_style_bg_color(list_wifi, COL_BG, 0);
    lv_obj_set_style_border_width(list_wifi, 0, 0);
    lv_obj_set_style_radius(list_wifi, 0, 0);
    lv_obj_set_style_pad_all(list_wifi, 0, 0);
    lv_obj_set_style_pad_row(list_wifi, 6, 0);

    // Password section (right column)
    lv_obj_t* pl = lv_label_create(content);
    lv_obj_set_pos(pl, 290, 75);
    lv_label_set_text(pl, "Password:");
    lv_obj_set_style_text_color(pl, COL_TEXT, 0);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_14, 0);

    ta_password = lv_textarea_create(content);
    lv_obj_set_size(ta_password, 300, 40);
    lv_obj_set_pos(ta_password, 290, 100);
    lv_textarea_set_password_mode(ta_password, true);
    lv_textarea_set_placeholder_text(ta_password, "Enter password");
    lv_obj_set_style_bg_color(ta_password, COL_CARD, 0);
    lv_obj_set_style_text_color(ta_password, COL_TEXT, 0);
    lv_obj_set_style_border_color(ta_password, COL_BTN, 0);
    lv_obj_add_event_cb(ta_password, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_FOCUSED) lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_ALL, NULL);

    btn_wifi_connect = lv_btn_create(content);
    lv_obj_set_size(btn_wifi_connect, 300, 44);
    lv_obj_set_pos(btn_wifi_connect, 290, 150);
    lv_obj_set_style_bg_color(btn_wifi_connect, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_connect, 12, 0);
    lv_obj_add_event_cb(btn_wifi_connect, ev_wifi_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cl = lv_label_create(btn_wifi_connect);
    lv_label_set_text(cl, "Connect");
    lv_obj_set_style_text_color(cl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_16, 0);
    lv_obj_center(cl);

    // Built-in LVGL keyboard with modern professional design (positioned to not cover sidebar)
    kb = lv_keyboard_create(scr_wifi);
    lv_keyboard_set_textarea(kb, ta_password);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_size(kb, 615, 175);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 90, -5);  // Bottom aligned, shifted right by 90px (half of sidebar width) to avoid covering it
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // Apply custom styling to match theme
    lv_obj_set_style_bg_color(kb, COL_CARD, 0);
    lv_obj_set_style_pad_all(kb, 5, 0);
    lv_obj_set_style_radius(kb, 10, 0);
    lv_obj_set_style_bg_color(kb, COL_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);

    // Hide keyboard when OK is pressed (auto-handled by built-in keyboard)
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY) {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_ALL, NULL);
}
