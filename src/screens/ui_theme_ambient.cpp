/**
 * "Ambient" player theme (issue #87, mockup theme-02.png).
 *
 * A refined take on the original layout rather than a recolour of it:
 *   - flat backdrop tinted from the artwork
 *   - smaller rounded artwork on the left, at a fixed position, with the synced
 *     lyrics sitting BELOW it instead of overlaid on top
 *   - pill-shaped room selector, circular queue/settings buttons
 *   - accent-coloured artist over a large white title, then album, progress,
 *     transport, and a volume row along the bottom of the right column
 *
 * ── Layout grid (800x480 design space, wrapped in SX/SY/SMIN)
 *   left column  : x  39 .. 347   artwork 308 square at y 36
 *   lyrics       : below the artwork, from y 360
 *   right column : x 375 .. 768
 *   transport    : centred on x = 571, all items on the y = 362 centreline
 *
 * CONTRACT (ui_theme.h): a builder MUST assign every player widget global —
 * updateUI() and the line-in/TV handlers dereference them without null checks.
 * Widgets this layout doesn't show are created and parked off-canvas.
 */

#include "ui_common.h"
#include "lyrics.h"
#include "ui_icons.h"
#include "ui_theme.h"
#include "ui_fonts.h"

// ── Grid ────────────────────────────────────────────────────────────────────
#define AM_L            39                  // left column origin
#define AM_ART          308
// The artwork never moves. An earlier version centred it and lifted it when a
// track had lyrics, but that jump on every song looked cheap — a fixed frame
// reads as far more considered. The lyric strip below is simply always reserved,
// and sits empty on tracks without lyrics.
#define AM_ART_Y        36
#define AM_LYRIC_Y      (AM_ART_Y + AM_ART + 16)   // 360
#define AM_R            375                 // right column origin
#define AM_RIGHT        768
#define AM_RW           (AM_RIGHT - AM_R)    // 393
#define AM_CTRL_MID     571                 // transport centreline (x)
#define AM_CTRL_Y       362                 // transport centreline (y)

static void pressScale(lv_obj_t* b) {
    static lv_style_transition_dsc_t tr;
    static lv_style_prop_t props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_PROP_INV};
    static bool init = false;
    if (!init) { lv_style_transition_dsc_init(&tr, props, lv_anim_path_ease_out, 150, 0, NULL); init = true; }
    lv_obj_set_style_transform_scale_x(b, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(b, 280, LV_STATE_PRESSED);
    lv_obj_set_style_transition(b, &tr, LV_STATE_PRESSED);
    lv_obj_set_style_transition(b, &tr, 0);
}

// Circular button. `filled` gives the soft translucent disc used in the header;
// transport buttons are bare icons.
static lv_obj_t* circleBtn(lv_obj_t* parent, const char* icon, const lv_font_t* font,
                           int x, int y, int d, lv_event_cb_t cb, bool filled,
                           lv_color_t icon_col) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, SMIN(d), SMIN(d));
    lv_obj_set_pos(b, SX(x), SY(y));
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (filled) {
        lv_obj_set_style_bg_color(b, COL_TEXT, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_10, 0);
        lv_obj_set_style_border_width(b, 0, 0);
    } else {
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b, 0, 0);
    }
    pressScale(b);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico = lv_label_create(b);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, font, 0);
    lv_obj_set_style_text_color(ico, icon_col, 0);
    lv_obj_center(ico);
    return b;
}

static void park(lv_obj_t* o) {
    if (!o) return;
    lv_obj_set_pos(o, SX(900), SY(600));
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void buildAmbientPlayer() {
    scr_main = lv_obj_create(NULL);
    // Flat tint, no gradient. A vertical gradient across the full screen banded
    // visibly into hard steps: the panel is RGB565 (LV_COLOR_DEPTH 16), so each
    // channel can only move in ~8/255 jumps, and LVGL 9 has no gradient dithering
    // to hide them. Flat colour is the only genuinely smooth option here.
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x16121A), 0);   // until the first art colour
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    // Created for API compatibility — this theme paints its own backdrop, so
    // themeUsesBlurBg() keeps the blurred art switched off.
    img_blur_bg = lv_img_create(scr_main);
    lv_obj_set_size(img_blur_bg, SX(800), SY(480));
    lv_obj_set_pos(img_blur_bg, 0, 0);
    lv_obj_add_flag(img_blur_bg, LV_OBJ_FLAG_HIDDEN);

    // Transparent layers, matching Classic's parenting so setLineInMode() and
    // setTvAudioMode() keep working untouched.
    auto mkLayer = [&](void) {
        lv_obj_t* p = lv_obj_create(scr_main);
        lv_obj_set_size(p, SX(800), SY(480));
        lv_obj_set_pos(p, 0, 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(p, 0, 0);
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
        return p;
    };
    panel_art   = mkLayer();
    panel_right = mkLayer();

    // ── Left column: artwork ────────────────────────────────────────────────
    img_album = lv_img_create(panel_art);
    lv_obj_set_size(img_album, SMIN(AM_ART), SMIN(AM_ART));
    lv_obj_set_pos(img_album, SX(AM_L), SY(AM_ART_Y));
    // Square artwork, and NO blur shadow.
    //
    // shadow_width in LVGL is a BLUR RADIUS, not an outline: the blur spreads
    // equally in every direction from each corner point, so a 36px shadow renders
    // with visibly rounded corners around a square image however the radius is
    // set. Zeroing the radius could never fix that — and it also drops the most
    // expensive draw on this screen, since blur is pure software here.
    //
    // A 1px white outline replaced the shadow to give the artwork definition.
    // Removed: against a dark ambient backdrop it read as a hard white frame
    // around the album rather than as an edge, which is not the look this theme
    // is going for. The artwork now sits directly on the backdrop.
    lv_obj_set_style_radius(img_album, 0, 0);
    lv_obj_set_style_shadow_width(img_album, 0, 0);
    lv_obj_set_style_border_width(img_album, 0, 0);

    art_placeholder = lv_label_create(panel_art);
    lv_label_set_text(art_placeholder, MDI_MUSIC_NOTE);
    lv_obj_set_style_text_font(art_placeholder, &lv_font_mdi_32, 0);
    lv_obj_set_style_text_color(art_placeholder, COL_TEXT2, 0);
    lv_obj_set_pos(art_placeholder, SX(AM_L + AM_ART / 2 - 16), SY(AM_ART_Y + AM_ART / 2 - 16));

    // Mode heroes (line-in / TV), centred on the artwork square.
    struct { lv_obj_t** icon; lv_obj_t** sub; const char* glyph; const char* text; } modes[] = {
        { &lbl_linein_icon, &lbl_linein_subtitle, MDI_WAVEFORM,   "LIVE AUDIO" },
        { &lbl_tv_icon,     &lbl_tv_subtitle,     MDI_TELEVISION, "TV AUDIO"   },
    };
    for (auto& m : modes) {
        *m.icon = lv_label_create(panel_art);
        lv_label_set_text(*m.icon, m.glyph);
        lv_obj_set_style_text_font(*m.icon, &lv_font_mdi_80, 0);
        lv_obj_set_style_text_color(*m.icon, COL_ACCENT, 0);
        lv_obj_set_pos(*m.icon, SX(AM_L + AM_ART / 2 - 40), SY(AM_ART_Y + AM_ART / 2 - 60));
        lv_obj_add_flag(*m.icon, LV_OBJ_FLAG_HIDDEN);

        *m.sub = lv_label_create(panel_art);
        lv_label_set_text(*m.sub, m.text);
        lv_obj_set_style_text_font(*m.sub, &font_text_14, 0);
        lv_obj_set_style_text_color(*m.sub, COL_TEXT3, 0);
        lv_obj_set_style_text_letter_space(*m.sub, 3, 0);
        lv_obj_set_pos(*m.sub, SX(AM_L + AM_ART / 2 - 44), SY(AM_ART_Y + AM_ART / 2 + 40));
        lv_obj_add_flag(*m.sub, LV_OBJ_FLAG_HIDDEN);
    }

    lbl_lyrics_status = lv_label_create(panel_art);
    lv_label_set_text(lbl_lyrics_status, "");
    lv_obj_set_pos(lbl_lyrics_status, SX(AM_L), SY(AM_ART_Y + AM_ART + 6));
    lv_obj_set_style_text_color(lbl_lyrics_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_lyrics_status, &font_text_12, 0);

    // ── Left column: lyrics BELOW the artwork ───────────────────────────────
    // createLyricsOverlay() bottom-aligns itself inside its parent, so a
    // positioned wrapper puts it exactly here without touching lyrics.cpp. Its
    // dark gradient panel is flattened — the backdrop is already dark, and in this
    // layout the lyrics are no longer sitting on top of the artwork.
    // Sits directly under the artwork and runs to the bottom margin.
    // runs to the bottom margin.
    lv_obj_t* lyric_slot = lv_obj_create(panel_art);
    lv_obj_set_size(lyric_slot, SX(AM_ART + 20), SY(128));
    lv_obj_set_pos(lyric_slot, SX(AM_L - 10), SY(AM_LYRIC_Y));
    lv_obj_set_style_bg_opa(lyric_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lyric_slot, 0, 0);
    lv_obj_set_style_pad_all(lyric_slot, 0, 0);
    lv_obj_clear_flag(lyric_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(lyric_slot, LV_OBJ_FLAG_CLICKABLE);
    createLyricsOverlay(lyric_slot);
    if (lv_obj_t* lyr = lv_obj_get_child(lyric_slot, 0)) {
        // Flatten the dark gradient panel — the backdrop is already dark, and here
        // the lyrics no longer sit on top of the artwork.
        lv_obj_set_style_bg_opa(lyr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_grad_opa(lyr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_top(lyr, 0, 0);
        lv_obj_set_style_radius(lyr, 0, 0);
        lv_obj_set_size(lyr, SX(AM_ART + 20), SY(128));

        // Scale the type up — the shared overlay is sized for the small strip over
        // the artwork in the original layout, which reads as tiny out here.
        // Children are prev / current / next, in creation order.
        //
        // Only TWO lines are shown. The overlay is bottom-aligned inside its slot,
        // so with three lines the block grew upward and the "previous" line ended
        // up behind the artwork. Hiding it keeps current + next fully in the clear.
        const lv_font_t* fonts[3] = { &font_text_16,   // prev (hidden)
                                      &font_text_24,   // current
                                      &font_text_16 }; // next
        for (int i = 0; i < 3; i++) {
            if (lv_obj_t* l = lv_obj_get_child(lyr, i)) {
                lv_obj_set_style_text_font(l, fonts[i], 0);
                lv_obj_set_width(l, SX(AM_ART + 4));
                lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
                if (i == 0) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // ── Right column: room pill + round header buttons ──────────────────────
    lv_obj_t* pill = lv_btn_create(panel_right);
    lv_obj_set_size(pill, SX(250), SY(38));
    lv_obj_set_pos(pill, SX(AM_R), SY(40));
    lv_obj_set_style_radius(pill, SMIN(19), 0);
    lv_obj_set_style_bg_color(pill, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_10, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    pressScale(pill);
    lv_obj_add_event_cb(pill, ev_devices, LV_EVENT_CLICKED, NULL);

    lv_obj_t* dot = lv_obj_create(pill);
    lv_obj_set_size(dot, SMIN(8), SMIN(8));
    lv_obj_set_pos(dot, SX(14), SY(15));
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    lbl_device_name = lv_label_create(pill);
    lv_label_set_text(lbl_device_name, "Now Playing");
    lv_obj_set_pos(lbl_device_name, SX(30), SY(10));
    lv_obj_set_size(lbl_device_name, SX(196), SY(20));
    lv_label_set_long_mode(lbl_device_name, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_color(lbl_device_name, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_device_name, &font_text_14, 0);

    // The generated MDI font has no chevron-down glyph, so the pill uses the
    // right chevron — it still reads as "tap to change room".
    lv_obj_t* chev = lv_label_create(pill);
    lv_label_set_text(chev, MDI_CHEVRON_RIGHT);
    lv_obj_set_style_text_font(chev, &lv_font_mdi_16, 0);
    lv_obj_set_style_text_color(chev, COL_TEXT2, 0);
    lv_obj_set_pos(chev, SX(228), SY(11));

    btn_queue = circleBtn(panel_right, MDI_PLAYLIST, &lv_font_mdi_24,
                          AM_RIGHT - 44 - 11 - 44, 40, 44, ev_queue, true, COL_TEXT);
    lv_obj_set_ext_click_area(btn_queue, 8);
    circleBtn(panel_right, MDI_COG, &lv_font_mdi_24,
              AM_RIGHT - 44, 40, 44, ev_settings, true, COL_TEXT);

    // ── Right column: track info ────────────────────────────────────────────
    // Artist first and in the accent colour, per the mockup.
    lbl_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_artist, SX(AM_R), SY(126));
    lv_obj_set_size(lbl_artist, SX(AM_RW), SY(26));
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_color(lbl_artist, COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_artist, &font_text_20, 0);

    lbl_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_title, SX(AM_R), SY(156));
    lv_obj_set_size(lbl_title, SX(AM_RW), SY(44));
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_title, "Not Playing");
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &font_text_32, 0);

    lbl_album = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_album, SX(AM_R), SY(212));
    lv_obj_set_size(lbl_album, SX(AM_RW), SY(20));
    lv_label_set_long_mode(lbl_album, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_album, "");
    lv_obj_set_style_text_color(lbl_album, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_album, &font_text_14, 0);

    // ── Right column: progress ──────────────────────────────────────────────
    slider_progress = lv_slider_create(panel_right);
    lv_obj_set_pos(slider_progress, SX(AM_R), SY(264));
    lv_obj_set_size(slider_progress, SX(AM_RW), SY(6));
    lv_slider_set_range(slider_progress, 0, 100);
    lv_obj_set_style_bg_color(slider_progress, COL_BTN_PRESSED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_progress, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_progress, SMIN(4), LV_PART_KNOB);
    lv_obj_add_event_cb(slider_progress, ev_progress, LV_EVENT_ALL, NULL);

    lbl_time = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time, SX(AM_R), SY(284));
    lv_label_set_text(lbl_time, "0:00");
    lv_obj_set_style_text_color(lbl_time, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_time, &font_text_14, 0);

    lbl_time_remaining = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time_remaining, SX(AM_RIGHT - 60), SY(284));
    lv_obj_set_size(lbl_time_remaining, SX(60), SY(18));
    // CLIP not DOT: this is a clock value in a fixed box, and an ellipsised time
    // reads as a glitch. Long durations are truncated rather than wrapped.
    lv_label_set_long_mode(lbl_time_remaining, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(lbl_time_remaining, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_time_remaining, "-0:00");
    lv_obj_set_style_text_color(lbl_time_remaining, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_time_remaining, &font_text_14, 0);

    // ── Right column: transport, symmetric about AM_CTRL_MID ────────────────
    btn_shuffle = circleBtn(panel_right, MDI_SHUFFLE, &lv_font_mdi_32,
                            AM_CTRL_MID - 160 - 22, AM_CTRL_Y - 22, 44, ev_shuffle, false, COL_TEXT2);
    btn_prev    = circleBtn(panel_right, MDI_SKIP_PREV, &lv_font_mdi_40,
                            AM_CTRL_MID - 88 - 26, AM_CTRL_Y - 26, 52, ev_prev, false, COL_TEXT);
    btn_next    = circleBtn(panel_right, MDI_SKIP_NEXT, &lv_font_mdi_40,
                            AM_CTRL_MID + 88 - 26, AM_CTRL_Y - 26, 52, ev_next, false, COL_TEXT);
    btn_repeat  = circleBtn(panel_right, MDI_REPEAT, &lv_font_mdi_32,
                            AM_CTRL_MID + 160 - 22, AM_CTRL_Y - 22, 44, ev_repeat, false, COL_TEXT2);

    btn_play = lv_btn_create(panel_right);
    lv_obj_set_size(btn_play, SMIN(70), SMIN(70));
    lv_obj_set_pos(btn_play, SX(AM_CTRL_MID - 35), SY(AM_CTRL_Y - 35));
    lv_obj_set_style_bg_color(btn_play, COL_TEXT, 0);
    lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    pressScale(btn_play);
    lv_obj_add_event_cb(btn_play, ev_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_play = lv_label_create(btn_play);
    lv_label_set_text(ico_play, MDI_PAUSE);
    lv_obj_set_style_text_font(ico_play, &lv_font_mdi_40, 0);
    lv_obj_set_style_text_color(ico_play, lv_color_hex(0x141414), 0);
    lv_obj_center(ico_play);

    // ── Right column: volume ────────────────────────────────────────────────
    btn_mute = circleBtn(panel_right, MDI_VOLUME_HIGH, &lv_font_mdi_32,
                         AM_R, 406, 44, ev_mute, false, COL_TEXT2);

    slider_vol = lv_slider_create(panel_right);
    lv_obj_set_pos(slider_vol, SX(AM_R + 54), SY(425));
    lv_obj_set_size(slider_vol, SX(AM_RW - 54), SY(6));
    lv_slider_set_range(slider_vol, 0, 100);
    lv_obj_set_style_bg_color(slider_vol, COL_BTN_PRESSED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, SMIN(4), LV_PART_KNOB);
    lv_obj_add_event_cb(slider_vol, ev_vol_slider, LV_EVENT_ALL, NULL);

    // ── Created but unused by this layout ───────────────────────────────────
    // updateUI() writes to these unconditionally, so they must exist.
    img_next_album = lv_img_create(panel_right);
    lv_obj_set_size(img_next_album, SMIN(40), SMIN(40));
    park(img_next_album);

    lbl_next_header = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_header, "Next:");
    lv_obj_set_style_text_font(lbl_next_header, &font_text_12, 0);
    park(lbl_next_header);

    // Ellipsise rather than wrap: both strings are unbounded track metadata in a
    // fixed-width parked label, so the default WRAP grows them downward over
    // whatever sits below.
    lbl_next_title = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_title, "");
    lv_obj_set_width(lbl_next_title, SX(200));
    lv_label_set_long_mode(lbl_next_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl_next_title, &font_text_14, 0);
    park(lbl_next_title);

    lbl_next_artist = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_artist, "");
    lv_obj_set_width(lbl_next_artist, SX(200));
    lv_label_set_long_mode(lbl_next_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl_next_artist, &font_text_12, 0);
    park(lbl_next_artist);
}
