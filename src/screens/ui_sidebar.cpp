/**
 * Settings Sidebar — shared navigation frame for every settings screen.
 *
 * Creates the left rail and returns the content area the caller fills.
 *
 * Coordinates are authored in 800x480 design space and wrapped in SX/SY/SMIN
 * (ui_scale.h) → identity on 4", auto-scaling on larger panels. Because this
 * frame is shared by every settings screen, changing it here changes all of them.
 *
 * ── Now-playing dock (from the "SonosESP Studio" design canvas) ─────────────
 * The rail and content sit above a 56px dock pinned to the bottom edge, so
 * transport control is never lost while a setting is being changed — previously
 * you had to leave Settings entirely to pause. The dock spans the full width
 * beneath both columns, which is why the content area is now 424 tall rather
 * than 480; every screen's list height already derives from SETTINGS_CONTENT_H,
 * so that shortening lands everywhere without touching the screens.
 *
 * The rail also widened 180 → 216 to fit the item labels without truncation,
 * and the firmware version moved from a lone line at the rail's foot up under
 * the "Settings" title, where the dock now sits.
 *
 * Palette is unchanged: this is the canvas's LAYOUT applied to the project's
 * existing COL_* tokens, not its warm repaint. Studio's palette stays with the
 * Studio theme (studio.h).
 */

#include "ui_common.h"
#include "clock_screen.h"
#include "ui_fonts.h"
#include "ui_settings_card.h"

// ── Frame geometry, 800x480 design space ────────────────────────────────────
#define SB_RAIL_W     216
#define SB_DOCK_H     (480 - SETTINGS_CONTENT_H)   // 56
#define SB_ITEM_H     42
#define SB_ITEM_GAP   1
#define SB_ITEM_TOP   66

// ── Dock registry ───────────────────────────────────────────────────────────
// One dock is built per settings screen (there are eight), but only the one on
// the active screen is worth refreshing. A single shared timer walks the list
// and updates that one; nothing here runs while the player is on screen.
#define SB_MAX_DOCKS 12

typedef struct {
    lv_obj_t* screen;
    lv_obj_t* title;
    lv_obj_t* meta;
    lv_obj_t* play_icon;
} SbDock;

static SbDock      sb_docks[SB_MAX_DOCKS];
static uint8_t     sb_dock_count = 0;
static lv_timer_t* sb_dock_timer = nullptr;

static void sbDockTick(lv_timer_t*) {
    lv_obj_t* active = lv_screen_active();
    for (uint8_t i = 0; i < sb_dock_count; i++) {
        SbDock& d = sb_docks[i];
        if (d.screen != active || !d.title) continue;

        SonosDevice* dev = sonos.getCurrentDevice();
        if (dev && dev->currentTrack.length()) {
            lv_label_set_text(d.title, dev->currentTrack.c_str());
            if (dev->currentArtist.length() && dev->roomName.length())
                lv_label_set_text_fmt(d.meta, "%s · %s",
                                      dev->currentArtist.c_str(), dev->roomName.c_str());
            else if (dev->roomName.length())
                lv_label_set_text(d.meta, dev->roomName.c_str());
            else
                lv_label_set_text(d.meta, dev->currentArtist.c_str());
            lv_label_set_text(d.play_icon, dev->isPlaying ? MDI_PAUSE : MDI_PLAY);
        } else {
            lv_label_set_text(d.title, "Not Playing");
            lv_label_set_text(d.meta, dev && dev->roomName.length()
                                          ? dev->roomName.c_str() : "No speaker selected");
            lv_label_set_text(d.play_icon, MDI_PLAY);
        }
        return;   // only ever one active screen
    }
}

// A settings screen is never deleted in normal operation, but themeSet() and any
// future rebuild would leave dangling rows behind. Drop the screen's entry.
static void sbDockScreenDeleted(lv_event_t* e) {
    lv_obj_t* scr = (lv_obj_t*)lv_event_get_target(e);
    for (uint8_t i = 0; i < sb_dock_count; i++) {
        if (sb_docks[i].screen != scr) continue;
        sb_docks[i] = sb_docks[--sb_dock_count];
        return;
    }
}

// Small round icon button used by the dock's transport pair.
static lv_obj_t* dockBtn(lv_obj_t* parent, const char* glyph, lv_color_t col,
                         lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, SMIN(40), SMIN(40));
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, COL_SELECTED, 0);
    lv_obj_set_style_bg_color(b, COL_BTN_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ico = lv_label_create(b);
    lv_label_set_text(ico, glyph);
    lv_obj_set_style_text_font(ico, &lv_font_mdi_16, 0);
    lv_obj_set_style_text_color(ico, col, 0);
    lv_obj_center(ico);
    return b;
}

static void buildDock(lv_obj_t* screen) {
    if (sb_dock_count >= SB_MAX_DOCKS) return;

    lv_obj_t* dock = lv_obj_create(screen);
    lv_obj_set_size(dock, SX(800), SY(SB_DOCK_H));
    lv_obj_set_pos(dock, 0, SY(SETTINGS_CONTENT_H));
    lv_obj_set_style_bg_color(dock, COL_BG, 0);
    lv_obj_set_style_radius(dock, 0, 0);
    lv_obj_set_style_border_width(dock, 1, 0);
    lv_obj_set_style_border_side(dock, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(dock, COL_CARD, 0);
    lv_obj_set_style_pad_hor(dock, SX(16), 0);
    lv_obj_set_style_pad_ver(dock, 0, 0);
    lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dock, LV_OBJ_FLAG_CLICKABLE);

    // A glyph tile, not the album artwork. art_dsc is rebuilt by the art task
    // under art_mutex and img_album on the player is its only consumer; adding a
    // second lv_image onto the same descriptor means a buffer swap can land
    // under a widget nothing invalidates. Not worth a thumbnail.
    lv_obj_t* tile = lv_obj_create(dock);
    lv_obj_set_size(tile, SMIN(40), SMIN(40));
    lv_obj_align(tile, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(tile, COL_CARD, 0);
    lv_obj_set_style_radius(tile, SMIN(6), 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* note = lv_label_create(tile);
    lv_label_set_text(note, MDI_MUSIC_NOTE);
    lv_obj_set_style_text_font(note, &lv_font_mdi_24, 0);
    lv_obj_set_style_text_color(note, COL_TEXT2, 0);
    lv_obj_center(note);

    // Right-hand controls first, so the text block can claim what is left.
    lv_obj_t* back = lv_button_create(dock);
    lv_obj_set_size(back, SX(120), SMIN(40));
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(back, SMIN(20), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(back, COL_BORDER, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "Now playing");
    lv_obj_set_style_text_font(back_lbl, &font_text_12, 0);
    lv_obj_set_style_text_color(back_lbl, COL_TEXT2, 0);
    lv_obj_center(back_lbl);

    lv_obj_t* b_next = dockBtn(dock, MDI_SKIP_NEXT, COL_TEXT2, ev_next);
    lv_obj_align(b_next, LV_ALIGN_RIGHT_MID, -SX(128), 0);

    lv_obj_t* b_play = dockBtn(dock, MDI_PAUSE, COL_ACCENT, ev_play);
    lv_obj_align(b_play, LV_ALIGN_RIGHT_MID, -SX(174), 0);

    const int text_w = 800 - 32 - 40 - 14 - 174 - 14;   // dock minus tile and controls

    lv_obj_t* title = lv_label_create(dock);
    lv_label_set_text(title, "Not Playing");
    lv_obj_set_pos(title, SX(54), SY(10));
    lv_obj_set_width(title, SX(text_w));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, &font_text_14, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);

    lv_obj_t* meta = lv_label_create(dock);
    lv_label_set_text(meta, "");
    lv_obj_set_pos(meta, SX(54), SY(30));
    lv_obj_set_width(meta, SX(text_w));
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(meta, &font_text_12, 0);
    lv_obj_set_style_text_color(meta, COL_TEXT2, 0);

    sb_docks[sb_dock_count++] = { screen, title, meta, lv_obj_get_child(b_play, 0) };
    lv_obj_add_event_cb(screen, sbDockScreenDeleted, LV_EVENT_DELETE, nullptr);

    if (!sb_dock_timer) sb_dock_timer = lv_timer_create(sbDockTick, 500, nullptr);
}

// ============================================================================
// Settings sidebar — creates the rail + dock and returns the content area
// ============================================================================
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx) {
    // ========== LEFT RAIL ==========
    lv_obj_t* sidebar = lv_obj_create(screen);
    lv_obj_set_size(sidebar, SX(SB_RAIL_W), SY(SETTINGS_CONTENT_H));
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_style_bg_color(sidebar, COL_BG, 0);
    lv_obj_set_style_border_width(sidebar, 1, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(sidebar, COL_CARD, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // Title, version and close. The version sits under the title now that the
    // dock occupies the rail's foot.
    lv_obj_t* lbl_title = lv_label_create(sidebar);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_set_style_text_font(lbl_title, &font_text_20, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, SX(16), SY(14));

    lv_obj_t* lbl_ver = lv_label_create(sidebar);
    lv_label_set_text(lbl_ver, "v" FIRMWARE_VERSION " · " PANEL_SIZE_LABEL);
    lv_obj_set_style_text_font(lbl_ver, &font_text_12, 0);
    lv_obj_set_style_text_color(lbl_ver, COL_TEXT2, 0);
    lv_obj_set_style_text_letter_space(lbl_ver, 1, 0);
    lv_obj_set_pos(lbl_ver, SX(16), SY(40));

    lv_obj_t* btn_close = lv_button_create(sidebar);
    lv_obj_set_size(btn_close, SMIN(36), SMIN(36));
    lv_obj_set_pos(btn_close, SX(SB_RAIL_W - 48), SY(14));
    lv_obj_set_style_bg_color(btn_close, COL_SELECTED, 0);
    lv_obj_set_style_bg_color(btn_close, COL_BTN_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_close, SMIN(18), 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_set_style_pad_all(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_x = lv_label_create(btn_close);
    lv_label_set_text(ico_x, MDI_CLOSE);
    lv_obj_set_style_text_color(ico_x, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_x, &lv_font_mdi_16, 0);
    lv_obj_center(ico_x);

    // Menu items (order: General, Speakers, Groups, Sources, Display, WiFi, Clock, Update)
    const char* icons[]  = {MDI_COG, MDI_SPEAKER, MDI_SPEAKER_MULTIPLE, MDI_PLAYLIST,
                            MDI_MONITOR, MDI_WIFI, MDI_CLOCK_OUTLINE, MDI_DOWNLOAD};
    const char* labels[] = {"General", "Speakers", "Groups", "Sources",
                            "Display", "WiFi", "Clock", "Update"};

    int y = SB_ITEM_TOP;
    for (int i = 0; i < 8; i++) {
        lv_obj_t* btn = lv_button_create(sidebar);
        lv_obj_set_size(btn, SX(SB_RAIL_W - 16), SY(SB_ITEM_H));
        lv_obj_set_pos(btn, SX(8), SY(y));

        bool active = (i == activeIdx);
        lv_obj_set_style_bg_color(btn, active ? COL_ACCENT : COL_BG, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 9, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_left(btn, SX(12), 0);

        lv_obj_t* ico = lv_label_create(btn);
        lv_label_set_text(ico, icons[i]);
        lv_obj_set_style_text_color(ico, active ? lv_color_hex(0x000000) : COL_TEXT2, 0);
        lv_obj_set_style_text_font(ico, &lv_font_mdi_24, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, active ? lv_color_hex(0x000000) : COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &font_text_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SX(30), 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            switch(idx) {
                case 0: lv_screen_load(scr_general);        break;
                case 1: lv_screen_load(scr_devices);        break;
                case 2: lv_screen_load(scr_groups);         break;
                case 3: lv_screen_load(scr_sources);        break;
                case 4: lv_screen_load(scr_display);        break;
                case 5: lv_screen_load(scr_wifi);           break;
                case 6: lv_screen_load(scr_clock_settings); break;
                case 7: lv_screen_load(scr_ota);            break;
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        y += SB_ITEM_H + SB_ITEM_GAP;
    }

    // ========== RIGHT CONTENT AREA ==========
    lv_obj_t* content = lv_obj_create(screen);
    lv_obj_set_size(content, SX(800 - SB_RAIL_W), SY(SETTINGS_CONTENT_H));
    lv_obj_set_pos(content, SX(SB_RAIL_W), 0);
    lv_obj_set_style_bg_color(content, COL_SCREEN, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, SMIN(SETTINGS_CONTENT_PAD), 0);

    // Every settings screen turns this area's scrollbar on but none of them style
    // it, so it was drawn by LVGL's default (light) theme. Styled once here so it
    // is dark for all of them — and for any settings screen added later.
    lv_obj_set_style_bg_color(content, COL_SELECTED, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(content, SX(6), LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, SX(3), LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(content, SX(2), LV_PART_SCROLLBAR);

    // ========== NOW-PLAYING DOCK ==========
    buildDock(screen);

    return content;
}
