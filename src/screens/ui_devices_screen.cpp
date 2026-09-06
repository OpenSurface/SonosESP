/**
 * Devices (Speakers) Settings Screen
 * Shows discovered Sonos devices, group status, and scan functionality
 */

#include "ui_common.h"
#include "ui_settings_card.h"   // addScreenHeader() - shared title row
#include "ui_fonts.h"
#include "amber_icons.h"
#include "amber.h"

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
            if (member && SonosController::uuidEquals(member->groupCoordinatorUUID,
                                                      dev->rinconID)) {
                memberCount++;
            }
        }

        bool isSelected = (current && dev->ip == current->ip);
        bool isPlaying = dev->isPlaying;
        bool hasGroup = (memberCount > 1);

        // Create main button - taller if it has subtitle
        lv_obj_t* btn = lv_btn_create(list_devices);
        // Taller than before: the row now carries an inline volume slider under
        // its label block, which the canvas puts on every speaker row.
        lv_obj_set_size(btn, lv_pct(100), hasGroup || isPlaying ? SY(96) : SY(86));
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, SMIN(12), 0);

        lv_obj_set_style_bg_color(btn, isSelected ? AMB_BORDER : AMB_CARD, 0);
        lv_obj_set_style_bg_color(btn, AMB_BORDER, LV_STATE_PRESSED);

        if (isSelected) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, AMB_ACCENT, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        // Speaker icon - show double icon for groups
        lv_obj_t* icon = lv_label_create(btn);
        if (hasGroup) {
            lv_label_set_text(icon, AMB_IC_GROUPS);
        } else {
            lv_label_set_text(icon, AMB_IC_SPEAKER);
        }
        lv_obj_set_style_text_color(icon, isPlaying ? AMB_ACCENT : (isSelected ? AMB_ACCENT : AMB_TEXT3), 0);
        lv_obj_set_style_text_font(icon, &font_icon_24, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, SX(5), hasGroup || isPlaying ? SY(6) : SY(10));

        // Room name
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, dev->roomName.c_str());
        lv_obj_set_style_text_color(lbl, AMB_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &font_text_20, 0);
        // Cap + ellipsize: these labels had no width limit at all, so a long
        // room name ran under the chevron. Generous — only bites past ~40 chars.
        // Clears the volume + chevron column on the right, in the 536-wide inner
        // content box left by the 216 rail.
        lv_obj_set_width(lbl, SX(360));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, hasGroup ? SX(55) : SX(45), hasGroup || isPlaying ? SY(4) : SY(8));

        // Subtitle: group info or playing status
        if (hasGroup || isPlaying) {
            lv_obj_t* sub = lv_label_create(btn);
            if (hasGroup && isPlaying) {
                lv_label_set_text_fmt(sub, AMB_IC_PLAY " Playing  " AMB_IC_SPEAKER " +%d speakers", memberCount - 1);
            } else if (hasGroup) {
                lv_label_set_text_fmt(sub, AMB_IC_SPEAKER " +%d speaker%s", memberCount - 1, memberCount > 2 ? "s" : "");
            } else {
                lv_label_set_text(sub, AMB_IC_PLAY " Playing");
            }
            lv_obj_set_style_text_color(sub, isPlaying ? AMB_LIVE : AMB_TEXT3, 0);
            lv_obj_set_style_text_font(sub, &font_icon_16, 0);
            lv_obj_align(sub, LV_ALIGN_TOP_LEFT, hasGroup ? SX(55) : SX(45), SY(28));
        }

        // Volume is only POLLED for the selected speaker — every other device holds
        // the placeholder 50 written at discovery. Showing that as a level would
        // be inventing a number, so the readout and the slider below appear for
        // the selected row only. Tapping any other row selects it, at which point
        // its volume is real and both appear.
        lv_obj_t* vol = lv_label_create(btn);
        lv_label_set_text_fmt(vol, "%d", dev->volume);
        if (!isSelected) lv_obj_add_flag(vol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(vol, AMB_TEXT3, 0);
        lv_obj_set_style_text_font(vol, &font_text_14, 0);
        lv_obj_align(vol, LV_ALIGN_TOP_RIGHT, SX(-32), hasGroup || isPlaying ? SY(10) : SY(14));

        // Right arrow indicator
        lv_obj_t* arrow = lv_label_create(btn);
        lv_label_set_text(arrow, AMB_IC_CHEV);
        lv_obj_set_style_text_color(arrow, AMB_TEXT3, 0);
        lv_obj_set_style_text_font(arrow, &font_icon_24, 0);
        lv_obj_align(arrow, LV_ALIGN_TOP_RIGHT, SX(-5), hasGroup || isPlaying ? SY(8) : SY(12));

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            sonos.selectDevice(idx);
            sonos.startTasks();
            lv_screen_load(scr_main);
        }, LV_EVENT_CLICKED, NULL);

        // ── Inline volume ───────────────────────────────────────────────────
        // A child of the row button, so it takes the drag before the row's
        // CLICKED handler sees it — tapping the row still selects the speaker,
        // dragging the slider does not.
        lv_obj_t* vol_sl = lv_slider_create(btn);
        lv_obj_set_size(vol_sl, lv_pct(88), SY(6));
        lv_obj_align(vol_sl, LV_ALIGN_BOTTOM_LEFT, SX(5), SY(-10));
        lv_slider_set_range(vol_sl, 0, 100);
        lv_slider_set_value(vol_sl, dev->volume, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(vol_sl, AMB_GROOVE, LV_PART_MAIN);
        lv_obj_set_style_radius(vol_sl, SMIN(3), LV_PART_MAIN);
        lv_obj_set_style_bg_color(vol_sl, isSelected ? AMB_ACCENT : AMB_TEXT3, LV_PART_INDICATOR);
        lv_obj_set_style_radius(vol_sl, SMIN(3), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(vol_sl, isSelected ? AMB_ACCENT : AMB_TEXT3, LV_PART_KNOB);
        lv_obj_set_style_pad_all(vol_sl, SMIN(6), LV_PART_KNOB);
        lv_obj_set_user_data(vol_sl, vol);          // the number to keep in step
        // Not just hidden — a hidden slider still cannot be pressed, but leaving
        // it out of the layout entirely is what makes the intent obvious.
        if (!isSelected) lv_obj_add_flag(vol_sl, LV_OBJ_FLAG_HIDDEN);

        // Live while dragging so the number tracks the knob...
        lv_obj_add_event_cb(vol_sl, [](lv_event_t* e) {
            lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
            lv_obj_t* n = (lv_obj_t*)lv_obj_get_user_data(s);
            if (n) lv_label_set_text_fmt(n, "%d", (int)lv_slider_get_value(s));
        }, LV_EVENT_VALUE_CHANGED, NULL);

        // ...but the SOAP call only on release. VALUE_CHANGED fires per pixel of
        // travel and would queue a command for every one of them.
        lv_obj_add_event_cb(vol_sl, [](lv_event_t* e) {
            lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            sonos.setDeviceVolume(idx, lv_slider_get_value(s));
        }, LV_EVENT_RELEASED, (void*)(intptr_t)i);

        // Show group members as indented sub-items
        if (hasGroup) {
            for (int j = 0; j < cnt; j++) {
                if (j == i) continue;
                SonosDevice* member = sonos.getDevice(j);
                if (!member ||
                    !SonosController::uuidEquals(member->groupCoordinatorUUID, dev->rinconID)) continue;

                lv_obj_t* memBtn = lv_btn_create(list_devices);
                lv_obj_set_size(memBtn, lv_pct(95), SY(50));
                lv_obj_set_user_data(memBtn, (void*)(intptr_t)j);
                lv_obj_set_style_radius(memBtn, 8, 0);
                lv_obj_set_style_shadow_width(memBtn, 0, 0);
                lv_obj_set_style_pad_all(memBtn, SMIN(10), 0);
                lv_obj_set_style_bg_color(memBtn, AMB_RAISED, 0);
                lv_obj_set_style_bg_color(memBtn, AMB_BORDER, LV_STATE_PRESSED);
                lv_obj_set_style_margin_left(memBtn, SX(40), 0);

                // Linking icon
                lv_obj_t* memIcon = lv_label_create(memBtn);
                lv_label_set_text(memIcon, AMB_IC_CHEV " " AMB_IC_SPEAKER);
                lv_obj_set_style_text_color(memIcon, AMB_TEXT3, 0);
                lv_obj_set_style_text_font(memIcon, &font_icon_16, 0);
                lv_obj_align(memIcon, LV_ALIGN_LEFT_MID, SX(5), 0);

                lv_obj_t* memLbl = lv_label_create(memBtn);
                lv_label_set_text(memLbl, member->roomName.c_str());
                lv_obj_set_style_text_color(memLbl, AMB_TEXT, 0);
                lv_obj_set_style_text_font(memLbl, &font_text_16, 0);
                lv_obj_align(memLbl, LV_ALIGN_LEFT_MID, SX(55), 0);

                // "Grouped" badge
                lv_obj_t* badge = lv_label_create(memBtn);
                lv_label_set_text(badge, "Grouped");
                lv_obj_set_style_text_color(badge, AMB_TEXT3, 0);
                lv_obj_set_style_text_font(badge, &font_text_12, 0);
                lv_obj_align(badge, LV_ALIGN_RIGHT_MID, SX(-10), 0);

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
            if (coord && SonosController::uuidEquals(coord->rinconID,
                                                     dev->groupCoordinatorUUID)) {
                coordinatorFound = true;
                break;
            }
        }

        // If coordinator not found, show as standalone
        if (!coordinatorFound) {
            bool isSelected = (current && dev->ip == current->ip);

            lv_obj_t* btn = lv_btn_create(list_devices);
            lv_obj_set_size(btn, lv_pct(100), SY(60));
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_set_style_radius(btn, 12, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, SMIN(15), 0);
            lv_obj_set_style_bg_color(btn, isSelected ? AMB_BORDER : AMB_CARD, 0);

            lv_obj_t* icon = lv_label_create(btn);
            lv_label_set_text(icon, AMB_IC_SPEAKER);
            lv_obj_set_style_text_color(icon, AMB_TEXT3, 0);
            lv_obj_set_style_text_font(icon, &font_icon_24, 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, SX(5), 0);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, dev->roomName.c_str());
            lv_obj_set_style_text_color(lbl, AMB_TEXT, 0);
            lv_obj_set_style_text_font(lbl, &font_text_20, 0);
            lv_obj_set_width(lbl, SX(430));
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SX(40), 0);

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
    lv_obj_set_style_bg_color(scr_devices, AMB_BG, 0);

    // Create sidebar and get content area (Speakers is index 1)
    lv_obj_t* content = createSettingsSidebar(scr_devices, 1);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title + Scan button row
    btn_sonos_scan = addScreenHeader(content, "Speakers", AMB_IC_REFRESH " Scan");
    lv_obj_add_event_cb(btn_sonos_scan, ev_discover, LV_EVENT_CLICKED, NULL);

    // Status label
    lbl_status = lv_label_create(content);
    lv_obj_set_pos(lbl_status, 0, SY(50));
    lv_label_set_text(lbl_status, "Tap Scan to find speakers");
    lv_obj_set_style_text_color(lbl_status, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_status, &font_icon_16, 0);

    // Devices list
    list_devices = lv_list_create(content);
    lv_obj_set_size(list_devices, lv_pct(100), SETTINGS_LIST_H(75));
    lv_obj_set_pos(list_devices, 0, SY(75));
    lv_obj_set_style_bg_color(list_devices, AMB_PANEL, 0);
    lv_obj_set_style_border_width(list_devices, 0, 0);
    lv_obj_set_style_radius(list_devices, 0, 0);
    lv_obj_set_style_pad_all(list_devices, 0, 0);
    lv_obj_set_style_pad_row(list_devices, SY(6), 0);

    // Professional scrollbar styling
    lv_obj_set_style_pad_right(list_devices, SX(8), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_devices, LV_OPA_30, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_devices, AMB_TEXT3, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_devices, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_devices, 3, LV_PART_SCROLLBAR);

    // Spinner for scan feedback (centered in content area, hidden by default)
    spinner_scan = lv_spinner_create(content);
    lv_obj_set_size(spinner_scan, SMIN(100), SMIN(100));
    lv_obj_center(spinner_scan);
    lv_obj_set_style_arc_color(spinner_scan, AMB_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_scan, AMB_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner_scan, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner_scan, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(spinner_scan, true, LV_PART_INDICATOR);
    lv_obj_move_foreground(spinner_scan);  // Ensure it's on top
    lv_obj_add_flag(spinner_scan, LV_OBJ_FLAG_HIDDEN);  // Hidden by default

    // Refresh list every time the screen is opened so cached/already-discovered
    // speakers show immediately without requiring a manual Scan tap (issue #19).
    lv_obj_add_event_cb(scr_devices, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) return;
        int cnt = sonos.getDeviceCount();
        if (cnt > 0) {
            refreshDeviceList();
            int playing = 0;
            for (int i = 0; i < cnt; i++) {
                SonosDevice* d = sonos.getDevice(i);
                if (d && d->isPlaying) playing++;
            }
            lv_label_set_text_fmt(lbl_status, "%d speaker%s found · %d playing",
                                  cnt, cnt == 1 ? "" : "s", playing);
        } else {
            lv_label_set_text(lbl_status, "Tap Scan to find speakers");
        }
    }, LV_EVENT_ALL, NULL);
}
