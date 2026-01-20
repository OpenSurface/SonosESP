/**
 * UI Settings Screens
 * All settings-related screens: Devices, Groups, Sources, Display, WiFi, OTA, Queue, Browse
 */

#include "ui_common.h"

// Forward declarations for internal helper functions

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

    // Create sidebar and get content area (Speakers is index 0)
    lv_obj_t* content = createSettingsSidebar(scr_devices, 0);
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

// ============================================================================
// Groups Screen
// ============================================================================
void refreshGroupsList() {
    if (!list_groups) return;
    lv_obj_clean(list_groups);

    int cnt = sonos.getDeviceCount();
    if (cnt == 0) {
        lv_label_set_text(lbl_groups_status, "No speakers found. Tap Scan to discover.");
        return;
    }

    // Count groups (coordinators)
    int groupCount = 0;
    for (int i = 0; i < cnt; i++) {
        SonosDevice* dev = sonos.getDevice(i);
        if (dev && dev->isGroupCoordinator) groupCount++;
    }

    lv_label_set_text_fmt(lbl_groups_status, "%d speaker%s, %d group%s",
        cnt, cnt == 1 ? "" : "s",
        groupCount, groupCount == 1 ? "" : "s");

    // First pass: Show group coordinators with their members
    for (int i = 0; i < cnt; i++) {
        SonosDevice* dev = sonos.getDevice(i);
        if (!dev || !dev->isGroupCoordinator) continue;

        // Count members in this group
        int memberCount = 0;
        for (int j = 0; j < cnt; j++) {
            SonosDevice* member = sonos.getDevice(j);
            if (member && (j == i || member->groupCoordinatorUUID == dev->rinconID)) {
                memberCount++;
            }
        }

        bool isSelected = (selected_group_coordinator == i);
        bool isPlaying = dev->isPlaying;
        bool hasTrack = (dev->currentTrack.length() > 0);

        // Create group header button - taller to show now playing info
        lv_obj_t* btn = lv_btn_create(list_groups);
        lv_obj_set_size(btn, lv_pct(100), (isPlaying && hasTrack) ? 85 : 70);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 12, 0);

        lv_obj_set_style_bg_color(btn, isSelected ? COL_SELECTED : COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);

        if (isSelected) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, COL_ACCENT, 0);
        } else if (isPlaying) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x4ECB71), 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        // Group icon with playing indicator
        lv_obj_t* icon = lv_label_create(btn);
        if (isPlaying) {
            lv_label_set_text(icon, memberCount > 1 ? LV_SYMBOL_PLAY " " LV_SYMBOL_AUDIO LV_SYMBOL_AUDIO : LV_SYMBOL_PLAY " " LV_SYMBOL_AUDIO);
        } else {
            lv_label_set_text(icon, memberCount > 1 ? LV_SYMBOL_AUDIO LV_SYMBOL_AUDIO : LV_SYMBOL_AUDIO);
        }
        lv_obj_set_style_text_color(icon, isPlaying ? lv_color_hex(0x4ECB71) : (memberCount > 1 ? COL_ACCENT : COL_TEXT2), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, (isPlaying && hasTrack) ? -18 : -8);

        // Room name (coordinator)
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, dev->roomName.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, isPlaying ? 70 : 55, (isPlaying && hasTrack) ? -18 : -8);

        // Member count / status subtitle
        lv_obj_t* sub = lv_label_create(btn);
        if (memberCount > 1) {
            lv_label_set_text_fmt(sub, "%d speakers in group", memberCount);
        } else {
            lv_label_set_text(sub, "Standalone");
        }
        lv_obj_set_style_text_color(sub, COL_TEXT2, 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, isPlaying ? 70 : 55, (isPlaying && hasTrack) ? 2 : 12);

        // Now playing info (if playing)
        if (isPlaying && hasTrack) {
            lv_obj_t* nowPlaying = lv_label_create(btn);
            String trackInfo = dev->currentTrack;
            if (dev->currentArtist.length() > 0) {
                trackInfo += " - " + dev->currentArtist;
            }
            // Truncate if too long
            if (trackInfo.length() > 45) {
                trackInfo = trackInfo.substring(0, 42) + "...";
            }
            lv_label_set_text(nowPlaying, trackInfo.c_str());
            lv_obj_set_style_text_color(nowPlaying, lv_color_hex(0x4ECB71), 0);
            lv_obj_set_style_text_font(nowPlaying, &lv_font_montserrat_12, 0);
            lv_obj_align(nowPlaying, LV_ALIGN_LEFT_MID, 70, 22);
        }

        // Click to select this group for management
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            selected_group_coordinator = (selected_group_coordinator == idx) ? -1 : idx;
            refreshGroupsList();
        }, LV_EVENT_CLICKED, NULL);

        // Show group members as sub-items if this group is selected
        if (isSelected && memberCount > 1) {
            for (int j = 0; j < cnt; j++) {
                if (j == i) continue;  // Skip coordinator
                SonosDevice* member = sonos.getDevice(j);
                if (!member || member->groupCoordinatorUUID != dev->rinconID) continue;

                // Member item (indented)
                lv_obj_t* memBtn = lv_btn_create(list_groups);
                lv_obj_set_size(memBtn, 680, 50);
                lv_obj_set_user_data(memBtn, (void*)(intptr_t)j);
                lv_obj_set_style_radius(memBtn, 8, 0);
                lv_obj_set_style_shadow_width(memBtn, 0, 0);
                lv_obj_set_style_pad_all(memBtn, 10, 0);
                lv_obj_set_style_bg_color(memBtn, lv_color_hex(0x252525), 0);
                lv_obj_set_style_bg_color(memBtn, COL_BTN_PRESSED, LV_STATE_PRESSED);
                lv_obj_set_style_margin_left(memBtn, 40, 0);

                lv_obj_t* memIcon = lv_label_create(memBtn);
                lv_label_set_text(memIcon, LV_SYMBOL_RIGHT " " LV_SYMBOL_AUDIO);
                lv_obj_set_style_text_color(memIcon, COL_TEXT2, 0);
                lv_obj_set_style_text_font(memIcon, &lv_font_montserrat_16, 0);
                lv_obj_align(memIcon, LV_ALIGN_LEFT_MID, 5, 0);

                lv_obj_t* memLbl = lv_label_create(memBtn);
                lv_label_set_text(memLbl, member->roomName.c_str());
                lv_obj_set_style_text_color(memLbl, COL_TEXT, 0);
                lv_obj_set_style_text_font(memLbl, &lv_font_montserrat_16, 0);
                lv_obj_align(memLbl, LV_ALIGN_LEFT_MID, 60, 0);

                // Remove from group button
                lv_obj_t* removeBtn = lv_btn_create(memBtn);
                lv_obj_set_size(removeBtn, 90, 35);
                lv_obj_align(removeBtn, LV_ALIGN_RIGHT_MID, -5, 0);
                lv_obj_set_style_bg_color(removeBtn, lv_color_hex(0x8B0000), 0);
                lv_obj_set_style_radius(removeBtn, 8, 0);
                lv_obj_set_user_data(removeBtn, (void*)(intptr_t)j);

                lv_obj_t* removeLbl = lv_label_create(removeBtn);
                lv_label_set_text(removeLbl, "Remove");
                lv_obj_set_style_text_color(removeLbl, COL_TEXT, 0);
                lv_obj_set_style_text_font(removeLbl, &lv_font_montserrat_14, 0);
                lv_obj_center(removeLbl);

                lv_obj_add_event_cb(removeBtn, [](lv_event_t* e) {
                    lv_event_stop_bubbling(e);
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    sonos.leaveGroup(idx);
                    lv_label_set_text(lbl_groups_status, "Removing from group...");
                    lv_timer_handler();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    sonos.updateGroupInfo();
                    refreshGroupsList();
                }, LV_EVENT_CLICKED, NULL);
            }
        }
    }

    // If a group is selected, show standalone speakers that can be added
    if (selected_group_coordinator >= 0) {
        SonosDevice* coordinator = sonos.getDevice(selected_group_coordinator);
        if (coordinator) {
            // Header for available speakers
            lv_obj_t* hdr = lv_obj_create(list_groups);
            lv_obj_set_size(hdr, 720, 40);
            lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_border_width(hdr, 0, 0);
            lv_obj_set_style_pad_all(hdr, 10, 0);
            lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* hdrLbl = lv_label_create(hdr);
            lv_label_set_text_fmt(hdrLbl, "Add speakers to \"%s\":", coordinator->roomName.c_str());
            lv_obj_set_style_text_color(hdrLbl, COL_ACCENT, 0);
            lv_obj_set_style_text_font(hdrLbl, &lv_font_montserrat_16, 0);
            lv_obj_align(hdrLbl, LV_ALIGN_LEFT_MID, 0, 0);

            // Show standalone speakers (not in any group except their own)
            for (int i = 0; i < cnt; i++) {
                if (i == selected_group_coordinator) continue;
                SonosDevice* dev = sonos.getDevice(i);
                if (!dev) continue;

                // Skip if already in the selected group
                if (dev->groupCoordinatorUUID == coordinator->rinconID) continue;

                // Only show if standalone (is own coordinator)
                if (!dev->isGroupCoordinator) continue;

                lv_obj_t* addBtn = lv_btn_create(list_groups);
                lv_obj_set_size(addBtn, 720, 55);
                lv_obj_set_user_data(addBtn, (void*)(intptr_t)i);
                lv_obj_set_style_radius(addBtn, 10, 0);
                lv_obj_set_style_shadow_width(addBtn, 0, 0);
                lv_obj_set_style_pad_all(addBtn, 10, 0);
                lv_obj_set_style_bg_color(addBtn, lv_color_hex(0x1E3A1E), 0);  // Dark green hint
                lv_obj_set_style_bg_color(addBtn, lv_color_hex(0x2A5A2A), LV_STATE_PRESSED);

                lv_obj_t* addIcon = lv_label_create(addBtn);
                lv_label_set_text(addIcon, LV_SYMBOL_PLUS " " LV_SYMBOL_AUDIO);
                lv_obj_set_style_text_color(addIcon, lv_color_hex(0x4ECB71), 0);
                lv_obj_set_style_text_font(addIcon, &lv_font_montserrat_18, 0);
                lv_obj_align(addIcon, LV_ALIGN_LEFT_MID, 5, 0);

                lv_obj_t* addLbl = lv_label_create(addBtn);
                lv_label_set_text_fmt(addLbl, "Add %s", dev->roomName.c_str());
                lv_obj_set_style_text_color(addLbl, COL_TEXT, 0);
                lv_obj_set_style_text_font(addLbl, &lv_font_montserrat_16, 0);
                lv_obj_align(addLbl, LV_ALIGN_LEFT_MID, 60, 0);

                lv_obj_add_event_cb(addBtn, [](lv_event_t* e) {
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    if (selected_group_coordinator >= 0) {
                        sonos.joinGroup(idx, selected_group_coordinator);
                        lv_label_set_text(lbl_groups_status, "Adding to group...");
                        lv_timer_handler();
                        vTaskDelay(pdMS_TO_TICKS(500));
                        sonos.updateGroupInfo();
                        refreshGroupsList();
                    }
                }, LV_EVENT_CLICKED, NULL);
            }
        }
    }
}

void createGroupsScreen() {
    scr_groups = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_groups, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Groups is index 1)
    lv_obj_t* content = createSettingsSidebar(scr_groups, 1);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title + Refresh button row
    lv_obj_t* title_row = lv_obj_create(content);
    lv_obj_set_size(title_row, lv_pct(100), 40);
    lv_obj_set_pos(title_row, 0, 0);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(title_row);
    lv_label_set_text(lbl_title, "Groups");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    btn_groups_scan = lv_button_create(title_row);
    lv_obj_set_size(btn_groups_scan, 110, 40);
    lv_obj_align(btn_groups_scan, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_groups_scan, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_groups_scan, 20, 0);
    lv_obj_set_style_shadow_width(btn_groups_scan, 0, 0);
    lv_obj_add_event_cb(btn_groups_scan, [](lv_event_t* e) {
        // Disable button during scan
        lv_obj_add_state(btn_groups_scan, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_groups_scan, lv_color_hex(0x555555), LV_STATE_DISABLED);

        // Show spinner
        if (spinner_groups_scan) {
            lv_obj_remove_flag(spinner_groups_scan, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(spinner_groups_scan);
        }

        // If no speakers discovered yet, run speaker discovery first
        if (sonos.getDeviceCount() == 0) {
            lv_label_set_text(lbl_groups_status, LV_SYMBOL_REFRESH " Discovering speakers...");
            lv_refr_now(NULL);  // Force immediate refresh to show spinner
            sonos.discoverDevices();
        }

        // Now update group info
        lv_label_set_text(lbl_groups_status, LV_SYMBOL_REFRESH " Updating groups...");
        lv_refr_now(NULL);  // Force immediate refresh

        // Update group info with UI updates
        int cnt = sonos.getDeviceCount();
        for (int i = 0; i < cnt; i++) {
            lv_tick_inc(10);
            lv_timer_handler();
            lv_refr_now(NULL);
        }
        sonos.updateGroupInfo();
        refreshGroupsList();

        // Hide spinner and re-enable button
        if (spinner_groups_scan) {
            lv_obj_add_flag(spinner_groups_scan, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_state(btn_groups_scan, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_groups_scan, COL_ACCENT, 0);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_scan = lv_label_create(btn_groups_scan);
    lv_label_set_text(lbl_scan, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_scan, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_scan);

    // Status label
    lbl_groups_status = lv_label_create(content);
    lv_obj_set_pos(lbl_groups_status, 0, 50);
    lv_label_set_text(lbl_groups_status, "Tap a group to manage it");
    lv_obj_set_style_text_color(lbl_groups_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_groups_status, &lv_font_montserrat_12, 0);

    // Groups list
    list_groups = lv_obj_create(content);
    lv_obj_set_size(list_groups, lv_pct(100), 380);
    lv_obj_set_pos(list_groups, 0, 75);
    lv_obj_set_style_bg_color(list_groups, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_groups, 0, 0);
    lv_obj_set_style_radius(list_groups, 0, 0);
    lv_obj_set_style_pad_all(list_groups, 0, 0);
    lv_obj_set_style_pad_row(list_groups, 6, 0);
    lv_obj_set_flex_flow(list_groups, LV_FLEX_FLOW_COLUMN);

    // Scrollbar styling
    lv_obj_set_style_pad_right(list_groups, 8, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_groups, LV_OPA_30, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_groups, COL_TEXT2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_groups, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_groups, 3, LV_PART_SCROLLBAR);

    // Spinner for scan feedback (centered in content area, hidden by default)
    spinner_groups_scan = lv_spinner_create(content);
    lv_obj_set_size(spinner_groups_scan, 100, 100);
    lv_obj_center(spinner_groups_scan);
    lv_obj_set_style_arc_color(spinner_groups_scan, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_groups_scan, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner_groups_scan, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner_groups_scan, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(spinner_groups_scan, true, LV_PART_INDICATOR);
    lv_obj_move_foreground(spinner_groups_scan);
    lv_obj_add_flag(spinner_groups_scan, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================================
// Queue Screen
// ============================================================================
void refreshQueueList() {
    lv_obj_clean(list_queue);
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) { lv_label_set_text(lbl_queue_status, "No device"); return; }
    if (d->queueSize == 0) { lv_label_set_text(lbl_queue_status, "Queue is empty"); return; }
    lv_label_set_text_fmt(lbl_queue_status, "%d %s in queue", d->queueSize, d->queueSize == 1 ? "track" : "tracks");

    for (int i = 0; i < d->queueSize; i++) {
        QueueItem* item = &d->queue[i];
        int trackNum = i + 1;
        bool isPlaying = (trackNum == d->currentTrackNumber);

        lv_obj_t* btn = lv_btn_create(list_queue);
        lv_obj_set_size(btn, 727, 60);  // Full width, uniform height
        lv_obj_set_style_bg_color(btn, isPlaying ? lv_color_hex(0x252525) : lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 0, 0);  // No rounded corners - clean list
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 12, 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)trackNum);
        lv_obj_add_event_cb(btn, ev_queue_item, LV_EVENT_CLICKED, NULL);

        // Subtle left border for currently playing
        if (isPlaying) {
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_width(btn, 3, 0);
            lv_obj_set_style_border_color(btn, COL_ACCENT, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        // Play icon for currently playing track OR track number
        lv_obj_t* num = lv_label_create(btn);
        if (isPlaying) {
            lv_label_set_text(num, LV_SYMBOL_PLAY);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_18, 0);
        } else {
            lv_label_set_text_fmt(num, "%d", trackNum);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_14, 0);
        }
        lv_obj_set_style_text_color(num, isPlaying ? COL_ACCENT : COL_TEXT2, 0);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 5, 0);

        // Title - highlight when playing
        lv_obj_t* title = lv_label_create(btn);
        lv_label_set_text(title, item->title.c_str());
        lv_obj_set_style_text_color(title, isPlaying ? COL_ACCENT : COL_TEXT, 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_width(title, 610);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 45, -11);

        // Artist - subtle gray
        lv_obj_t* artist = lv_label_create(btn);
        lv_label_set_text(artist, item->artist.c_str());
        lv_obj_set_style_text_color(artist, COL_TEXT2, 0);
        lv_obj_set_style_text_font(artist, &lv_font_montserrat_12, 0);
        lv_obj_set_width(artist, 610);
        lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
        lv_obj_align(artist, LV_ALIGN_LEFT_MID, 45, 11);
    }
}

void createQueueScreen() {
    scr_queue = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_queue, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_queue);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Playlist");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Refresh button in header
    lv_obj_t* btn_refresh = lv_button_create(header);
    lv_obj_set_size(btn_refresh, 50, 50);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, -80, 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_refresh, 25, 0);
    lv_obj_set_style_shadow_width(btn_refresh, 0, 0);
    lv_obj_add_event_cb(btn_refresh, [](lv_event_t* e) { sonos.updateQueue(); refreshQueueList(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(ico_refresh, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(ico_refresh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_refresh, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_refresh);

    // Close button in header
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Status label below header
    lbl_queue_status = lv_label_create(scr_queue);
    lv_obj_align(lbl_queue_status, LV_ALIGN_TOP_LEFT, 40, 85);
    lv_label_set_text(lbl_queue_status, "Loading...");
    lv_obj_set_style_text_color(lbl_queue_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_queue_status, &lv_font_montserrat_14, 0);

    // Queue list - modern clean design
    list_queue = lv_list_create(scr_queue);
    lv_obj_set_size(list_queue, 730, 360);
    lv_obj_set_pos(list_queue, 35, 115);
    lv_obj_set_style_bg_color(list_queue, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_queue, 0, 0);
    lv_obj_set_style_radius(list_queue, 0, 0);
    lv_obj_set_style_pad_all(list_queue, 0, 0);
    lv_obj_set_style_pad_row(list_queue, 0, 0);  // No spacing between items

    // Modern thin scrollbar on the right edge
    lv_obj_set_style_pad_right(list_queue, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_queue, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_queue, COL_ACCENT, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_queue, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_queue, 0, LV_PART_SCROLLBAR);
}

// ============================================================================
// Settings Screen (just redirects to Speakers)
// ============================================================================
void createSettingsScreen() {
    // Settings screen just redirects to Speakers screen (which has the sidebar)
    // scr_settings will point to scr_devices so clicking Settings button loads Speakers
    if (!scr_devices) {
        createDevicesScreen();
    }
    scr_settings = scr_devices;  // Point to the same screen
}

// ============================================================================
// Display Settings Screen
// ============================================================================
void createDisplaySettingsScreen() {
    scr_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_display, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Display is index 3)
    lv_obj_t* content = createSettingsSidebar(scr_display, 3);
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

// ============================================================================
// WiFi Screen
// ============================================================================
void createWiFiScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (WiFi is index 4)
    lv_obj_t* content = createSettingsSidebar(scr_wifi, 4);
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

// ============================================================================
// Sources Screen
// ============================================================================
void createSourcesScreen() {
    scr_sources = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sources, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Sources is index 2)
    lv_obj_t* content = createSettingsSidebar(scr_sources, 2);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, "Sources");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

    // Scrollable list
    lv_obj_t* list = lv_obj_create(content);
    lv_obj_set_pos(list, 0, 50);
    lv_obj_set_size(list, lv_pct(100), 405);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 8, 0);

    // Music source items
    struct MusicSource {
        const char* name;
        const char* icon;
        const char* objectID;
    };

    MusicSource sources[] = {
        {"Sonos Favorites", LV_SYMBOL_DIRECTORY, "FV:2"},
        {"Sonos Playlists", LV_SYMBOL_LIST, "SQ:"}
    };

    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, lv_pct(100), 50);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 15, 0);
        lv_obj_set_user_data(btn, (void*)sources[i].objectID);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, sources[i].icon);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, sources[i].name);
        lv_obj_set_style_text_color(name, COL_TEXT, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn_target = (lv_obj_t*)lv_event_get_target(e);
            const char* objID = (const char*)lv_obj_get_user_data(btn_target);
            lv_obj_t* label = lv_obj_get_child(btn_target, 1);
            const char* title = lv_label_get_text(label);

            current_browse_id = String(objID);
            current_browse_title = String(title);

            createBrowseScreen();
            lv_screen_load(scr_browse);
        }, LV_EVENT_CLICKED, NULL);
    }
}

// ============================================================================
// Browse Screen
// ============================================================================
void cleanupBrowseData(lv_obj_t* list) {
    if (!list) return;
    uint32_t child_count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(list, i);
        if (child) {
            void* data = lv_obj_get_user_data(child);
            if (data) {
                heap_caps_free(data);
                lv_obj_set_user_data(child, NULL);
            }
        }
    }
}

void createBrowseScreen() {
    if (scr_browse) {
        lv_obj_t* list = lv_obj_get_child(scr_browse, -1);
        cleanupBrowseData(list);
        lv_obj_del(scr_browse);
    }

    scr_browse = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_browse, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Sources is index 2)
    lv_obj_t* content = createSettingsSidebar(scr_browse, 2);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, current_browse_title.c_str());
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

    // Content list
    lv_obj_t* list = lv_obj_create(content);
    lv_obj_set_pos(list, 0, 50);
    lv_obj_set_size(list, lv_pct(100), 405);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 10, 0);

    String didl = sonos.browseContent(current_browse_id.c_str());

    Serial.printf("[BROWSE] ID=%s, DIDL length=%d\n", current_browse_id.c_str(), didl.length());

    if (didl.length() == 0) {
        lv_obj_t* lbl_empty = lv_label_create(list);
        lv_label_set_text(lbl_empty, "No items found");
        lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
        return;
    }

    int searchPos = 0;
    int itemCount = 0;

    while (searchPos < (int)didl.length()) {
        int containerPos = didl.indexOf("<container", searchPos);
        int itemPos = didl.indexOf("<item", searchPos);

        if (containerPos < 0 && itemPos < 0) break;

        bool isContainer = false;
        if (containerPos >= 0 && (itemPos < 0 || containerPos < itemPos)) {
            searchPos = containerPos;
            isContainer = true;
        } else if (itemPos >= 0) {
            searchPos = itemPos;
            isContainer = false;
        } else {
            break;
        }

        int endPos = isContainer ? didl.indexOf("</container>", searchPos) : didl.indexOf("</item>", searchPos);
        if (endPos < 0) break;

        String itemXML = didl.substring(searchPos, endPos + (isContainer ? 12 : 7));
        String title = sonos.extractXML(itemXML, "dc:title");

        int idStart = itemXML.indexOf("id=\"") + 4;
        int idEnd = itemXML.indexOf("\"", idStart);
        String id = itemXML.substring(idStart, idEnd);

        Serial.printf("[BROWSE] Item #%d: %s (container=%d, id=%s)\n",
                      itemCount, title.c_str(), isContainer, id.c_str());

        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, lv_pct(100), 60);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 15, 0);

        struct ItemData {
            char id[128];
            char itemXML[2048];  // Increased for full DIDL-Lite metadata with r:resMD
            bool isContainer;
        };
        ItemData* data = (ItemData*)heap_caps_malloc(sizeof(ItemData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!data) {
            Serial.println("[BROWSE] PSRAM malloc failed, trying regular heap...");
            data = (ItemData*)malloc(sizeof(ItemData));
            if (!data) {
                Serial.println("[BROWSE] Regular malloc also failed!");
                break;
            }
        }
        strncpy(data->id, id.c_str(), sizeof(data->id) - 1);
        data->id[sizeof(data->id) - 1] = '\0';

        if (itemXML.length() >= sizeof(data->itemXML)) {
            itemXML = itemXML.substring(0, sizeof(data->itemXML) - 1);
        }
        strncpy(data->itemXML, itemXML.c_str(), sizeof(data->itemXML) - 1);
        data->itemXML[sizeof(data->itemXML) - 1] = '\0';
        data->isContainer = isContainer;
        lv_obj_set_user_data(btn, data);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, isContainer ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, title.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 40, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lbl, lv_pct(90));

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            struct ItemData {
                char id[128];
                char itemXML[2048];
                bool isContainer;
            };
            ItemData* data = (ItemData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            String itemXML = String(data->itemXML);
            String id = String(data->id);

            String uri = sonos.extractXML(itemXML, "res");
            uri = sonos.decodeHTML(uri);

            if (data->isContainer) {
                if (id.startsWith("SQ:") && id.indexOf("/") < 0) {
                    String title = sonos.extractXML(itemXML, "dc:title");
                    Serial.printf("[BROWSE] Playing playlist: %s (ID: %s)\n", title.c_str(), id.c_str());
                    sonos.playPlaylist(id.c_str());
                    lv_screen_load(scr_main);
                } else {
                    current_browse_id = id;
                    current_browse_title = sonos.extractXML(itemXML, "dc:title");
                    createBrowseScreen();
                    lv_screen_load(scr_browse);
                }
            } else {

                if (uri.length() == 0) {
                    String resMD = sonos.extractXML(itemXML, "r:resMD");
                    if (resMD.length() > 0) {
                        resMD = sonos.decodeHTML(resMD);

                        if (resMD.indexOf("<upnp:class>object.container</upnp:class>") >= 0) {
                            int idStart = resMD.indexOf("id=\"") + 4;
                            int idEnd = resMD.indexOf("\"", idStart);
                            String containerID = resMD.substring(idStart, idEnd);
                            current_browse_id = containerID;
                            current_browse_title = sonos.extractXML(resMD, "dc:title");
                            Serial.printf("[BROWSE] Shortcut to container: %s\n", containerID.c_str());
                            createBrowseScreen();
                            lv_screen_load(scr_browse);
                            return;
                        }

                        uri = sonos.extractXML(resMD, "res");
                    }
                }

                if (uri.startsWith("x-rincon-cpcontainer:")) {
                    String title = sonos.extractXML(itemXML, "dc:title");
                    Serial.printf("[BROWSE] Playing container: %s\n", title.c_str());

                    // Extract inner DIDL-Lite from r:resMD tag
                    String resMD = sonos.extractXML(itemXML, "r:resMD");
                    if (resMD.length() > 0) {
                        resMD = sonos.decodeHTML(resMD);

                        // Extract <res> tag from outer item and inject into inner DIDL
                        String resTag = sonos.extractXML(itemXML, "res");
                        String protocolInfo = "";
                        int protoStart = itemXML.indexOf("protocolInfo=\"");
                        if (protoStart > 0) {
                            int protoEnd = itemXML.indexOf("\"", protoStart + 14);
                            if (protoEnd > protoStart) {
                                protocolInfo = itemXML.substring(protoStart + 14, protoEnd);
                            }
                        }

                        // Build complete <res> element
                        String resElement = "<res protocolInfo=\"" + protocolInfo + "\">" + uri + "</res>";

                        // Insert <res> into the inner DIDL's <item> (after <upnp:class>)
                        int insertPos = resMD.indexOf("</upnp:class>") + 13;
                        if (insertPos > 13) {
                            resMD = resMD.substring(0, insertPos) + resElement + resMD.substring(insertPos);
                        }

                        Serial.printf("[BROWSE] Enhanced inner DIDL with <res> tag (%d bytes)\n", resMD.length());
                        sonos.playContainer(uri.c_str(), resMD.c_str());
                    } else {
                        Serial.println("[BROWSE] No r:resMD found, using full itemXML");
                        sonos.playContainer(uri.c_str(), itemXML.c_str());
                    }
                    lv_screen_load(scr_main);
                } else if (uri.length() > 0) {
                    Serial.printf("[BROWSE] Playing URI: %s\n", uri.c_str());
                    sonos.playURI(uri.c_str(), itemXML.c_str());
                    lv_screen_load(scr_main);
                } else {
                    Serial.println("[BROWSE] No URI found!");
                }
            }
        }, LV_EVENT_CLICKED, NULL);

        searchPos = endPos + (isContainer ? 12 : 7);
        itemCount++;
        if (itemCount >= 20) {
            Serial.printf("[BROWSE] Reached 20 item limit, stopping\n");
            break;
        }
    }

    if (itemCount == 0) {
        lv_obj_t* lbl_empty = lv_label_create(list);
        lv_label_set_text(lbl_empty, "No items found");
        lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
    }

    Serial.printf("[BROWSE] Created %d items, free heap: %d bytes\n", itemCount, esp_get_free_heap_size());
}

