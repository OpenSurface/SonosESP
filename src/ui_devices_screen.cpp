/**
 * Devices (Speakers) Settings Screen
 * Shows discovered Sonos devices, group status, and scan functionality
 */

#include "ui_common.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// Devices (Speakers) Screen
// ============================================================================
void refreshDeviceList() {
    lv_obj_clean(list_devices);
    int cnt = sonos.getDeviceCount();
    SonosDevice* current = sonos.getCurrentDevice();

    // First pass: Show group coordinators (standalone or group leaders)
    for (int i = 0; i < cnt; i++) {
        SonosDevice* dev = sonos.getDevice(i);
        if (!dev) continue;

        // Skip non-coordinators (they'll be shown under their coordinator)
        if (!dev->isGroupCoordinator) continue;

        // Count members in this group
        int memberCount = 1;
        for (int j = 0; j < cnt; j++) {
            if (j == i) continue;
            SonosDevice* member = sonos.getDevice(j);
            if (member && member->groupCoordinatorUUID == dev->rinconID) {
                memberCount++;
            }
        }

        bool isSelected = (current && dev->ip == current->ip);
        bool isPlaying = dev->isPlaying;
        bool hasGroup = (memberCount > 1);

        // Create main button - taller if it has subtitle
        lv_obj_t* btn = lv_btn_create(list_devices);
        lv_obj_set_size(btn, lv_pct(100), hasGroup || isPlaying ? 70 : 60);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 12, 0);

        lv_obj_set_style_bg_color(btn, isSelected ? COL_SELECTED : COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);

        if (isSelected) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, COL_ACCENT, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        // Speaker icon - show double icon for groups
        lv_obj_t* icon = lv_label_create(btn);
        if (hasGroup) {
            lv_label_set_text(icon, LV_SYMBOL_AUDIO LV_SYMBOL_AUDIO);
        } else {
            lv_label_set_text(icon, LV_SYMBOL_AUDIO);
        }
        lv_obj_set_style_text_color(icon, isPlaying ? COL_ACCENT : (isSelected ? COL_ACCENT : COL_TEXT2), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_18, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, hasGroup || isPlaying ? -8 : 0);

        // Room name
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, dev->roomName.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, hasGroup ? 55 : 45, hasGroup || isPlaying ? -8 : 0);

        // Subtitle: group info or playing status
        if (hasGroup || isPlaying) {
            lv_obj_t* sub = lv_label_create(btn);
            if (hasGroup && isPlaying) {
                lv_label_set_text_fmt(sub, LV_SYMBOL_PLAY " Playing  " LV_SYMBOL_AUDIO " +%d speakers", memberCount - 1);
            } else if (hasGroup) {
                lv_label_set_text_fmt(sub, LV_SYMBOL_AUDIO " +%d speaker%s", memberCount - 1, memberCount > 2 ? "s" : "");
            } else {
                lv_label_set_text(sub, LV_SYMBOL_PLAY " Playing");
            }
            lv_obj_set_style_text_color(sub, isPlaying ? lv_color_hex(0x4ECB71) : COL_TEXT2, 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_LEFT_MID, hasGroup ? 55 : 45, 12);
        }

        // Right arrow indicator
        lv_obj_t* arrow = lv_label_create(btn);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, COL_TEXT2, 0);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -5, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            sonos.selectDevice(idx);
            sonos.startTasks();
            lv_screen_load(scr_main);
        }, LV_EVENT_CLICKED, NULL);

        // Show group members as indented sub-items
        if (hasGroup) {
            for (int j = 0; j < cnt; j++) {
                if (j == i) continue;
                SonosDevice* member = sonos.getDevice(j);
                if (!member || member->groupCoordinatorUUID != dev->rinconID) continue;

                lv_obj_t* memBtn = lv_btn_create(list_devices);
                lv_obj_set_size(memBtn, lv_pct(95), 50);
                lv_obj_set_user_data(memBtn, (void*)(intptr_t)j);
                lv_obj_set_style_radius(memBtn, 8, 0);
                lv_obj_set_style_shadow_width(memBtn, 0, 0);
                lv_obj_set_style_pad_all(memBtn, 10, 0);
                lv_obj_set_style_bg_color(memBtn, lv_color_hex(0x252525), 0);
                lv_obj_set_style_bg_color(memBtn, COL_BTN_PRESSED, LV_STATE_PRESSED);
                lv_obj_set_style_margin_left(memBtn, 40, 0);

                // Linking icon
                lv_obj_t* memIcon = lv_label_create(memBtn);
                lv_label_set_text(memIcon, LV_SYMBOL_RIGHT " " LV_SYMBOL_AUDIO);
                lv_obj_set_style_text_color(memIcon, COL_TEXT2, 0);
                lv_obj_set_style_text_font(memIcon, &lv_font_montserrat_14, 0);
                lv_obj_align(memIcon, LV_ALIGN_LEFT_MID, 5, 0);

                lv_obj_t* memLbl = lv_label_create(memBtn);
                lv_label_set_text(memLbl, member->roomName.c_str());
                lv_obj_set_style_text_color(memLbl, COL_TEXT, 0);
                lv_obj_set_style_text_font(memLbl, &lv_font_montserrat_16, 0);
                lv_obj_align(memLbl, LV_ALIGN_LEFT_MID, 55, 0);

                // "Grouped" badge
                lv_obj_t* badge = lv_label_create(memBtn);
                lv_label_set_text(badge, "Grouped");
                lv_obj_set_style_text_color(badge, COL_TEXT2, 0);
                lv_obj_set_style_text_font(badge, &lv_font_montserrat_12, 0);
                lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -10, 0);

                // Click to select this member directly
                lv_obj_add_event_cb(memBtn, [](lv_event_t* e) {
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    sonos.selectDevice(idx);
                    sonos.startTasks();
                    lv_screen_load(scr_main);
                }, LV_EVENT_CLICKED, NULL);
            }
        }
    }

    // Second pass: Show any standalone non-coordinators (shouldn't happen normally, but just in case)
    for (int i = 0; i < cnt; i++) {
        SonosDevice* dev = sonos.getDevice(i);
        if (!dev || dev->isGroupCoordinator) continue;

        // Check if this device's coordinator is in our list
        bool coordinatorFound = false;
        for (int j = 0; j < cnt; j++) {
            SonosDevice* coord = sonos.getDevice(j);
            if (coord && coord->rinconID == dev->groupCoordinatorUUID) {
                coordinatorFound = true;
                break;
            }
        }

        // If coordinator not found, show as standalone
        if (!coordinatorFound) {
            bool isSelected = (current && dev->ip == current->ip);

            lv_obj_t* btn = lv_btn_create(list_devices);
            lv_obj_set_size(btn, 720, 60);
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_set_style_radius(btn, 12, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 15, 0);
            lv_obj_set_style_bg_color(btn, isSelected ? COL_SELECTED : COL_CARD, 0);

            lv_obj_t* icon = lv_label_create(btn);
            lv_label_set_text(icon, LV_SYMBOL_AUDIO);
            lv_obj_set_style_text_color(icon, COL_TEXT2, 0);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, dev->roomName.c_str());
            lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 40, 0);

            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                sonos.selectDevice(idx);
                sonos.startTasks();
                lv_screen_load(scr_main);
            }, LV_EVENT_CLICKED, NULL);
        }
    }
}

void createDevicesScreen() {
    scr_devices = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_devices, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Speakers is index 1)
    lv_obj_t* content = createSettingsSidebar(scr_devices, 1);
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
    lv_label_set_text(lbl_title, "Speakers");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    btn_sonos_scan = lv_button_create(title_row);
    lv_obj_set_size(btn_sonos_scan, 110, 40);
    lv_obj_align(btn_sonos_scan, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_sonos_scan, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_sonos_scan, 20, 0);
    lv_obj_set_style_shadow_width(btn_sonos_scan, 0, 0);
    lv_obj_add_event_cb(btn_sonos_scan, ev_discover, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_scan = lv_label_create(btn_sonos_scan);
    lv_label_set_text(lbl_scan, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_scan, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_scan);

    // Status label
    lbl_status = lv_label_create(content);
    lv_obj_set_pos(lbl_status, 0, 50);
    lv_label_set_text(lbl_status, "Tap Scan to find speakers");
    lv_obj_set_style_text_color(lbl_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);

    // Devices list
    list_devices = lv_list_create(content);
    lv_obj_set_size(list_devices, lv_pct(100), 380);
    lv_obj_set_pos(list_devices, 0, 75);
    lv_obj_set_style_bg_color(list_devices, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_devices, 0, 0);
    lv_obj_set_style_radius(list_devices, 0, 0);
    lv_obj_set_style_pad_all(list_devices, 0, 0);
    lv_obj_set_style_pad_row(list_devices, 6, 0);

    // Professional scrollbar styling
    lv_obj_set_style_pad_right(list_devices, 8, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_devices, LV_OPA_30, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_devices, COL_TEXT2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_devices, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_devices, 3, LV_PART_SCROLLBAR);

    // Spinner for scan feedback (centered in content area, hidden by default)
    spinner_scan = lv_spinner_create(content);
    lv_obj_set_size(spinner_scan, 100, 100);
    lv_obj_center(spinner_scan);
    lv_obj_set_style_arc_color(spinner_scan, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_scan, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner_scan, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner_scan, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(spinner_scan, true, LV_PART_INDICATOR);
    lv_obj_move_foreground(spinner_scan);  // Ensure it's on top
    lv_obj_add_flag(spinner_scan, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
}
