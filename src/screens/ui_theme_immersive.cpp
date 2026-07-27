/**
 * "Immersive" player theme (issue #87).
 *
 * A lyrics-forward layout, not a recolour of the Classic screen:
 *   - saturated full-bleed ambient colour (painted by themeApplyBackdrop)
 *   - header: large 112px artwork + title / artist / room
 *   - a big LYRIC STAGE filling the middle of the screen: each synced line fades
 *     and slides in at a randomised position, so the screen keeps changing with
 *     the song. Falls back to the track title when there are no lyrics, so the
 *     stage is never empty.
 *   - flat dark control bar pinned to the bottom edge
 *
 * ── Layout grid (800x480 design space, wrapped in SX/SY/SMIN like every screen)
 *   margins      : content spans x = 32 .. 768
 *   header       : y  24 .. 136   (artwork 112 square)
 *   lyric stage  : y 152 .. 372
 *   control bar  : y 386 .. 480   (flat, flush to the bottom edge)
 * Everything inside the bar is centred on the bar's own height, so the row reads
 * as one line: 44px buttons at y=25, the 60px play button at y=17, 6px sliders at
 * y=44, 14px labels at y=38.
 *
 * CONTRACT (ui_theme.h): a builder MUST assign every player widget global —
 * updateUI() and the line-in/TV handlers dereference them without null checks.
 * Widgets this layout doesn't show are created and hidden.
 */

#include "ui_common.h"
#include "lyrics.h"
#include "ui_icons.h"
#include "ui_theme.h"
#include <esp_random.h>

// ── Grid constants (design space) ───────────────────────────────────────────
#define IM_MARGIN      32
#define IM_RIGHT       768                  // 800 - IM_MARGIN
#define IM_CONTENT_W   (IM_RIGHT - IM_MARGIN)   // 736

#define IM_ART         112
#define IM_HEAD_Y      24
#define IM_TEXT_X      (IM_MARGIN + IM_ART + 20)   // 164

#define IM_STAGE_Y     152
#define IM_STAGE_BOT   372

#define IM_BAR_Y       386
#define IM_BAR_H       94                   // 386 + 94 = 480, flush to the bottom

// Vertical centring inside the bar — one source of truth for the whole row.
#define IM_BAR_MID(h)  ((IM_BAR_H - (h)) / 2)

// Volume icon rests hard against the right margin (so the bar has no dead space),
// then slides to the head of the control row when tapped, handing that whole row
// to the volume slider.
#define IM_MUTE_X      (IM_RIGHT - 44)      // 724 -> right edge lands on 768
#define IM_VOL_ICON_X  IM_MARGIN
#define IM_ROW_X       78
#define IM_ROW_W       250

// The stage clears itself when nothing new arrives. A lyric line resets this, so
// it only fades during an instrumental stretch or on a track with no lyrics at
// all — matching how the Classic overlay hides itself.
#define IM_LYRIC_HOLD_MS   9000
#define IM_TITLE_HOLD_MS   5000
// Idle timeout for the slide-out volume bar.
#define IM_VOL_HOLD_MS     4000

// ── Lyric stage state ───────────────────────────────────────────────────────
static lv_obj_t*    im_stage      = nullptr;   // the big animated text
static lv_timer_t*  im_timer      = nullptr;
static int          im_last_idx   = -999;
static char         im_last_txt[MAX_LYRIC_TEXT] = "";
static uint32_t     im_fade_at    = 0;         // ms deadline to fade the stage out (0 = stay)

// ── Volume fly-out state ────────────────────────────────────────────────────
static bool         im_vol_open   = false;
static uint32_t     im_vol_close_at = 0;
static lv_obj_t*    im_prog_group[3] = { nullptr, nullptr, nullptr };  // time / progress / -time

static void im_anim_opa(void* o, int32_t v) { lv_obj_set_style_text_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void im_anim_y(void* o, int32_t v)   { lv_obj_set_y((lv_obj_t*)o, v); }
static void im_anim_x(void* o, int32_t v)   { lv_obj_set_x((lv_obj_t*)o, v); }
static void im_anim_obj_opa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }

// Places `txt` on the stage. `lively` = a new synced lyric line: randomise the
// position and animate it in. Otherwise (title fallback) sit calmly centred.
static void im_show(const char* txt, bool lively) {
    if (!im_stage) return;

    lv_anim_delete(im_stage, im_anim_opa);
    lv_anim_delete(im_stage, im_anim_y);

    lv_label_set_text(im_stage, txt ? txt : "");

    lv_text_align_t align = LV_TEXT_ALIGN_CENTER;
    int y = SY(IM_STAGE_Y + 40);

    if (lively) {
        // Randomise which corner of the stage the line lands in, so consecutive
        // lines don't stack in the same spot.
        uint32_t r = esp_random();
        switch (r % 3) {
            case 0: align = LV_TEXT_ALIGN_LEFT;   break;
            case 1: align = LV_TEXT_ALIGN_CENTER; break;
            default: align = LV_TEXT_ALIGN_RIGHT; break;
        }
        static const int bands[] = { 8, 44, 82 };
        y = SY(IM_STAGE_Y + bands[(r >> 8) % 3]);
    }

    lv_obj_set_style_text_align(im_stage, align, 0);

    // Long lines wrap; clamp so a tall block can never spill into the control bar.
    lv_obj_update_layout(im_stage);
    int h = lv_obj_get_height(im_stage);
    int max_y = SY(IM_STAGE_BOT) - h;
    if (y > max_y) y = max_y;
    if (y < SY(IM_STAGE_Y)) y = SY(IM_STAGE_Y);

    if (!lively) {
        lv_obj_set_y(im_stage, y);
        lv_obj_set_style_text_opa(im_stage, LV_OPA_90, 0);
        return;
    }

    // Fade + rise. Two short animations on one label — cheap enough with the
    // software renderer (PPA is disabled project-wide).
    lv_obj_set_y(im_stage, y + SY(22));
    lv_obj_set_style_text_opa(im_stage, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, im_stage);
    lv_anim_set_duration(&a, 380);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, im_anim_y);
    lv_anim_set_values(&a, y + SY(22), y);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, im_anim_opa);
    lv_anim_set_values(&a, 0, LV_OPA_COVER);
    lv_anim_start(&a);
}

// Fades the stage out and leaves it empty.
static void im_stage_fade_out(void) {
    if (!im_stage) return;
    im_fade_at = 0;
    lv_anim_delete(im_stage, im_anim_opa);
    lv_anim_delete(im_stage, im_anim_y);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, im_stage);
    lv_anim_set_duration(&a, 420);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, im_anim_opa);
    lv_anim_set_values(&a, lv_obj_get_style_text_opa(im_stage, LV_PART_MAIN), 0);
    lv_anim_start(&a);
}

// Slides the volume bar into the progress row, or puts the progress row back.
static void im_vol_set_open(bool open) {
    if (!btn_mute || !slider_vol) return;
    im_vol_open = open;
    im_vol_close_at = open ? (lv_tick_get() + IM_VOL_HOLD_MS) : 0;

    // The volume slider occupies the same slot as the progress bar, so opening it
    // gives the whole width of the bar to volume — nothing is cramped in beside it.
    for (auto* o : im_prog_group) {
        if (!o) continue;
        if (open) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }

    // Cross-fade the slider rather than snapping it on/off.
    lv_anim_delete(slider_vol, im_anim_obj_opa);
    lv_anim_t s;
    lv_anim_init(&s);
    lv_anim_set_var(&s, slider_vol);
    lv_anim_set_duration(&s, 240);
    lv_anim_set_exec_cb(&s, im_anim_obj_opa);
    lv_anim_set_path_cb(&s, lv_anim_path_ease_out);
    if (open) {
        lv_obj_set_style_opa(slider_vol, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(slider_vol, LV_OBJ_FLAG_HIDDEN);
        lv_anim_set_values(&s, 0, LV_OPA_COVER);
    } else {
        lv_anim_set_values(&s, LV_OPA_COVER, 0);
        lv_anim_set_completed_cb(&s, [](lv_anim_t* a) {
            lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN);
        });
    }
    lv_anim_start(&s);

    // Glide the icon between its resting place and the head of the volume row.
    lv_anim_delete(btn_mute, im_anim_x);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, btn_mute);
    lv_anim_set_duration(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, im_anim_x);
    lv_anim_set_values(&a, lv_obj_get_x(btn_mute), open ? SX(IM_VOL_ICON_X) : SX(IM_MUTE_X));
    lv_anim_start(&a);
}

// Any interaction with the volume keeps it on screen.
static void im_vol_poke(void) {
    if (im_vol_open) im_vol_close_at = lv_tick_get() + IM_VOL_HOLD_MS;
}

// Drives the stage. updateLyricsDisplay() owns the timing and advances
// current_lyric_index; we only react to it changing.
static void im_tick(lv_timer_t*) {
    // Auto-close the volume fly-out.
    if (im_vol_open && im_vol_close_at && lv_tick_get() > im_vol_close_at) im_vol_set_open(false);

    if (!im_stage) return;

    bool have = lyrics_enabled && lyrics_ready && lyric_count > 0;
    if (have) {
        if (current_lyric_index != im_last_idx) {
            im_last_idx = current_lyric_index;
            const char* t = lyricsCurrentText();
            if (t && t[0]) {
                im_show(t, true);
                // Each line re-arms the deadline, so the stage only clears once the
                // lines stop coming (instrumental break, or the song ends).
                im_fade_at = lv_tick_get() + IM_LYRIC_HOLD_MS;
            } else {
                im_stage_fade_out();     // gap between lines
            }
        }
        if (im_fade_at && lv_tick_get() > im_fade_at) im_stage_fade_out();
        return;
    }

    // No lyrics for this track (or lyrics switched off): show the title briefly so
    // the stage still carries the design, then let it fade so the screen settles
    // into clean colour instead of parking text there forever.
    im_last_idx = -999;   // re-animate when lyrics do arrive
    const char* t = lbl_title ? lv_label_get_text(lbl_title) : "";
    if (t && strcmp(t, im_last_txt) != 0) {
        strncpy(im_last_txt, t, sizeof(im_last_txt) - 1);
        im_last_txt[sizeof(im_last_txt) - 1] = '\0';
        im_show(t, false);
        im_fade_at = lv_tick_get() + IM_TITLE_HOLD_MS;
    }
    if (im_fade_at && lv_tick_get() > im_fade_at) im_stage_fade_out();
}

// The stage timer outlives nothing — kill it with the screen it draws on.
static void im_screen_deleted(lv_event_t*) {
    if (im_timer) { lv_timer_delete(im_timer); im_timer = nullptr; }
    im_stage = nullptr;
    im_last_idx = -999;
    im_last_txt[0] = '\0';
}

// ── Small builders ──────────────────────────────────────────────────────────
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

static lv_obj_t* roundBtn(lv_obj_t* parent, const char* icon, const lv_font_t* font,
                          int x, int y, int size, lv_event_cb_t cb, bool outlined) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, SMIN(size), SMIN(size));
    lv_obj_set_pos(b, SX(x), SY(y));
    lv_obj_set_style_radius(b, SMIN(size / 2), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (outlined) {
        lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
    } else {
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b, 0, 0);
    }
    pressScale(b);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico = lv_label_create(b);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, font, 0);
    lv_obj_set_style_text_color(ico, COL_TEXT, 0);
    lv_obj_center(ico);
    return b;
}

// Off-canvas parking spot for widgets this layout doesn't use but must create.
static void park(lv_obj_t* o) {
    if (!o) return;
    lv_obj_set_pos(o, SX(900), SY(600));
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ── Builder ─────────────────────────────────────────────────────────────────
void buildImmersivePlayer() {
    im_stage = nullptr; im_timer = nullptr;
    im_last_idx = -999; im_last_txt[0] = '\0';
    im_fade_at = 0;
    im_vol_open = false; im_vol_close_at = 0;
    im_prog_group[0] = im_prog_group[1] = im_prog_group[2] = nullptr;

    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x1b1b1b), 0);   // until the first art colour lands
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr_main, im_screen_deleted, LV_EVENT_DELETE, NULL);

    // Created for API compatibility only — this theme paints a solid backdrop, so
    // themeUsesBlurBg() keeps the blurred-art upload off and this stays hidden.
    img_blur_bg = lv_img_create(scr_main);
    lv_obj_set_size(img_blur_bg, SX(800), SY(480));
    lv_obj_set_pos(img_blur_bg, 0, 0);
    lv_obj_add_flag(img_blur_bg, LV_OBJ_FLAG_HIDDEN);

    // Two fullscreen transparent layers, matching Classic's parenting so
    // setLineInMode()/setTvAudioMode() keep working untouched.
    // panel_art below (artwork, mode heroes, lyric stage), panel_right above
    // (header + bar) so its buttons receive the touches.
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

    // ── Header: artwork ─────────────────────────────────────────────────────
    img_album = lv_img_create(panel_art);
    lv_obj_set_size(img_album, SMIN(IM_ART), SMIN(IM_ART));
    lv_obj_set_pos(img_album, SX(IM_MARGIN), SY(IM_HEAD_Y));
    // Square artwork, no blur shadow — see the note in ui_theme_ambient.cpp:
    // shadow_width is a blur radius, so it always renders rounded corners around a
    // square image. A 1px outline gives definition without that artefact.
    lv_obj_set_style_radius(img_album, 0, 0);
    lv_obj_set_style_shadow_width(img_album, 0, 0);

    art_placeholder = lv_label_create(panel_art);
    lv_label_set_text(art_placeholder, MDI_MUSIC_NOTE);
    lv_obj_set_style_text_font(art_placeholder, &lv_font_mdi_32, 0);
    lv_obj_set_style_text_color(art_placeholder, COL_TEXT, 0);
    lv_obj_set_style_text_opa(art_placeholder, LV_OPA_60, 0);
    // Centred on the artwork square.
    lv_obj_set_pos(art_placeholder, SX(IM_MARGIN + IM_ART / 2 - 16), SY(IM_HEAD_Y + IM_ART / 2 - 16));

    // Mode heroes (line-in / TV) — centred, shown by the mode handlers.
    struct { lv_obj_t** icon; lv_obj_t** sub; const char* glyph; const char* text; } modes[] = {
        { &lbl_linein_icon, &lbl_linein_subtitle, MDI_WAVEFORM,    "LIVE AUDIO" },
        { &lbl_tv_icon,     &lbl_tv_subtitle,     MDI_TELEVISION,  "TV AUDIO"   },
    };
    for (auto& m : modes) {
        *m.icon = lv_label_create(panel_art);
        lv_label_set_text(*m.icon, m.glyph);
        lv_obj_set_style_text_font(*m.icon, &lv_font_mdi_80, 0);
        lv_obj_set_style_text_color(*m.icon, COL_TEXT, 0);
        lv_obj_align(*m.icon, LV_ALIGN_CENTER, 0, SY(-40));
        lv_obj_add_flag(*m.icon, LV_OBJ_FLAG_HIDDEN);

        *m.sub = lv_label_create(panel_art);
        lv_label_set_text(*m.sub, m.text);
        lv_obj_set_style_text_font(*m.sub, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(*m.sub, COL_TEXT, 0);
        lv_obj_set_style_text_letter_space(*m.sub, 3, 0);
        lv_obj_align(*m.sub, LV_ALIGN_CENTER, 0, SY(38));
        lv_obj_add_flag(*m.sub, LV_OBJ_FLAG_HIDDEN);
    }

    // ── Header: text block ──────────────────────────────────────────────────
    // Width stops short of the two 46px buttons + gap on the right.
    const int head_text_w = (IM_RIGHT - 2 * 46 - 12 - 16) - IM_TEXT_X;

    lbl_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_title, SX(IM_TEXT_X), SY(IM_HEAD_Y + 6));
    lv_obj_set_size(lbl_title, SX(head_text_w), SY(40));
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_title, "Not Playing");
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);

    lbl_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_artist, SX(IM_TEXT_X), SY(IM_HEAD_Y + 52));
    lv_obj_set_size(lbl_artist, SX(head_text_w), SY(26));
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_color(lbl_artist, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_artist, LV_OPA_80, 0);
    lv_obj_set_style_text_font(lbl_artist, &lv_font_montserrat_20, 0);

    lbl_device_name = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_device_name, SX(IM_TEXT_X), SY(IM_HEAD_Y + 82));
    lv_label_set_text(lbl_device_name, "Now Playing");
    lv_obj_set_style_text_color(lbl_device_name, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_device_name, LV_OPA_60, 0);
    lv_obj_set_style_text_font(lbl_device_name, &lv_font_montserrat_14, 0);

    // Right-aligned header buttons: settings hugs the right margin, queue sits 12 left.
    roundBtn(panel_right, MDI_COG, &lv_font_mdi_24, IM_RIGHT - 46, IM_HEAD_Y + 6, 46, ev_settings, true);
    btn_queue = roundBtn(panel_right, MDI_PLAYLIST, &lv_font_mdi_24,
                         IM_RIGHT - 46 - 12 - 46, IM_HEAD_Y + 6, 46, ev_queue, true);
    lv_obj_set_ext_click_area(btn_queue, 8);

    lbl_lyrics_status = lv_label_create(panel_right);
    lv_label_set_text(lbl_lyrics_status, "");
    lv_obj_set_pos(lbl_lyrics_status, SX(IM_TEXT_X), SY(IM_HEAD_Y + 104));
    lv_obj_set_style_text_color(lbl_lyrics_status, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_lyrics_status, LV_OPA_50, 0);
    lv_obj_set_style_text_font(lbl_lyrics_status, &lv_font_montserrat_14, 0);

    // ── Lyric stage ─────────────────────────────────────────────────────────
    im_stage = lv_label_create(panel_art);
    lv_obj_set_pos(im_stage, SX(IM_MARGIN), SY(IM_STAGE_Y + 40));
    lv_obj_set_width(im_stage, SX(IM_CONTENT_W));
    lv_label_set_long_mode(im_stage, LV_LABEL_LONG_WRAP);
    lv_label_set_text(im_stage, "");
    lv_obj_set_style_text_color(im_stage, COL_TEXT, 0);
    lv_obj_set_style_text_font(im_stage, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_opa(im_stage, LV_OPA_90, 0);
    lv_obj_set_style_text_line_space(im_stage, 4, 0);

    // updateLyricsDisplay() must keep running to advance current_lyric_index, and
    // it no-ops unless the overlay exists — so create it into a hidden sink and
    // let the stage above do the visible work.
    lv_obj_t* sink = lv_obj_create(panel_art);
    lv_obj_set_size(sink, SX(2), SY(2));
    lv_obj_set_pos(sink, SX(900), SY(600));
    lv_obj_set_style_bg_opa(sink, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sink, 0, 0);
    lv_obj_add_flag(sink, LV_OBJ_FLAG_HIDDEN);
    createLyricsOverlay(sink);

    im_timer = lv_timer_create(im_tick, 120, NULL);

    // ── Control bar — flat, flush to the bottom edge ────────────────────────
    lv_obj_t* bar = lv_obj_create(panel_right);
    lv_obj_set_size(bar, SX(800), SY(IM_BAR_H));
    lv_obj_set_pos(bar, 0, SY(IM_BAR_Y));
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0C0C0C), 0);
    lv_obj_set_style_bg_opa(bar, 232, 0);
    lv_obj_set_style_radius(bar, 0, 0);          // square: a rounded bottom edge looked wrong
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lbl_time = lv_label_create(bar);
    lv_obj_set_pos(lbl_time, SX(IM_MARGIN), SY(IM_BAR_MID(17)));
    lv_label_set_text(lbl_time, "0:00");
    lv_obj_set_style_text_color(lbl_time, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_time, LV_OPA_80, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_14, 0);

    slider_progress = lv_slider_create(bar);
    lv_obj_set_pos(slider_progress, SX(IM_ROW_X), SY(IM_BAR_MID(6)));
    lv_obj_set_size(slider_progress, SX(IM_ROW_W), SY(6));
    lv_slider_set_range(slider_progress, 0, 100);
    lv_obj_set_style_bg_color(slider_progress, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_progress, COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_progress, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_progress, ev_progress, LV_EVENT_ALL, NULL);

    lbl_time_remaining = lv_label_create(bar);
    lv_obj_set_pos(lbl_time_remaining, SX(340), SY(IM_BAR_MID(17)));
    lv_label_set_text(lbl_time_remaining, "-0:00");
    lv_obj_set_style_text_color(lbl_time_remaining, COL_TEXT, 0);
    lv_obj_set_style_text_opa(lbl_time_remaining, LV_OPA_60, 0);
    lv_obj_set_style_text_font(lbl_time_remaining, &lv_font_montserrat_14, 0);

    // Transport: prev / play / next on a 64px pitch, centred in the space between
    // the time readout and the volume icon.
    btn_prev = roundBtn(bar, MDI_SKIP_PREV, &lv_font_mdi_32, 466, IM_BAR_MID(44), 44, ev_prev, false);

    btn_play = lv_btn_create(bar);
    lv_obj_set_size(btn_play, SMIN(60), SMIN(60));
    lv_obj_set_pos(btn_play, SX(522), SY(IM_BAR_MID(60)));
    lv_obj_set_style_bg_color(btn_play, g_ambient_bright, 0);
    lv_obj_set_style_radius(btn_play, SMIN(30), 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    pressScale(btn_play);
    lv_obj_add_event_cb(btn_play, ev_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_play = lv_label_create(btn_play);
    lv_label_set_text(ico_play, MDI_PAUSE);
    lv_obj_set_style_text_font(ico_play, &lv_font_mdi_32, 0);
    lv_obj_set_style_text_color(ico_play, lv_color_hex(0x111111), 0);
    lv_obj_center(ico_play);

    btn_next = roundBtn(bar, MDI_SKIP_NEXT, &lv_font_mdi_32, 594, IM_BAR_MID(44), 44, ev_next, false);

    // Volume: just an icon until tapped. Tapping slides it to the head of the row
    // and reveals the slider across the full width; tapping again while open
    // toggles mute (so mute is still reachable). Auto-closes after IM_VOL_HOLD_MS.
    btn_mute = roundBtn(bar, MDI_VOLUME_HIGH, &lv_font_mdi_32, IM_MUTE_X, IM_BAR_MID(44), 44, nullptr, false);
    lv_obj_add_event_cb(btn_mute, [](lv_event_t* e) {
        if (im_vol_open) { ev_mute(e); im_vol_poke(); }
        else             { im_vol_set_open(true); }
    }, LV_EVENT_CLICKED, NULL);

    // Shares the progress row's slot — only one of the two is visible at a time.
    slider_vol = lv_slider_create(bar);
    lv_obj_set_pos(slider_vol, SX(IM_ROW_X), SY(IM_BAR_MID(6)));
    lv_obj_set_size(slider_vol, SX(IM_ROW_W + 120), SY(6));
    lv_slider_set_range(slider_vol, 0, 100);
    lv_obj_set_style_bg_color(slider_vol, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_vol, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_vol, 4, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider_vol, 14);   // easier to grab a 6px bar
    lv_obj_add_event_cb(slider_vol, ev_vol_slider, LV_EVENT_ALL, NULL);
    // Only real touches keep it alive. Registering this on LV_EVENT_ALL also caught
    // draw/refresh events, which re-armed the timer every frame — so the bar never
    // closed. Restricted to the interaction codes.
    lv_obj_add_event_cb(slider_vol, [](lv_event_t* e) {
        switch (lv_event_get_code(e)) {
            case LV_EVENT_PRESSED:
            case LV_EVENT_PRESSING:
            case LV_EVENT_RELEASED:
            case LV_EVENT_VALUE_CHANGED: im_vol_poke(); break;
            default: break;
        }
    }, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(slider_vol, LV_OBJ_FLAG_HIDDEN);   // hidden until the icon is tapped

    // Watched by im_vol_set_open() so the volume row can take their place.
    im_prog_group[0] = lbl_time;
    im_prog_group[1] = slider_progress;
    im_prog_group[2] = lbl_time_remaining;

    // ── Created but unused by this layout ───────────────────────────────────
    // updateUI() writes to all of these unconditionally, so they must exist.
    btn_shuffle = roundBtn(bar, MDI_SHUFFLE, &lv_font_mdi_32, 0, 0, 44, ev_shuffle, false);
    btn_repeat  = roundBtn(bar, MDI_REPEAT,  &lv_font_mdi_32, 0, 0, 44, ev_repeat,  false);
    park(btn_shuffle);
    park(btn_repeat);

    lbl_album = lv_label_create(panel_right);
    lv_label_set_text(lbl_album, "");
    lv_obj_set_style_text_font(lbl_album, &lv_font_montserrat_14, 0);
    park(lbl_album);

    img_next_album = lv_img_create(panel_right);
    lv_obj_set_size(img_next_album, SMIN(40), SMIN(40));
    park(img_next_album);

    lbl_next_header = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_header, "Next:");
    lv_obj_set_style_text_font(lbl_next_header, &lv_font_montserrat_12, 0);
    park(lbl_next_header);

    lbl_next_title = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_title, "");
    lv_obj_set_width(lbl_next_title, SX(200));
    lv_obj_set_style_text_font(lbl_next_title, &lv_font_montserrat_14, 0);
    park(lbl_next_title);

    lbl_next_artist = lv_label_create(panel_right);
    lv_label_set_text(lbl_next_artist, "");
    lv_obj_set_width(lbl_next_artist, SX(200));
    lv_obj_set_style_text_font(lbl_next_artist, &lv_font_montserrat_12, 0);
    park(lbl_next_artist);
}
