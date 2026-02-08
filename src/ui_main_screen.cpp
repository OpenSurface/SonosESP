/**
 * UI Main Screen
 * Main player screen with album art, playback controls, and volume
 */

#include "ui_common.h"
#include "lyrics.h"

// ==================== MAIN SCREEN - CLEAN SIMPLE DESIGN ====================
void createMainScreen() {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, COL_BG, 0);
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    // LEFT: Album Art Area (420px) - ambient color background
    panel_art = lv_obj_create(scr_main);
    lv_obj_set_size(panel_art, 420, 480);
    lv_obj_set_pos(panel_art, 0, 0);
    lv_obj_set_style_bg_color(panel_art, lv_color_hex(0x1a1a1a), 0);  // Start with dark gray
    lv_obj_set_style_radius(panel_art, 0, 0);
    lv_obj_set_style_border_width(panel_art, 0, 0);
    lv_obj_set_style_pad_all(panel_art, 0, 0);
    lv_obj_clear_flag(panel_art, LV_OBJ_FLAG_SCROLLABLE);

    // Album art image - centered (JPEG decoder handles aspect ratio centering in buffer)
    img_album = lv_img_create(panel_art);
    lv_obj_set_size(img_album, ART_SIZE, ART_SIZE);
    lv_obj_center(img_album);

    // Placeholder when no art
    art_placeholder = lv_label_create(panel_art);
    lv_label_set_text(art_placeholder, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(art_placeholder, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(art_placeholder, COL_TEXT2, 0);
    lv_obj_center(art_placeholder);

    // Album name ON the album art (at bottom, overlaid on image)
    lbl_album = lv_label_create(panel_art);
    lv_obj_set_width(lbl_album, 400);
    lv_label_set_long_mode(lbl_album, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_album, "");
    lv_obj_set_style_text_color(lbl_album, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_album, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(lbl_album, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_album, LV_ALIGN_BOTTOM_MID, 0, -5);  // Moved closer to bottom (-5 instead of -20)

    // Lyrics status indicator (top left of album art, small and subtle)
    lbl_lyrics_status = lv_label_create(panel_art);
    lv_label_set_text(lbl_lyrics_status, "");
    lv_obj_set_style_text_color(lbl_lyrics_status, lv_color_hex(0x888888), 0);  // Dim gray
    lv_obj_set_style_text_font(lbl_lyrics_status, &lv_font_montserrat_14, 0);  // Small font
    lv_obj_align(lbl_lyrics_status, LV_ALIGN_TOP_LEFT, 5, 5);  // Top left corner

    // Synced lyrics overlay (on top of album art)
    createLyricsOverlay(panel_art);

    // RIGHT: Control Panel (380px) - use global for ambient color changes
    panel_right = lv_obj_create(scr_main);
    lv_obj_set_size(panel_right, 380, 480);
    lv_obj_set_pos(panel_right, 420, 0);
    lv_obj_set_style_bg_color(panel_right, COL_BG, 0);
    lv_obj_set_style_radius(panel_right, 0, 0);
    lv_obj_set_style_border_width(panel_right, 0, 0);
    lv_obj_set_style_pad_all(panel_right, 0, 0);
    lv_obj_clear_flag(panel_right, LV_OBJ_FLAG_SCROLLABLE);

    // ===== TOP ROW: Back | Now Playing - Device | WiFi Queue Settings =====
    // Setup smooth scale transition for all buttons (110% on press)
    static lv_style_transition_dsc_t trans_btn;
    static lv_style_prop_t trans_props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_PROP_INV};
    lv_style_transition_dsc_init(&trans_btn, trans_props, lv_anim_path_ease_out, 150, 0, NULL);

    // Back button - scale effect
    lv_obj_t* btn_back = lv_btn_create(panel_right);
    lv_obj_set_size(btn_back, 40, 40);
    lv_obj_set_pos(btn_back, 15, 15);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_back, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_back, &trans_btn, 0);
    lv_obj_add_event_cb(btn_back, ev_devices, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_back = lv_label_create(btn_back);
    lv_label_set_text(ico_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(ico_back, COL_TEXT, 0);
    lv_obj_center(ico_back);

    // "Now Playing - Device" label - positioned after back button
    lbl_device_name = lv_label_create(panel_right);
    lv_label_set_text(lbl_device_name, "Now Playing");
    lv_obj_set_style_text_color(lbl_device_name, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_device_name, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_device_name, 60, 25);

    // Music Sources button - scale effect
    lv_obj_t* btn_sources = lv_btn_create(panel_right);
    lv_obj_set_size(btn_sources, 38, 38);
    lv_obj_set_pos(btn_sources, 285, 18);
    lv_obj_set_style_bg_opa(btn_sources, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_sources, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_sources, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_sources, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_sources, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_sources, &trans_btn, 0);
    lv_obj_add_event_cb(btn_sources, [](lv_event_t* e) { lv_screen_load(scr_sources); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_src = lv_label_create(btn_sources);
    lv_label_set_text(ico_src, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(ico_src, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_src, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_src);

    // Settings button
    lv_obj_t* btn_settings = lv_btn_create(panel_right);
    lv_obj_set_size(btn_settings, 38, 38);
    lv_obj_set_pos(btn_settings, 335, 18);
    lv_obj_set_style_bg_opa(btn_settings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_settings, 0, 0);
    lv_obj_add_event_cb(btn_settings, ev_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_set = lv_label_create(btn_settings);
    lv_label_set_text(ico_set, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(ico_set, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_set, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_set);

    // ===== TRACK INFO =====
    // Artist (gray, smaller)
    lbl_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_artist, 20, 75);
    lv_obj_set_width(lbl_artist, 300);
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_color(lbl_artist, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_artist, &lv_font_montserrat_16, 0);

    // Title (white, large)
    lbl_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_title, 20, 100);
    lv_obj_set_width(lbl_title, 300);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_title, "Not Playing");
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_28, 0);

    // Queue/Playlist button (on same line as track info) - scale effect
    btn_queue = lv_btn_create(panel_right);
    lv_obj_set_size(btn_queue, 48, 48);  // Increased from 35x35 for better touch target
    lv_obj_set_pos(btn_queue, 323, 88);  // Adjusted position
    lv_obj_set_style_bg_opa(btn_queue, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_queue, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_queue, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_queue, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_queue, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_queue, &trans_btn, 0);
    lv_obj_set_ext_click_area(btn_queue, 8);  // Add 8px clickable area around button
    lv_obj_add_event_cb(btn_queue, ev_queue, LV_EVENT_CLICKED, NULL);  // Go to Queue/Playlist
    lv_obj_t* ico_fav = lv_label_create(btn_queue);
    lv_label_set_text(ico_fav, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(ico_fav, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_fav, &lv_font_montserrat_18, 0);
    lv_obj_center(ico_fav);

    // ===== PROGRESS BAR =====
    slider_progress = lv_slider_create(panel_right);
    lv_obj_set_pos(slider_progress, 20, 160);
    lv_obj_set_size(slider_progress, 340, 5);
    lv_slider_set_range(slider_progress, 0, 100);
    lv_obj_set_style_bg_color(slider_progress, COL_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_progress, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_progress, ev_progress, LV_EVENT_ALL, NULL);

    // Time elapsed
    lbl_time = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time, 20, 175);
    lv_label_set_text(lbl_time, "00:00");
    lv_obj_set_style_text_color(lbl_time, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_14, 0);

    // Time remaining
    lbl_time_remaining = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time_remaining, 315, 175);
    lv_label_set_text(lbl_time_remaining, "0:00");
    lv_obj_set_style_text_color(lbl_time_remaining, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_time_remaining, &lv_font_montserrat_14, 0);

    // ===== PLAYBACK CONTROLS - PERFECTLY CENTERED =====
    // Layout: [shuffle] [prev] [PLAY] [next] [repeat]
    // Center of 380px panel = 190
    // Play button at center, others symmetrically placed

    int ctrl_y = 260;
    int center_x = 190;

    // PLAY button (center) - big white circle with scale effect
    btn_play = lv_btn_create(panel_right);
    lv_obj_set_size(btn_play, 80, 80);
    lv_obj_set_pos(btn_play, center_x - 40, ctrl_y - 40);
    lv_obj_set_style_bg_color(btn_play, COL_TEXT, 0);
    lv_obj_set_style_radius(btn_play, 40, 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_play, 280, LV_STATE_PRESSED);  // Scale to 110%
    lv_obj_set_style_transform_scale_y(btn_play, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_play, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_play, &trans_btn, 0);

    lv_obj_add_event_cb(btn_play, ev_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_play = lv_label_create(btn_play);
    lv_label_set_text(ico_play, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_color(ico_play, COL_BG, 0);
    lv_obj_set_style_text_font(ico_play, &lv_font_montserrat_32, 0);
    lv_obj_center(ico_play);

    // PREV button (left of play) - scale effect
    btn_prev = lv_btn_create(panel_right);
    lv_obj_set_size(btn_prev, 50, 50);
    lv_obj_set_pos(btn_prev, center_x - 100, ctrl_y - 25);
    lv_obj_set_style_bg_opa(btn_prev, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_prev, 25, 0);
    lv_obj_set_style_shadow_width(btn_prev, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_prev, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_prev, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_prev, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_prev, &trans_btn, 0);
    lv_obj_add_event_cb(btn_prev, ev_prev, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_prev = lv_label_create(btn_prev);
    lv_label_set_text(ico_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_color(ico_prev, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_prev, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_prev);

    // NEXT button (right of play) - scale effect
    btn_next = lv_btn_create(panel_right);
    lv_obj_set_size(btn_next, 50, 50);
    lv_obj_set_pos(btn_next, center_x + 50, ctrl_y - 25);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_next, 25, 0);
    lv_obj_set_style_shadow_width(btn_next, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_next, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_next, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_next, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_next, &trans_btn, 0);
    lv_obj_add_event_cb(btn_next, ev_next, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_next = lv_label_create(btn_next);
    lv_label_set_text(ico_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_color(ico_next, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_next, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_next);

    // SHUFFLE button (far left) - scale effect
    btn_shuffle = lv_btn_create(panel_right);
    lv_obj_set_size(btn_shuffle, 45, 45);
    lv_obj_set_pos(btn_shuffle, center_x - 160, ctrl_y - 22);
    lv_obj_set_style_bg_opa(btn_shuffle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_shuffle, 22, 0);
    lv_obj_set_style_shadow_width(btn_shuffle, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_shuffle, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_shuffle, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_shuffle, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_shuffle, &trans_btn, 0);
    lv_obj_add_event_cb(btn_shuffle, ev_shuffle, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_shuf = lv_label_create(btn_shuffle);
    lv_label_set_text(ico_shuf, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_color(ico_shuf, COL_TEXT2, 0);
    lv_obj_set_style_text_font(ico_shuf, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_shuf);

    // REPEAT button (far right) - scale effect
    btn_repeat = lv_btn_create(panel_right);
    lv_obj_set_size(btn_repeat, 45, 45);
    lv_obj_set_pos(btn_repeat, center_x + 115, ctrl_y - 22);
    lv_obj_set_style_bg_opa(btn_repeat, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_repeat, 22, 0);
    lv_obj_set_style_shadow_width(btn_repeat, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_repeat, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_repeat, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_repeat, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_repeat, &trans_btn, 0);
    lv_obj_add_event_cb(btn_repeat, ev_repeat, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_rpt = lv_label_create(btn_repeat);
    lv_label_set_text(ico_rpt, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_color(ico_rpt, COL_TEXT2, 0);
    lv_obj_set_style_text_font(ico_rpt, &lv_font_montserrat_20, 0);
    lv_obj_center(ico_rpt);

    // ===== VOLUME SLIDER =====
    int vol_y = 340;

    // Mute button (left) - scale effect
    btn_mute = lv_btn_create(panel_right);
    lv_obj_set_size(btn_mute, 40, 40);
    lv_obj_set_pos(btn_mute, 20, vol_y);
    lv_obj_set_style_bg_opa(btn_mute, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn_mute, 20, 0);
    lv_obj_set_style_transform_scale_y(btn_mute, 280, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn_mute, 0, 0);
    lv_obj_set_style_transform_scale_x(btn_mute, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn_back, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_mute, &trans_btn, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn_mute, &trans_btn, 0);
    lv_obj_add_event_cb(btn_mute, ev_mute, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_mute = lv_label_create(btn_mute);
    lv_label_set_text(ico_mute, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_color(ico_mute, COL_TEXT2, 0);
    lv_obj_set_style_text_font(ico_mute, &lv_font_montserrat_18, 0);
    lv_obj_center(ico_mute);

    // Volume slider
    slider_vol = lv_slider_create(panel_right);
    lv_obj_set_size(slider_vol, 260, 6);
    lv_obj_set_pos(slider_vol, 70, vol_y + 17);
    lv_slider_set_range(slider_vol, 0, 100);
    lv_obj_set_style_bg_color(slider_vol, COL_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_vol, ev_vol_slider, LV_EVENT_ALL, NULL);

    // ===== PLAY NEXT SECTION (below volume) =====
    int next_y = 440;

    // Small album art for next track (hidden for now)
    img_next_album = lv_img_create(panel_right);
    lv_obj_set_pos(img_next_album, 20, next_y);
    lv_obj_set_size(img_next_album, 40, 40);
    lv_obj_set_style_radius(img_next_album, 4, 0);
    lv_obj_set_style_clip_corner(img_next_album, true, 0);
    lv_obj_add_flag(img_next_album, LV_OBJ_FLAG_HIDDEN); // Hide thumbnail for now

    // "Next:" label
    lbl_next_header = lv_label_create(panel_right);  // Use GLOBAL, not local!
    lv_obj_set_pos(lbl_next_header, 20, next_y);
    lv_label_set_text(lbl_next_header, "Next:");
    lv_obj_set_style_text_color(lbl_next_header, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_next_header, &lv_font_montserrat_12, 0);

    // Next track title - clickable to play next
    lbl_next_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_next_title, 60, next_y);
    lv_label_set_text(lbl_next_title, "");
    lv_obj_set_style_text_color(lbl_next_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_next_title, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_next_title, 300);
    lv_label_set_long_mode(lbl_next_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // Make it clickable to play next track
    lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_next_title, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            sonos.next();
        }
    }, LV_EVENT_ALL, NULL);

    // Next track artist - also clickable
    lbl_next_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_next_artist, 60, next_y + 18);
    lv_label_set_text(lbl_next_artist, "");
    lv_obj_set_style_text_color(lbl_next_artist, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_next_artist, &lv_font_montserrat_12, 0);
    lv_obj_set_width(lbl_next_artist, 300);
    lv_label_set_long_mode(lbl_next_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // Make it clickable to play next track
    lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_next_artist, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            sonos.next();
        }
    }, LV_EVENT_ALL, NULL);
}
