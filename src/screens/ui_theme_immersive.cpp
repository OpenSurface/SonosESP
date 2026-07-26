/**
 * "Immersive" player theme (issue #87, mockup theme-03.png).
 *
 * A genuinely different layout — not a recolour of the Classic screen:
 *   - saturated full-bleed ambient colour (painted by themeApplyBackdrop)
 *   - compact header: art thumbnail + artist + room/state, queue & settings top-right
 *   - oversized title typography, with a large translucent second typographic layer
 *   - synced lyrics sitting directly on the colour (no dark gradient panel)
 *   - minimal dark bottom bar: time, progress, transport, volume
 *
 * CONTRACT (see ui_theme.h): this builder MUST assign every player widget global,
 * because updateUI() and the line-in/TV mode handlers dereference them without
 * null checks. Widgets this layout doesn't display are still created, then hidden.
 *
 * Coordinates are authored in the 800x480 design space and wrapped in SX()/SY()
 * (positions & rectangles) or SMIN() (squares & radii) exactly like the Classic
 * screen, so the layout scales to the 7" panel with no per-size code.
 */

#include "ui_common.h"
#include "lyrics.h"
#include "ui_icons.h"
#include "ui_theme.h"

// Shared press-scale transition (same feel as the Classic screen).
static void applyPressScale(lv_obj_t* btn) {
    static lv_style_transition_dsc_t trans;
    static lv_style_prop_t props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_PROP_INV};
    static bool init = false;
    if (!init) { lv_style_transition_dsc_init(&trans, props, lv_anim_path_ease_out, 150, 0, NULL); init = true; }
    lv_obj_set_style_transform_scale_x(btn, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(btn, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn, &trans, LV_STATE_PRESSED);
    lv_obj_set_style_transition(btn, &trans, 0);
}

// Circular outlined icon button used in the header.
static lv_obj_t* headerButton(lv_obj_t* parent, const char* icon, int x, int y,
                              lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, SMIN(46), SMIN(46));
    lv_obj_set_pos(b, SX(x), SY(y));
    lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(b, SMIN(23), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    applyPressScale(b);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico = lv_label_create(b);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, &lv_font_mdi_24, 0);
    lv_obj_set_style_text_color(ico, COL_TEXT, 0);
    lv_obj_center(ico);
    return b;
}

// Transport button on the dark bottom bar.
static lv_obj_t* barButton(lv_obj_t* parent, const char* icon, const lv_font_t* font,
                           int x, int y, int size, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, SMIN(size), SMIN(size));
    lv_obj_set_pos(b, SX(x), SY(y));
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(b, SMIN(size / 2), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    applyPressScale(b);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico = lv_label_create(b);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, font, 0);
    lv_obj_set_style_text_color(ico, COL_TEXT, 0);
    lv_obj_center(ico);
    return b;
}

void buildImmersivePlayer() {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x1b1b1b), 0);  // until first art colour arrives
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    // Created for API compatibility only — this theme paints a solid backdrop, so
    // themeUsesBlurBg() keeps the blurred-art upload switched off and this stays hidden.
    img_blur_bg = lv_img_create(scr_main);
    lv_obj_set_size(img_blur_bg, SX(800), SY(480));
    lv_obj_set_pos(img_blur_bg, 0, 0);
    lv_obj_add_flag(img_blur_bg, LV_OBJ_FLAG_HIDDEN);

    // ── Two fullscreen transparent layers ───────────────────────────────────
    // panel_art (below): artwork, mode icons, lyrics — matches the Classic
    // parenting so setLineInMode()/setTvAudioMode() keep working unchanged.
    // panel_right (above): header + bottom bar, so its buttons receive touches.
    panel_art = lv_obj_create(scr_main);
    lv_obj_set_size(panel_art, SX(800), SY(480));
    lv_obj_set_pos(panel_art, 0, 0);
    lv_obj_set_style_bg_opa(panel_art, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_art, 0, 0);
    lv_obj_set_style_pad_all(panel_art, 0, 0);
    lv_obj_clear_flag(panel_art, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel_art, LV_OBJ_FLAG_CLICKABLE);

    panel_right = lv_obj_create(scr_main);
    lv_obj_set_size(panel_right, SX(800), SY(480));
    lv_obj_set_pos(panel_right, 0, 0);
    lv_obj_set_style_bg_opa(panel_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_right, 0, 0);
    lv_obj_set_style_pad_all(panel_right, 0, 0);
    lv_obj_clear_flag(panel_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel_right, LV_OBJ_FLAG_CLICKABLE);

    // ── Header: art thumbnail ───────────────────────────────────────────────
    img_album = lv_img_create(panel_art);
    lv_obj_set_size(img_album, SMIN(56), SMIN(56));
    lv_obj_set_pos(img_album, SX(28), SY(22));
    lv_obj_set_style_radius(img_album, SMIN(8), 0);
    lv_obj_set_style_clip_corner(img_album, true, 0);
    lv_obj_set_style_border_width(img_album, 1, 0);
    lv_obj_set_style_border_color(img_album, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(img_album, LV_OPA_30, 0);

    art_placeholder = lv_label_create(panel_art);
    lv_label_set_text(art_placeholder, MDI_MUSIC_NOTE);
    lv_obj_set_style_text_font(art_placeholder, &lv_font_mdi_24, 0);
    lv_obj_set_style_text_color(art_placeholder, COL_TEXT, 0);
    lv_obj_set_style_text_opa(art_placeholder, LV_OPA_60, 0);
    lv_obj_set_pos(art_placeholder, SX(44), SY(38));

    // Mode heroes (line-in / TV) — centred like Classic; shown by the mode handlers.
    lbl_linein_icon = lv_label_create(panel_art);
    lv_label_set_text(lbl_linein_icon, MDI_WAVEFORM);
    lv_obj_set_style_text_font(lbl_linein_icon, &lv_font_mdi_80, 0);
    lv_obj_set_style_text_color(lbl_linein_icon, COL_TEXT, 0);
    lv_obj_align(lbl_linein_icon, LV_ALIGN_CENTER, 0, SY(-40));
    lv_obj_add_flag(lbl_linein_icon, LV_OBJ_FLAG_HIDDEN);

    lbl_linein_subtitle = lv_label_create(panel_art);
    lv_label_set_text(lbl_linein_subtitle, "LIVE AUDIO");
    lv_obj_set_style_text_font(lbl_linein_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_linein_subtitle, COL_TEXT, 0);
    lv_obj_set_style_text_letter_space(lbl_linein_subtitle, 3, 0);
    lv_obj_align(lbl_linein_subtitle, LV_ALIGN_CENTER, 0, SY(38));
    lv_obj_add_flag(lbl_linein_subtitle, LV_OBJ_FLAG_HIDDEN);

    lbl_tv_icon = lv_label_create(panel_art);
    lv_label_set_text(lbl_tv_icon, MDI_TELEVISION);
    lv_obj_set_style_text_font(lbl_tv_icon, &lv_font_mdi_80, 0);
    lv_obj_set_style_text_color(lbl_tv_icon, COL_TEXT, 0);
    lv_obj_align(lbl_tv_icon, LV_ALIGN_CENTER, 0, SY(-40));
    lv_obj_add_flag(lbl_tv_icon, LV_OBJ_FLAG_HIDDEN);

    lbl_tv_subtitle = lv_label_create(panel_art);
    lv_label_set_text(lbl_tv_subtitle, "TV AUDIO");
    lv_obj_set_style_text_font(lbl_tv_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_tv_subtitle, COL_TEXT, 0);
    lv_obj_set_style_text_letter_space(lbl_tv_subtitle, 3, 0);
    lv_obj_align(lbl_tv_subtitle, LV_ALIGN_CENTER, 0, SY(38));
    lv_obj_add_flag(lbl_tv_subtitle, LV_OBJ_FLAG_HIDDEN);

    // ── Header text ─────────────────────────────────────────────────────────
    lbl_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_artist, SX(100), SY(24));
    lv_obj_set_size(lbl_artist, SX(520), SY(26));
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_color(lbl_artist, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_artist, &lv_font_montserrat_20, 0);

    lbl_device_name = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_device_name, SX(100), SY(54));
    lv_label_set_text(lbl_device_name, "Now Playing");
    lv_obj_set_style_text_color(lbl_device_name, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_device_name, LV_OPA_70, 0);
    lv_obj_set_style_text_font(lbl_device_name, &lv_font_montserrat_14, 0);

    lbl_lyrics_status = lv_label_create(panel_right);
    lv_label_set_text(lbl_lyrics_status, "");
    lv_obj_set_pos(lbl_lyrics_status, SX(640), SY(58));
    lv_obj_set_style_text_color(lbl_lyrics_status, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_lyrics_status, LV_OPA_60, 0);
    lv_obj_set_style_text_font(lbl_lyrics_status, &lv_font_montserrat_14, 0);

    btn_queue = headerButton(panel_right, MDI_PLAYLIST, 668, 20, ev_queue);
    lv_obj_set_ext_click_area(btn_queue, 8);
    headerButton(panel_right, MDI_COG, 726, 20, ev_settings);

    // ── Oversized typography ────────────────────────────────────────────────
    // Solid layer: the track title. LONG_CLIP (not scrolling) — animating a 48px
    // label across the full width every frame is expensive with PPA disabled.
    lbl_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_title, SX(24), SY(94));
    lv_obj_set_width(lbl_title, SX(752));
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_title, "Not Playing");
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_48, 0);

    // Translucent second layer: the album name, echoing the mockup's layered
    // display type. Uses an existing global so updateUI() keeps it current with
    // zero changes to the update path.
    lbl_album = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_album, SX(24), SY(158));
    lv_obj_set_width(lbl_album, SX(752));
    lv_label_set_long_mode(lbl_album, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_album, "");
    lv_obj_set_style_text_color(lbl_album, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_album, LV_OPA_30, 0);
    lv_obj_set_style_text_font(lbl_album, &lv_font_montserrat_48, 0);

    // ── Synced lyrics ───────────────────────────────────────────────────────
    // createLyricsOverlay() bottom-aligns itself inside whatever parent it is
    // given, so a positioned transparent wrapper places it exactly where we want
    // without touching lyrics.cpp. Its dark gradient panel is then flattened so the
    // lyric sits straight on the ambient colour, as in the mockup.
    lv_obj_t* lyric_slot = lv_obj_create(panel_art);
    lv_obj_set_size(lyric_slot, SX(470), SY(150));
    lv_obj_set_pos(lyric_slot, SX(20), SY(228));
    lv_obj_set_style_bg_opa(lyric_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lyric_slot, 0, 0);
    lv_obj_set_style_pad_all(lyric_slot, 0, 0);
    lv_obj_clear_flag(lyric_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(lyric_slot, LV_OBJ_FLAG_CLICKABLE);
    createLyricsOverlay(lyric_slot);
    if (lv_obj_t* lyr = lv_obj_get_child(lyric_slot, 0)) {   // the lyrics container
        lv_obj_set_style_bg_opa(lyr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_grad_opa(lyr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_top(lyr, 0, 0);
    }

    // ── Bottom bar ──────────────────────────────────────────────────────────
    // Extends past the bottom edge so only its top corners read as rounded.
    lv_obj_t* bar = lv_obj_create(panel_right);
    lv_obj_set_size(bar, SX(800), SY(110));
    lv_obj_set_pos(bar, 0, SY(386));
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E0E0E), 0);
    lv_obj_set_style_bg_opa(bar, 235, 0);
    lv_obj_set_style_radius(bar, SMIN(26), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lbl_time = lv_label_create(bar);
    lv_obj_set_pos(lbl_time, SX(30), SY(26));
    lv_label_set_text(lbl_time, "0:00");
    lv_obj_set_style_text_color(lbl_time, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_14, 0);

    slider_progress = lv_slider_create(bar);
    lv_obj_set_pos(slider_progress, SX(78), SY(31));
    lv_obj_set_size(slider_progress, SX(228), SY(6));
    lv_slider_set_range(slider_progress, 0, 100);
    lv_obj_set_style_bg_color(slider_progress, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_progress, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_progress, ev_progress, LV_EVENT_ALL, NULL);

    lbl_time_remaining = lv_label_create(bar);
    lv_obj_set_pos(lbl_time_remaining, SX(318), SY(26));
    lv_label_set_text(lbl_time_remaining, "-0:00");
    lv_obj_set_style_text_color(lbl_time_remaining, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_time_remaining, LV_OPA_70, 0);
    lv_obj_set_style_text_font(lbl_time_remaining, &lv_font_montserrat_14, 0);

    btn_prev = barButton(bar, MDI_SKIP_PREV, &lv_font_mdi_32, 396, 20, 44, ev_prev);

    // Play/pause — the ambient-coloured accent of the layout. themeApplyBackdrop()
    // keeps its fill in step with the artwork.
    btn_play = lv_btn_create(bar);
    lv_obj_set_size(btn_play, SMIN(60), SMIN(60));
    lv_obj_set_pos(btn_play, SX(452), SY(12));
    lv_obj_set_style_bg_color(btn_play, g_ambient_bright, 0);
    lv_obj_set_style_radius(btn_play, SMIN(30), 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    applyPressScale(btn_play);
    lv_obj_add_event_cb(btn_play, ev_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_play = lv_label_create(btn_play);
    lv_label_set_text(ico_play, MDI_PAUSE);
    lv_obj_set_style_text_font(ico_play, &lv_font_mdi_32, 0);
    lv_obj_set_style_text_color(ico_play, lv_color_hex(0x121212), 0);
    lv_obj_center(ico_play);

    btn_next = barButton(bar, MDI_SKIP_NEXT, &lv_font_mdi_32, 528, 20, 44, ev_next);
    btn_mute = barButton(bar, MDI_VOLUME_HIGH, &lv_font_mdi_32, 596, 20, 44, ev_mute);

    slider_vol = lv_slider_create(bar);
    lv_obj_set_pos(slider_vol, SX(650), SY(38));
    lv_obj_set_size(slider_vol, SX(120), SY(6));
    lv_slider_set_range(slider_vol, 0, 100);
    lv_obj_set_style_bg_color(slider_vol, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, 3, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_vol, ev_vol_slider, LV_EVENT_ALL, NULL);

    // ── Created-but-hidden ──────────────────────────────────────────────────
    // This layout is deliberately minimal, but updateUI() writes to all of these
    // unconditionally, so they must exist. Kept off-screen rather than merely
    // hidden so a stray unhide can't drop them on top of the artwork.
    btn_shuffle = barButton(bar, MDI_SHUFFLE, &lv_font_mdi_32, 830, 20, 44, ev_shuffle);
    btn_repeat  = barButton(bar, MDI_REPEAT,  &lv_font_mdi_32, 890, 20, 44, ev_repeat);
    lv_obj_add_flag(btn_shuffle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_repeat,  LV_OBJ_FLAG_HIDDEN);

    img_next_album = lv_img_create(panel_right);
    lv_obj_set_pos(img_next_album, SX(830), SY(300));
    lv_obj_set_size(img_next_album, SMIN(40), SMIN(40));
    lv_obj_add_flag(img_next_album, LV_OBJ_FLAG_HIDDEN);

    lbl_next_header = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_header, "Next:");
    lv_obj_set_pos(lbl_next_header, SX(830), SY(300));
    lv_obj_set_style_text_font(lbl_next_header, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);

    lbl_next_title = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_title, "");
    lv_obj_set_pos(lbl_next_title, SX(830), SY(320));
    lv_obj_set_width(lbl_next_title, SX(200));
    lv_obj_set_style_text_font(lbl_next_title, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);

    lbl_next_artist = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_artist, "");
    lv_obj_set_pos(lbl_next_artist, SX(830), SY(338));
    lv_obj_set_width(lbl_next_artist, SX(200));
    lv_obj_set_style_text_font(lbl_next_artist, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
}
