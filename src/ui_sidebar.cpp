/**
 * Settings Sidebar - Shared Navigation Component
 * Creates sidebar with menu items and returns content area
 */

#include "ui_common.h"

// ============================================================================
// Settings sidebar - creates sidebar and returns content area
// ============================================================================
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx) {
    // ========== LEFT SIDEBAR ==========
    lv_obj_t* sidebar = lv_obj_create(screen);
    lv_obj_set_size(sidebar, 180, 480);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(sidebar, 1, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // Title + close button row
    lv_obj_t* title_row = lv_obj_create(sidebar);
    lv_obj_set_size(title_row, 180, 50);
    lv_obj_set_pos(title_row, 0, 0);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(title_row);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 12, 14);

    lv_obj_t* btn_close = lv_button_create(title_row);
    lv_obj_set_size(btn_close, 32, 32);
    lv_obj_set_pos(btn_close, 140, 10);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_close, 16, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_x = lv_label_create(btn_close);
    lv_label_set_text(ico_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_x, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_x, &lv_font_montserrat_14, 0);
    lv_obj_center(ico_x);

    // Menu items
    const char* icons[] = {LV_SYMBOL_AUDIO, LV_SYMBOL_SHUFFLE, LV_SYMBOL_LIST, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_WIFI, LV_SYMBOL_DOWNLOAD};
    const char* labels[] = {"Speakers", "Groups", "Sources", "Display", "WiFi", "Update"};

    int y = 55;
    for (int i = 0; i < 6; i++) {
        lv_obj_t* btn = lv_button_create(sidebar);
        lv_obj_set_size(btn, 164, 42);
        lv_obj_set_pos(btn, 8, y);

        bool active = (i == activeIdx);
        lv_obj_set_style_bg_color(btn, active ? COL_ACCENT : lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_left(btn, 10, 0);

        lv_obj_t* ico = lv_label_create(btn);
        lv_label_set_text(ico, icons[i]);
        lv_obj_set_style_text_color(ico, active ? lv_color_hex(0x000000) : COL_TEXT2, 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_16, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, active ? lv_color_hex(0x000000) : COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 26, 0);

        // Navigation callbacks
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            switch(idx) {
                case 0: lv_screen_load(scr_devices); break;
                case 1: lv_screen_load(scr_groups); break;
                case 2: lv_screen_load(scr_sources); break;
                case 3: lv_screen_load(scr_display); break;
                case 4: lv_screen_load(scr_wifi); break;
                case 5: lv_screen_load(scr_ota); break;
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        y += 46;
    }

    // Version at bottom
    lv_obj_t* ver = lv_label_create(sidebar);
    lv_label_set_text_fmt(ver, "v%s", FIRMWARE_VERSION);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver, COL_TEXT2, 0);
    lv_obj_set_pos(ver, 12, 455);

    // ========== RIGHT CONTENT AREA ==========
    lv_obj_t* content = lv_obj_create(screen);
    lv_obj_set_size(content, 620, 480);
    lv_obj_set_pos(content, 180, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 24, 0);

    return content;
}
