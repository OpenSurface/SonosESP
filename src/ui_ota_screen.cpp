/**
 * OTA (Over-The-Air) Update Screen
 * Firmware update management with GitHub integration
 */

#include "ui_common.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// OTA Update Screen
// ============================================================================
void createOTAScreen() {
    scr_ota = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_ota, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Update is index 5)
    lv_obj_t* content = createSettingsSidebar(scr_ota, 5);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, "Firmware Update");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

    // Version info card
    lv_obj_t* card_version = lv_obj_create(content);
    lv_obj_set_size(card_version, lv_pct(100), 100);
    lv_obj_set_pos(card_version, 0, 40);
    lv_obj_set_style_bg_color(card_version, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(card_version, 12, 0);
    lv_obj_set_style_border_width(card_version, 0, 0);
    lv_obj_set_style_pad_all(card_version, 16, 0);
    lv_obj_clear_flag(card_version, LV_OBJ_FLAG_SCROLLABLE);

    lbl_current_version = lv_label_create(card_version);
    lv_label_set_text_fmt(lbl_current_version, "Current: v" FIRMWARE_VERSION);
    lv_obj_set_style_text_font(lbl_current_version, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_current_version, COL_TEXT, 0);
    lv_obj_align(lbl_current_version, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_latest_version = lv_label_create(card_version);
    lv_label_set_text(lbl_latest_version, "Latest: Checking...");
    lv_obj_set_style_text_font(lbl_latest_version, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_latest_version, COL_TEXT2, 0);
    lv_obj_align(lbl_latest_version, LV_ALIGN_TOP_LEFT, 0, 30);

    // Status label
    lbl_ota_status = lv_label_create(content);
    lv_obj_set_pos(lbl_ota_status, 0, 160);
    lv_label_set_text(lbl_ota_status, "Tap 'Check for Updates' to begin");
    lv_obj_set_style_text_color(lbl_ota_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_ota_status, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_ota_status, lv_pct(100));
    lv_label_set_long_mode(lbl_ota_status, LV_LABEL_LONG_WRAP);

    // Progress label
    lbl_ota_progress = lv_label_create(content);
    lv_obj_set_pos(lbl_ota_progress, 0, 190);
    lv_label_set_text(lbl_ota_progress, "");
    lv_obj_set_style_text_color(lbl_ota_progress, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_ota_progress, &lv_font_montserrat_16, 0);

    // Visual progress bar (hidden by default)
    bar_ota_progress = lv_bar_create(content);
    lv_obj_set_size(bar_ota_progress, lv_pct(100), 16);
    lv_obj_set_pos(bar_ota_progress, 0, 220);
    lv_bar_set_range(bar_ota_progress, 0, 100);
    lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_ota_progress, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_ota_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_ota_progress, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_ota_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_ota_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_ota_progress, 8, LV_PART_INDICATOR);
    lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);

    // Check for Updates button
    btn_check_update = lv_btn_create(content);
    lv_obj_set_size(btn_check_update, 280, 50);
    lv_obj_set_pos(btn_check_update, 0, 260);
    lv_obj_set_style_bg_color(btn_check_update, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_check_update, 12, 0);
    lv_obj_add_event_cb(btn_check_update, ev_check_update, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_check = lv_label_create(btn_check_update);
    lv_label_set_text(lbl_check, LV_SYMBOL_REFRESH " Check for Updates");
    lv_obj_set_style_text_color(lbl_check, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_check, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_check);

    // Install Update button (hidden by default)
    btn_install_update = lv_btn_create(content);
    lv_obj_set_size(btn_install_update, 280, 50);
    lv_obj_set_pos(btn_install_update, 310, 260);
    lv_obj_set_style_bg_color(btn_install_update, lv_color_hex(0x4ECB71), 0);
    lv_obj_set_style_radius(btn_install_update, 12, 0);
    lv_obj_add_event_cb(btn_install_update, ev_install_update, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_install = lv_label_create(btn_install_update);
    lv_label_set_text(lbl_install, LV_SYMBOL_DOWNLOAD " Install Update");
    lv_obj_set_style_text_color(lbl_install, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_install, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_install);
    lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);  // Hidden until update available

    // Info text
    lv_obj_t* lbl_info = lv_label_create(content);
    lv_label_set_text(lbl_info,
        LV_SYMBOL_WARNING "  Do not disconnect power during update!\n"
        "Updates are fetched from GitHub releases automatically.");
    lv_obj_set_style_text_color(lbl_info, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_12, 0);
    lv_obj_set_width(lbl_info, lv_pct(100));
    lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(lbl_info, 0, 330);
}
