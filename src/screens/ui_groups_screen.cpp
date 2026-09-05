/**
 * Groups Settings Screen
 * Manages Sonos speaker groups (join/leave operations)
 */

#include "ui_common.h"
#include "ui_settings_card.h"   // addScreenHeader() - shared title row
#include "ui_fonts.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

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
            if (member && (j == i ||
                SonosController::uuidEquals(member->groupCoordinatorUUID, dev->rinconID))) {
                memberCount++;
            }
        }

        bool isSelected = (selected_group_coordinator == i);
        bool isPlaying = dev->isPlaying;
        bool hasTrack = (dev->currentTrack.length() > 0);

        // Create group header button - taller to show now playing info
        lv_obj_t* btn = lv_btn_create(list_groups);
        lv_obj_set_size(btn, lv_pct(100), (isPlaying && hasTrack) ? SY(85) : SY(70));
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, SMIN(12), 0);

        lv_obj_set_style_bg_color(btn, isSelected ? COL_SELECTED : COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);

        if (isSelected) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, COL_ACCENT, 0);
        } else if (isPlaying) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, COL_OK, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        // Group icon with playing indicator
        lv_obj_t* icon = lv_label_create(btn);
        if (isPlaying) {
            lv_label_set_text(icon, memberCount > 1 ? MDI_PLAY " " MDI_SPEAKER_MULTIPLE : MDI_PLAY " " MDI_SPEAKER);
        } else {
            lv_label_set_text(icon, memberCount > 1 ? MDI_SPEAKER_MULTIPLE : MDI_SPEAKER);
        }
        lv_obj_set_style_text_color(icon, isPlaying ? COL_OK : (memberCount > 1 ? COL_ACCENT : COL_TEXT2), 0);
        lv_obj_set_style_text_font(icon, &lv_font_mdi_24, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, SX(5), (isPlaying && hasTrack) ? SY(-18) : SY(-8));

        // Room name (coordinator)
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, dev->roomName.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &font_text_20, 0);
        // Cap + ellipsize — the Remove button sits at this row's right edge.
        lv_obj_set_width(lbl, SX(400));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, isPlaying ? SX(70) : SX(55), (isPlaying && hasTrack) ? SY(-18) : SY(-8));

        // Member count / status subtitle
        lv_obj_t* sub = lv_label_create(btn);
        if (memberCount > 1) {
            lv_label_set_text_fmt(sub, "%d speakers in group", memberCount);
        } else {
            lv_label_set_text(sub, "Standalone");
        }
        lv_obj_set_style_text_color(sub, COL_TEXT2, 0);
        lv_obj_set_style_text_font(sub, &font_text_14, 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, isPlaying ? SX(70) : SX(55), (isPlaying && hasTrack) ? SY(2) : SY(12));

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
            lv_obj_set_style_text_color(nowPlaying, COL_OK, 0);
            lv_obj_set_style_text_font(nowPlaying, &font_text_12, 0);
            lv_obj_align(nowPlaying, LV_ALIGN_LEFT_MID, SX(70), SY(22));
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
                if (!member ||
                    !SonosController::uuidEquals(member->groupCoordinatorUUID, dev->rinconID)) continue;

                // Member item (indented)
                lv_obj_t* memBtn = lv_btn_create(list_groups);
                lv_obj_set_size(memBtn, SX(680), SY(50));
                lv_obj_set_user_data(memBtn, (void*)(intptr_t)j);
                lv_obj_set_style_radius(memBtn, 8, 0);
                lv_obj_set_style_shadow_width(memBtn, 0, 0);
                lv_obj_set_style_pad_all(memBtn, SMIN(10), 0);
                lv_obj_set_style_bg_color(memBtn, COL_CARD2, 0);
                lv_obj_set_style_bg_color(memBtn, COL_BTN_PRESSED, LV_STATE_PRESSED);
                lv_obj_set_style_margin_left(memBtn, SX(40), 0);

                lv_obj_t* memIcon = lv_label_create(memBtn);
                lv_label_set_text(memIcon, MDI_CHEVRON_RIGHT " " MDI_SPEAKER);
                lv_obj_set_style_text_color(memIcon, COL_TEXT2, 0);
                lv_obj_set_style_text_font(memIcon, &lv_font_mdi_16, 0);
                lv_obj_align(memIcon, LV_ALIGN_LEFT_MID, SX(5), 0);

                lv_obj_t* memLbl = lv_label_create(memBtn);
                lv_label_set_text(memLbl, member->roomName.c_str());
                lv_obj_set_style_text_color(memLbl, COL_TEXT, 0);
                lv_obj_set_style_text_font(memLbl, &font_text_16, 0);
                lv_obj_align(memLbl, LV_ALIGN_LEFT_MID, SX(60), 0);

                // Remove from group button
                lv_obj_t* removeBtn = lv_btn_create(memBtn);
                lv_obj_set_size(removeBtn, SX(90), SY(35));
                lv_obj_align(removeBtn, LV_ALIGN_RIGHT_MID, SX(-5), 0);
                lv_obj_set_style_bg_color(removeBtn, COL_ERROR_SURFACE, 0);
                lv_obj_set_style_radius(removeBtn, 8, 0);
                lv_obj_set_user_data(removeBtn, (void*)(intptr_t)j);

                lv_obj_t* removeLbl = lv_label_create(removeBtn);
                lv_label_set_text(removeLbl, "Remove");
                lv_obj_set_style_text_color(removeLbl, COL_TEXT, 0);
                lv_obj_set_style_text_font(removeLbl, &font_text_14, 0);
                lv_obj_center(removeLbl);

                lv_obj_add_event_cb(removeBtn, [](lv_event_t* e) {
                    lv_event_stop_bubbling(e);
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    lv_label_set_text(lbl_groups_status, "Removing from group...");
                    lv_refr_now(NULL);
                    sonos.leaveGroup(idx);
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
            lv_obj_set_size(hdr, SX(720), SY(40));
            lv_obj_set_style_bg_color(hdr, COL_BG, 0);
            lv_obj_set_style_border_width(hdr, 0, 0);
            lv_obj_set_style_pad_all(hdr, SMIN(10), 0);
            lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* hdrLbl = lv_label_create(hdr);
            lv_label_set_text_fmt(hdrLbl, "Add speakers to \"%s\":", coordinator->roomName.c_str());
            lv_obj_set_style_text_color(hdrLbl, COL_ACCENT, 0);
            lv_obj_set_style_text_font(hdrLbl, &font_text_16, 0);
            lv_obj_align(hdrLbl, LV_ALIGN_LEFT_MID, 0, 0);

            // Every speaker that is not already in this group is a valid target — not
            // just the standalone ones. Requiring isGroupCoordinator here meant that once
            // speakers were grouped they vanished from this list, so with everything in
            // one group it came up empty and there was no way to rearrange anything
            // (issue #140 follow-up). Sonos itself lets you pull a speaker straight from
            // one group into another.
            for (int i = 0; i < cnt; i++) {
                if (i == selected_group_coordinator) continue;
                SonosDevice* dev = sonos.getDevice(i);
                if (!dev) continue;

                // Skip if already in the selected group
                if (SonosController::uuidEquals(dev->groupCoordinatorUUID,
                                                coordinator->rinconID)) continue;

                // Where is it now? Drives the label, so the tap is never a surprise.
                int otherMembers = dev->isGroupCoordinator
                                 ? sonos.getGroupMemberCount(i) : 0;
                bool leadsGroup  = (otherMembers > 1);
                bool followsOther = !dev->isGroupCoordinator;
                SonosDevice* otherCoord = followsOther ? sonos.groupCoordinatorFor(dev) : nullptr;

                lv_obj_t* addBtn = lv_btn_create(list_groups);
                // Taller only when a second line is rendered below the room name.
                lv_obj_set_size(addBtn, SX(720), (leadsGroup || followsOther) ? SY(68) : SY(55));
                lv_obj_set_user_data(addBtn, (void*)(intptr_t)i);
                lv_obj_set_style_radius(addBtn, 10, 0);
                lv_obj_set_style_shadow_width(addBtn, 0, 0);
                lv_obj_set_style_pad_all(addBtn, SMIN(10), 0);
                lv_obj_set_style_bg_color(addBtn, COL_OK_SURFACE, 0);  // Dark green hint
                lv_obj_set_style_bg_color(addBtn, COL_OK_SURFACE_PRESSED, LV_STATE_PRESSED);

                lv_obj_t* addIcon = lv_label_create(addBtn);
                lv_label_set_text(addIcon, MDI_PLUS " " MDI_SPEAKER);
                lv_obj_set_style_text_color(addIcon, COL_OK, 0);
                lv_obj_set_style_text_font(addIcon, &lv_font_mdi_24, 0);
                lv_obj_align(addIcon, LV_ALIGN_LEFT_MID, SX(5), 0);

                lv_obj_t* addLbl = lv_label_create(addBtn);
                lv_label_set_text_fmt(addLbl, "Add %s", dev->roomName.c_str());
                lv_obj_set_style_text_color(addLbl, COL_TEXT, 0);
                lv_obj_set_style_text_font(addLbl, &font_text_16, 0);
                lv_obj_set_width(addLbl, SX(560));
                lv_label_set_long_mode(addLbl, LV_LABEL_LONG_DOT);
                lv_obj_align(addLbl, LV_ALIGN_LEFT_MID,
                             SX(60), (leadsGroup || followsOther) ? SY(-9) : 0);

                // Second line only when the speaker is coming from somewhere, so the
                // common standalone case looks exactly as it did before.
                if (leadsGroup) {
                    lv_obj_t* sub2 = lv_label_create(addBtn);
                    lv_label_set_text_fmt(sub2, "brings %d more with it", otherMembers - 1);
                    lv_obj_set_style_text_color(sub2, COL_TEXT2, 0);
                    lv_obj_set_style_text_font(sub2, &font_text_12, 0);
                    lv_obj_align(sub2, LV_ALIGN_LEFT_MID, SX(60), SY(9));
                } else if (followsOther && otherCoord && otherCoord != dev) {
                    lv_obj_t* sub2 = lv_label_create(addBtn);
                    lv_label_set_text_fmt(sub2, "currently in %s", otherCoord->roomName.c_str());
                    lv_obj_set_style_text_color(sub2, COL_TEXT2, 0);
                    lv_obj_set_style_text_font(sub2, &font_text_12, 0);
                    lv_obj_set_width(sub2, SX(560));
                    lv_label_set_long_mode(sub2, LV_LABEL_LONG_DOT);
                    lv_obj_align(sub2, LV_ALIGN_LEFT_MID, SX(60), SY(9));
                }

                lv_obj_add_event_cb(addBtn, [](lv_event_t* e) {
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    if (selected_group_coordinator >= 0) {
                        lv_label_set_text(lbl_groups_status, "Adding to group...");
                        lv_refr_now(NULL);
                        // Cascade: if this speaker coordinates its own group, its members
                        // come along rather than being stranded behind it.
                        sonos.joinGroupCascade(idx, selected_group_coordinator);
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
    lv_obj_set_style_bg_color(scr_groups, COL_SCREEN, 0);

    // Create sidebar and get content area (Groups is index 2)
    lv_obj_t* content = createSettingsSidebar(scr_groups, 2);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title + Refresh button row
    btn_groups_scan = addScreenHeader(content, "Groups", MDI_REFRESH " Scan");
    lv_obj_add_event_cb(btn_groups_scan, [](lv_event_t* e) {
        // Disable button during scan
        lv_obj_add_state(btn_groups_scan, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_groups_scan, COL_BORDER, LV_STATE_DISABLED);

        // Show spinner
        if (spinner_groups_scan) {
            lv_obj_remove_flag(spinner_groups_scan, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(spinner_groups_scan);
        }

        // If no speakers discovered yet, run speaker discovery first
        if (sonos.getDeviceCount() == 0) {
            lv_label_set_text(lbl_groups_status, MDI_REFRESH " Discovering speakers...");
            lv_refr_now(NULL);  // Force immediate refresh to show spinner
            sonos.discoverDevices();
        }

        // Now update group info
        lv_label_set_text(lbl_groups_status, MDI_REFRESH " Updating groups...");
        lv_refr_now(NULL);  // Force immediate refresh

        // Update group info with UI updates
        int cnt = sonos.getDeviceCount();
        for (int i = 0; i < cnt; i++) {
            lv_tick_inc(10);
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
    lv_label_set_text(lbl_scan, MDI_REFRESH " Scan");
    lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_scan, &lv_font_mdi_16, 0);
    lv_obj_center(lbl_scan);

    // Status label
    lbl_groups_status = lv_label_create(content);
    lv_obj_set_pos(lbl_groups_status, 0, SY(50));
    lv_label_set_text(lbl_groups_status, "Tap a group to manage it");
    lv_obj_set_style_text_color(lbl_groups_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_groups_status, &lv_font_mdi_16, 0);

    // Groups list
    list_groups = lv_obj_create(content);
    lv_obj_set_size(list_groups, lv_pct(100), SY(380));
    lv_obj_set_pos(list_groups, 0, SY(75));
    lv_obj_set_style_bg_color(list_groups, COL_BG, 0);
    lv_obj_set_style_border_width(list_groups, 0, 0);
    lv_obj_set_style_radius(list_groups, 0, 0);
    lv_obj_set_style_pad_all(list_groups, 0, 0);
    lv_obj_set_style_pad_row(list_groups, SY(6), 0);
    lv_obj_set_flex_flow(list_groups, LV_FLEX_FLOW_COLUMN);

    // Scrollbar styling
    lv_obj_set_style_pad_right(list_groups, SX(8), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_groups, LV_OPA_30, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_groups, COL_TEXT2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_groups, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_groups, 3, LV_PART_SCROLLBAR);

    // Spinner for scan feedback (centered in content area, hidden by default)
    spinner_groups_scan = lv_spinner_create(content);
    lv_obj_set_size(spinner_groups_scan, SMIN(100), SMIN(100));
    lv_obj_center(spinner_groups_scan);
    lv_obj_set_style_arc_color(spinner_groups_scan, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_groups_scan, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner_groups_scan, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner_groups_scan, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(spinner_groups_scan, true, LV_PART_INDICATOR);
    lv_obj_move_foreground(spinner_groups_scan);
    lv_obj_add_flag(spinner_groups_scan, LV_OBJ_FLAG_HIDDEN);
}
