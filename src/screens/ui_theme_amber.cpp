/**
 * "Amber" player theme — artboard 1a of "SonosESP Amber".
 *
 * A flat two-column panel: artwork edge to edge down the left with a shelf
 * beneath it, and every control permanently visible on the right. Nothing here
 * tints from the album; the ground is fixed (THEME_BG_FLAT) and gold is the only
 * action colour.
 *
 * ── What the canvas changed, and why it is built this way ──────────────────
 *   - Artwork runs to the panel edges: no margin, no rounded card floating on
 *     black. The registry row therefore places it at the origin at 344 square.
 *   - The title WRAPS to two lines and ellipsises rather than side-scrolling.
 *     The other themes use LV_LABEL_LONG_SCROLL_CIRCULAR, which walks a long
 *     title past the edge forever; the canvas note calls that out specifically.
 *   - Lyrics live in the artwork column's shelf instead of taking the screen, so
 *     the transport never disappears behind a lyric.
 *   - The shelf shows Next-up OR lyrics, never both — see shelfSwapCb().
 *
 * ── Grid (800x480 design space, wrapped in SX/SY/SMIN) ──────────────────────
 *   left column   x   0 .. 344   artwork 344 square at the origin
 *   shelf         y 344 .. 480   next-up / lyrics
 *   right column  x 370 .. 778
 *   transport     y 288, 78 tall, space-between across the column
 *
 * CONTRACT (ui_theme.h): a builder MUST assign every player widget global —
 * updateUI() and the line-in/TV handlers dereference them without null checks.
 * Widgets this layout does not show are created and parked off-canvas.
 */

#include "ui_common.h"
#include "lyrics.h"
#include "ui_icons.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "amber_icons.h"
#include "amber.h"

// ── Grid ────────────────────────────────────────────────────────────────────
#define AP_ART        344                  // artwork column width AND art edge
#define AP_SHELF_Y    AP_ART               // 344
#define AP_SHELF_H    (480 - AP_ART)       // 136
#define AP_SHELF_PAD  20

#define AP_R          370                  // right column content origin
#define AP_RIGHT      778
#define AP_RW         (AP_RIGHT - AP_R)    // 408

#define AP_HEAD_Y     18
#define AP_HEAD_H     44
#define AP_ARTIST_Y   92
#define AP_TITLE_Y    114
#define AP_TITLE_H    78
#define AP_ALBUM_Y    198
#define AP_PROG_Y     230
#define AP_TIME_Y     244
#define AP_CTRL_Y     288
#define AP_CTRL_H     78
#define AP_VOL_Y      404

static lv_obj_t* ap_shelf_next = nullptr;   // the "NEXT" block
static lv_obj_t* ap_lyric_slot = nullptr;   // the lyrics overlay's wrapper
static lv_obj_t* ap_lyric_cur  = nullptr;   // the current line, auto-fitted
static lv_timer_t* ap_shelf_timer = nullptr;

// Defined below, next to the reasoning for it; used by shelfSwapCb() above it.
static void spFitLyric(lv_obj_t* lbl, const char* text);

// ── Shelf ownership ─────────────────────────────────────────────────────────
// The canvas shows Next-up and the lyrics as mutually exclusive occupants of the
// same shelf (`shelfNext: !s.lyrics`), but nothing in the project broadcasts a
// "lyrics became visible" event: setLyricsVisible() just toggles the overlay's
// hidden flag, and updateUI() independently toggles the four lbl_next_* widgets.
// Polling the overlay's flag is the only way to reconcile the two without
// reaching into lyrics.cpp or updateUI(), both of which every other theme shares.
// 200ms is far below the rate either side actually changes.
static void shelfSwapCb(lv_timer_t*) {
    if (!ap_shelf_next || !ap_lyric_slot) return;
    lv_obj_t* lyr = lv_obj_get_child(ap_lyric_slot, 0);
    const bool lyrics_showing = lyr && !lv_obj_has_flag(lyr, LV_OBJ_FLAG_HIDDEN);
    if (lyrics_showing) lv_obj_add_flag(ap_shelf_next, LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_remove_flag(ap_shelf_next, LV_OBJ_FLAG_HIDDEN);

    // Refit when the line changes. Compared by content rather than by index so a
    // repeated line (choruses repeat) does not re-measure, and a track change
    // that lands on the same index does.
    if (!lyrics_showing || !ap_lyric_cur) return;
    static String last;
    const char* cur = lyricsCurrentText();
    if (cur && last != cur) {
        last = cur;
        spFitLyric(ap_lyric_cur, cur);
    }
}

// ── Fitting the lyric to its box ────────────────────────────────────────────
// A lyric line is whatever the songwriter wrote — "Now I see your thinned face at
// the window" is 44 characters, and plenty run to 60. At 20px in a 304px column
// that is three rows, and a fixed font can only answer by ellipsising, which is
// the worst outcome available: the END of the line is the part that rhymes, and
// it is the part "..." eats.
//
// So the font is chosen per line instead. Measure the string at each size and
// take the largest that fits the box in full. Most lines keep 20px, a long one
// steps to 16, a very long one to 14 — and at 14px in three rows the box holds
// roughly 165 characters, which no sung line reaches. In practice nothing
// truncates any more.
//
// Cheap enough to do per line change: lv_text_get_size() is a glyph-metric walk
// over ~50 characters, and lines change every few seconds at most.
static void spFitLyric(lv_obj_t* lbl, const char* text) {
    if (!lbl || !text || !*text) return;

    static const lv_font_t* const ladder[] = {
        &font_text_20, &font_text_16, &font_text_14, &font_text_12
    };
    const int32_t w = lv_obj_get_width(lbl);
    const int32_t h = lv_obj_get_height(lbl);
    if (w <= 0 || h <= 0) return;   // not laid out yet

    for (uint8_t i = 0; i < sizeof(ladder) / sizeof(ladder[0]); i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, text, ladder[i], 0, 0, w, LV_TEXT_FLAG_NONE);
        if (sz.y <= h || i == sizeof(ladder) / sizeof(ladder[0]) - 1) {
            lv_obj_set_style_text_font(lbl, ladder[i], 0);
            return;
        }
    }
}

// The timer outlives no screen: themeSet() deletes the old scr_main wholesale,
// so it has to be torn down with it or it fires on freed widgets.
static void ap_screen_deleted(lv_event_t*) {
    if (ap_shelf_timer) { lv_timer_del(ap_shelf_timer); ap_shelf_timer = nullptr; }
    ap_shelf_next = nullptr;
    ap_lyric_slot = nullptr;
    ap_lyric_cur  = nullptr;
}

// ── Helpers ─────────────────────────────────────────────────────────────────
static void pressFade(lv_obj_t* b) {
    lv_obj_set_style_bg_color(b, AMB_RAISED, LV_STATE_PRESSED);
}

// Round icon button: the 44px card-backed chips in the header, and the bare
// transport glyphs, differ only in whether they carry a surface.
static lv_obj_t* roundBtn(lv_obj_t* parent, const char* icon, const lv_font_t* font,
                          int x, int y, int d, lv_event_cb_t cb, bool carded,
                          lv_color_t icon_col) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, SMIN(d), SMIN(d));
    lv_obj_set_pos(b, SX(x), SY(y));
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (carded) {
        lv_obj_set_style_bg_color(b, AMB_CARD, 0);
        lv_obj_set_style_border_color(b, AMB_BORDER, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        pressFade(b);
    } else {
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b, 0, 0);
    }
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ico = lv_label_create(b);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, font, 0);
    lv_obj_set_style_text_color(ico, icon_col, 0);
    lv_obj_center(ico);
    return b;
}

// Progress and volume share a spec: 6px groove, 18px knob ringed in the ground
// colour so it reads as lifted off the track.
static lv_obj_t* amberSlider(lv_obj_t* parent, int x, int y, int w,
                              lv_color_t fill, lv_event_cb_t cb) {
    lv_obj_t* s = lv_slider_create(parent);
    lv_obj_set_pos(s, SX(x), SY(y));
    lv_obj_set_size(s, SX(w), SY(6));
    lv_slider_set_range(s, 0, 100);
    lv_obj_set_style_bg_color(s, AMB_BORDER, LV_PART_MAIN);
    lv_obj_set_style_radius(s, SMIN(3), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, fill, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s, SMIN(3), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, fill, LV_PART_KNOB);
    lv_obj_set_style_border_color(s, AMB_BG, LV_PART_KNOB);
    lv_obj_set_style_border_width(s, SMIN(3), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, SMIN(6), LV_PART_KNOB);
    if (cb) lv_obj_add_event_cb(s, cb, LV_EVENT_ALL, NULL);
    return s;
}

static void park(lv_obj_t* o) {
    if (!o) return;
    lv_obj_set_pos(o, SX(900), SY(600));
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ── Builder ─────────────────────────────────────────────────────────────────
void buildAmberPlayer() {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, AMB_BG, 0);
    lv_obj_set_style_pad_all(scr_main, 0, 0);
    lv_obj_set_style_border_width(scr_main, 0, 0);
    lv_obj_remove_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr_main, ap_screen_deleted, LV_EVENT_DELETE, nullptr);

    // Created for API compatibility — THEME_BG_FLAT keeps the blurred art off.
    img_blur_bg = lv_image_create(scr_main);
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
        lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
        return p;
    };
    panel_art   = mkLayer();
    panel_right = mkLayer();

    // ── Left column ground ──────────────────────────────────────────────────
    lv_obj_t* col = ambRect(panel_art, AP_ART, 480, AMB_BG_ART);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_t* col_edge = ambRect(panel_art, 1, 480, AMB_LINE);
    lv_obj_set_pos(col_edge, SX(AP_ART), 0);

    // ── Artwork, edge to edge ───────────────────────────────────────────────
    img_album = lv_image_create(panel_art);
    lv_obj_set_size(img_album, SMIN(AP_ART), SMIN(AP_ART));
    lv_obj_set_pos(img_album, 0, 0);
    lv_obj_set_style_radius(img_album, 0, 0);
    lv_obj_set_style_shadow_width(img_album, 0, 0);
    lv_obj_set_style_border_width(img_album, 0, 0);

    art_placeholder = lv_label_create(panel_art);
    lv_label_set_text(art_placeholder, AMB_IC_MUSIC);
    lv_obj_set_style_text_font(art_placeholder, &font_icon_32, 0);
    lv_obj_set_style_text_color(art_placeholder, AMB_TEXT3, 0);
    lv_obj_set_pos(art_placeholder, SX(AP_ART / 2 - 16), SY(AP_ART / 2 - 16));

    // Mode heroes (line-in / TV), centred on the artwork square.
    struct { lv_obj_t** icon; lv_obj_t** sub; const char* glyph; const char* text; } modes[] = {
        { &lbl_linein_icon, &lbl_linein_subtitle, MDI_WAVEFORM,   "LIVE AUDIO" },
        { &lbl_tv_icon,     &lbl_tv_subtitle,     MDI_TELEVISION, "TV AUDIO"   },
    };
    for (auto& m : modes) {
        *m.icon = lv_label_create(panel_art);
        lv_label_set_text(*m.icon, m.glyph);
        lv_obj_set_style_text_font(*m.icon, &lv_font_mdi_80, 0);
        lv_obj_set_style_text_color(*m.icon, AMB_ACCENT, 0);
        lv_obj_set_pos(*m.icon, SX(AP_ART / 2 - 40), SY(AP_ART / 2 - 60));
        lv_obj_add_flag(*m.icon, LV_OBJ_FLAG_HIDDEN);

        *m.sub = lv_label_create(panel_art);
        lv_label_set_text(*m.sub, m.text);
        lv_obj_set_style_text_font(*m.sub, &font_text_14, 0);
        lv_obj_set_style_text_color(*m.sub, AMB_TEXT3, 0);
        lv_obj_set_style_text_letter_space(*m.sub, 3, 0);
        lv_obj_set_pos(*m.sub, SX(AP_ART / 2 - 44), SY(AP_ART / 2 + 40));
        lv_obj_add_flag(*m.sub, LV_OBJ_FLAG_HIDDEN);
    }

    // ── Shelf: the divider, then Next-up and lyrics stacked in the same box ──
    lv_obj_t* shelf_rule = ambRect(panel_art, AP_ART, 1, AMB_LINE);
    lv_obj_set_pos(shelf_rule, 0, SY(AP_SHELF_Y));

    ap_shelf_next = lv_obj_create(panel_art);
    lv_obj_remove_style_all(ap_shelf_next);
    lv_obj_set_size(ap_shelf_next, SX(AP_ART), SY(AP_SHELF_H));
    lv_obj_set_pos(ap_shelf_next, 0, SY(AP_SHELF_Y));
    lv_obj_remove_flag(ap_shelf_next, LV_OBJ_FLAG_SCROLLABLE);

    const int shelf_w = AP_ART - AP_SHELF_PAD * 2;   // 304

    lbl_next_header = ambCaption(ap_shelf_next, AMB_TEXT3, "NEXT", 3);
    lv_obj_set_pos(lbl_next_header, SX(AP_SHELF_PAD), SY(22));

    lv_obj_t* next_rule = ambRect(ap_shelf_next, shelf_w - 60, 1, AMB_LINE);
    lv_obj_set_pos(next_rule, SX(AP_SHELF_PAD + 52), SY(28));

    lbl_next_title = lv_label_create(ap_shelf_next);
    lv_label_set_text(lbl_next_title, "");
    lv_obj_set_pos(lbl_next_title, SX(AP_SHELF_PAD), SY(44));
    lv_obj_set_width(lbl_next_title, SX(shelf_w));
    lv_label_set_long_mode(lbl_next_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl_next_title, &font_text_16, 0);
    lv_obj_set_style_text_color(lbl_next_title, AMB_TEXT2, 0);

    lbl_next_artist = lv_label_create(ap_shelf_next);
    lv_label_set_text(lbl_next_artist, "");
    lv_obj_set_pos(lbl_next_artist, SX(AP_SHELF_PAD), SY(70));
    lv_obj_set_width(lbl_next_artist, SX(shelf_w));
    lv_label_set_long_mode(lbl_next_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl_next_artist, &font_text_12, 0);
    lv_obj_set_style_text_color(lbl_next_artist, AMB_TEXT3, 0);

    // The canvas shows a 40px thumbnail of the next track here. There is no
    // second decoded bitmap to point it at — the art task decodes only the
    // current track — so it stays parked, as it is on Ambient and Immersive.
    img_next_album = lv_image_create(ap_shelf_next);
    lv_obj_set_size(img_next_album, SMIN(40), SMIN(40));
    park(img_next_album);

    // Lyrics occupy the same shelf. createLyricsOverlay() bottom-aligns itself
    // inside its parent, so a positioned wrapper places it without touching
    // lyrics.cpp.
    ap_lyric_slot = lv_obj_create(panel_art);
    lv_obj_remove_style_all(ap_lyric_slot);
    lv_obj_set_size(ap_lyric_slot, SX(AP_ART), SY(AP_SHELF_H));
    lv_obj_set_pos(ap_lyric_slot, 0, SY(AP_SHELF_Y));
    lv_obj_remove_flag(ap_lyric_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ap_lyric_slot, LV_OBJ_FLAG_CLICKABLE);
    createLyricsOverlay(ap_lyric_slot);
    if (lv_obj_t* lyr = lv_obj_get_child(ap_lyric_slot, 0)) {
        // Flatten the overlay's own dark gradient: the shelf already has a
        // surface, and here the lyrics are not sitting over artwork.
        lv_obj_set_style_bg_opa(lyr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_grad_opa(lyr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(lyr, 0, 0);
        lv_obj_set_style_pad_all(lyr, 0, 0);
        lv_obj_set_size(lyr, SX(AP_ART), SY(AP_SHELF_H - 14));
        // Pin it to the top of the slot. createLyricsOverlay() bottom-aligns with
        // a -30 offset, which for a 122-tall block in a 136-tall slot put its top
        // edge 16px ABOVE the slot — so the first row was clipped by the shelf's
        // own boundary before anything else got a say.
        lv_obj_align(lyr, LV_ALIGN_TOP_LEFT, 0, SY(8));
        lv_obj_set_style_pad_left(lyr, SX(AP_SHELF_PAD), 0);
        lv_obj_set_style_pad_right(lyr, SX(AP_SHELF_PAD), 0);
        lv_obj_set_style_pad_row(lyr, SY(4), 0);

        // Children are prev / current / next, in creation order. The canvas sets
        // the current line at 21/600 with the neighbours at 13.
        //
        // ── Long lines ──────────────────────────────────────────────────────
        // The shared overlay uses LV_LABEL_LONG_SCROLL_CIRCULAR, which side-
        // scrolls anything too wide. For a lyric that is exactly wrong: the line
        // is only on screen for a few seconds, so a scroll means you are reading
        // a moving target and the end arrives after the line has already changed.
        //
        // LONG_DOT wraps within the label's width and ellipsises once it runs out
        // of HEIGHT, so a normal line sits still, a long one takes the second row,
        // and a pathological one truncates rather than animating. The current line
        // gets two rows; the neighbours get one each, which is all the shelf has.
        //
        // ── Two lines, not three ────────────────────────────────────────────
        // The PREVIOUS line is hidden. The shelf is 136 tall and the overlay
        // bottom-aligns inside it, so with three lines the block grew upward and
        // the previous line's descenders were clipped by the shelf's top edge —
        // which is the row of half-characters showing above the current lyric.
        // It is also the least useful of the three: it has already been sung.
        // Ambient hides it for the same reason.
        //
        // The current line is PURE WHITE rather than AMB_TEXT (#F5F1EA). Against
        // #0E0D0C at 20px this is the one string on the panel worth the extra
        // contrast, and the neighbours sit a tier down so the eye lands on it.
        // THREE rows for the current line, not two, and the font is fitted to them
        // per line by spFitLyric(). LONG_DOT stays as a backstop only — at the
        // bottom of the ladder the box holds far more than any sung line, so it
        // should never actually trim.
        const lv_font_t* fonts[3] = { &font_text_12, &font_text_20, &font_text_12 };
        const lv_color_t cols[3]  = { AMB_TEXT3, AMB_TEXT_HI, AMB_TEXT2 };
        const int        rows[3]  = { 0,             78,            20 };
        for (int i = 0; i < 3; i++) {
            lv_obj_t* l = lv_obj_get_child(lyr, i);
            if (!l) continue;
            if (i == 0) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); continue; }
            lv_obj_set_style_text_font(l, fonts[i], 0);
            lv_obj_set_style_text_color(l, cols[i], 0);
            lv_obj_set_size(l, SX(shelf_w), SY(rows[i]));
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        }
        ap_lyric_cur = lv_obj_get_child(lyr, 1);
    }

    lbl_lyrics_status = lv_label_create(panel_art);
    lv_label_set_text(lbl_lyrics_status, "");
    lv_obj_set_pos(lbl_lyrics_status, SX(AP_SHELF_PAD), SY(AP_SHELF_Y + 4));
    lv_obj_set_style_text_color(lbl_lyrics_status, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_lyrics_status, &font_text_12, 0);

    ap_shelf_timer = lv_timer_create(shelfSwapCb, 200, nullptr);

    // ── Right column header ─────────────────────────────────────────────────
    lv_obj_t* pill = lv_button_create(panel_right);
    lv_obj_set_size(pill, SX(210), SY(AP_HEAD_H));
    lv_obj_set_pos(pill, SX(AP_R), SY(AP_HEAD_Y));
    lv_obj_set_style_radius(pill, SMIN(AP_HEAD_H / 2), 0);
    lv_obj_set_style_bg_color(pill, AMB_CARD, 0);
    lv_obj_set_style_border_color(pill, AMB_BORDER, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    pressFade(pill);
    lv_obj_add_event_cb(pill, ev_devices, LV_EVENT_CLICKED, NULL);

    // Green is reserved for live playback state — this dot is the only place it
    // appears on the player.
    lv_obj_t* dot = ambRoundRect(pill, 7, 7, 4, AMB_LIVE);
    lv_obj_set_pos(dot, SX(14), SY(AP_HEAD_H / 2 - 4));

    lbl_device_name = lv_label_create(pill);
    lv_label_set_text(lbl_device_name, "Now Playing");
    lv_obj_set_pos(lbl_device_name, SX(29), SY(13));
    lv_obj_set_size(lbl_device_name, SX(150), SY(20));
    lv_label_set_long_mode(lbl_device_name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(lbl_device_name, AMB_TEXT, 0);
    lv_obj_set_style_text_font(lbl_device_name, &font_text_14, 0);

    lv_obj_t* chev = lv_label_create(pill);
    lv_label_set_text(chev, AMB_IC_CHEV);
    lv_obj_set_style_text_font(chev, &font_icon_16, 0);
    lv_obj_set_style_text_color(chev, AMB_TEXT3, 0);
    lv_obj_set_pos(chev, SX(185), SY(14));

    // LRC / queue / settings, right-aligned in that order.
    const int chip = AP_HEAD_H, gap = 10;
    // An INDICATOR, not a control. It briefly toggled lyrics, but that setting
    // already lives in Settings > General and two places to change one thing is
    // an invitation for them to disagree. What it is actually good for is saying
    // whether THIS track has synced lyrics — which is a thing you cannot
    // otherwise tell until the shelf either fills or does not.
    //
    // No callback, so it gives no press feedback for something it will not do.
    // updateLyricsStatus() lights it; see btn_lyrics in ui_common.h.
    lv_obj_t* lrc = roundBtn(panel_right, "", &font_text_12, AP_RIGHT - chip * 3 - gap * 2,
                             AP_HEAD_Y, chip, NULL, true, AMB_TEXT3);
    lv_obj_remove_flag(lrc, LV_OBJ_FLAG_CLICKABLE);
    btn_lyrics = lrc;
    // A text chip, not a glyph: the MDI set carries no "lyrics" icon and the
    // canvas labels this one "LRC" anyway.
    if (lv_obj_t* l = lv_obj_get_child(lrc, 0)) {
        lv_label_set_text(l, "LRC");
        lv_obj_set_style_text_letter_space(l, 1, 0);
    }

    btn_queue = roundBtn(panel_right, AMB_IC_QUEUE, &font_icon_24,
                         AP_RIGHT - chip * 2 - gap, AP_HEAD_Y, chip, ev_queue, true, AMB_TEXT2);
    lv_obj_set_ext_click_area(btn_queue, 8);
    roundBtn(panel_right, AMB_IC_GEAR, &font_icon_24,
             AP_RIGHT - chip, AP_HEAD_Y, chip, ev_settings, true, AMB_TEXT2);

    // ── Track meta ──────────────────────────────────────────────────────────
    lbl_artist = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_artist, SX(AP_R), SY(AP_ARTIST_Y));
    lv_obj_set_size(lbl_artist, SX(AP_RW), SY(18));
    lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_artist, "");
    lv_obj_set_style_text_color(lbl_artist, AMB_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_artist, &font_text_12, 0);
    lv_obj_set_style_text_letter_space(lbl_artist, 3, 0);

    // WRAP, not SCROLL_CIRCULAR: the canvas note is explicit that the title
    // should break to a second line and truncate rather than scroll past the
    // edge. Two lines of font_text_32 fit AP_TITLE_H exactly.
    lbl_title = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_title, SX(AP_R), SY(AP_TITLE_Y));
    lv_obj_set_size(lbl_title, SX(AP_RW), SY(AP_TITLE_H));
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_title, "Not Playing");
    lv_obj_set_style_text_color(lbl_title, AMB_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &font_text_32, 0);

    lbl_album = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_album, SX(AP_R), SY(AP_ALBUM_Y));
    lv_obj_set_size(lbl_album, SX(AP_RW), SY(20));
    lv_label_set_long_mode(lbl_album, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_album, "");
    lv_obj_set_style_text_color(lbl_album, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_album, &font_text_14, 0);

    // ── Progress ────────────────────────────────────────────────────────────
    slider_progress = amberSlider(panel_right, AP_R, AP_PROG_Y, AP_RW, AMB_ACCENT, ev_progress);

    lbl_time = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time, SX(AP_R), SY(AP_TIME_Y));
    lv_label_set_text(lbl_time, "0:00");
    lv_obj_set_style_text_color(lbl_time, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_time, &font_text_14, 0);

    lbl_time_remaining = lv_label_create(panel_right);
    lv_obj_set_pos(lbl_time_remaining, SX(AP_RIGHT - 60), SY(AP_TIME_Y));
    lv_obj_set_size(lbl_time_remaining, SX(60), SY(18));
    // CLIP, not DOT: this is a clock value in a fixed box and an ellipsised time
    // reads as a glitch.
    lv_label_set_long_mode(lbl_time_remaining, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(lbl_time_remaining, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_time_remaining, "-0:00");
    lv_obj_set_style_text_color(lbl_time_remaining, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_time_remaining, &font_text_14, 0);

    // ── Transport ───────────────────────────────────────────────────────────
    // Widths 44/52/78/52/44 = 270 across 408, so the four gaps are 34.5 each.
    // Laid out from measured offsets rather than a flex row because every widget
    // here is a global that later code positions and restyles by hand.
    const int b0 = AP_R;                    // shuffle 44
    const int b1 = AP_R + 78;               // prev    52
    const int b2 = AP_R + 165;              // play    78
    const int b3 = AP_R + 278;              // next    52
    const int b4 = AP_RIGHT - 44;           // repeat  44

    btn_shuffle = roundBtn(panel_right, AMB_IC_SHUFFLE, &font_icon_32,
                           // Muted by default: updateUI() lights it when shuffle
                           // is actually on. Starting gold claimed shuffle was
                           // enabled before anything had asked the speaker.
                           b0, AP_CTRL_Y + 17, 44, ev_shuffle, false, AMB_TEXT3);
    btn_prev    = roundBtn(panel_right, AMB_IC_PREV, &font_icon_40,
                           b1, AP_CTRL_Y + 13, 52, ev_prev, false, AMB_TEXT);
    btn_next    = roundBtn(panel_right, AMB_IC_NEXT, &font_icon_40,
                           b3, AP_CTRL_Y + 13, 52, ev_next, false, AMB_TEXT);
    btn_repeat  = roundBtn(panel_right, AMB_IC_REPEAT, &font_icon_32,
                           b4, AP_CTRL_Y + 17, 44, ev_repeat, false, AMB_TEXT3);

    btn_play = lv_button_create(panel_right);
    lv_obj_set_size(btn_play, SMIN(AP_CTRL_H), SMIN(AP_CTRL_H));
    lv_obj_set_pos(btn_play, SX(b2), SY(AP_CTRL_Y));
    lv_obj_set_style_bg_color(btn_play, AMB_ACCENT, 0);
    lv_obj_set_style_bg_color(btn_play, AMB_ACCENT_HI, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    lv_obj_set_style_border_width(btn_play, 0, 0);
    lv_obj_add_event_cb(btn_play, ev_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_play = lv_label_create(btn_play);
    lv_label_set_text(ico_play, AMB_IC_PAUSE);
    lv_obj_set_style_text_font(ico_play, &font_icon_40, 0);
    lv_obj_set_style_text_color(ico_play, AMB_ON_ACCENT, 0);
    lv_obj_center(ico_play);

    // ── Volume ──────────────────────────────────────────────────────────────
    btn_mute = roundBtn(panel_right, AMB_IC_VOL, &font_icon_24,
                        AP_R - 4, AP_VOL_Y - 10, 28, ev_mute, false, AMB_TEXT3);

    // Neutral fill, not gold: the canvas keeps one action colour, and volume is
    // not the action on this screen.
    // Narrowed to leave room for the readout the canvas puts at the end of the row.
    slider_vol = amberSlider(panel_right, AP_R + 34, AP_VOL_Y, AP_RW - 34 - 40,
                              AMB_TEXT_BRIGHT, ev_vol_slider);

    // Volume readout. Kept in step from the slider itself rather than from
    // updateUI(), which only writes slider_vol and has no label for this.
    lv_obj_t* lbl_vol = ambLabel(panel_right, &font_text_14, AMB_TEXT3, "--");
    lv_obj_set_width(lbl_vol, SX(32));
    lv_obj_set_style_text_align(lbl_vol, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(lbl_vol, SX(AP_RIGHT - 32), SY(AP_VOL_Y - 6));
    lv_obj_set_user_data(slider_vol, lbl_vol);
    lv_obj_add_event_cb(slider_vol, [](lv_event_t* e) {
        lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* l  = (lv_obj_t*)lv_obj_get_user_data(sl);
        if (l) lv_label_set_text_fmt(l, "%d", (int)lv_slider_get_value(sl));
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Overlays ────────────────────────────────────────────────────────────
    // Created LAST so they sit above both panels with no z-order juggling, and
    // parented to the screen rather than to a panel — setLineInMode() and
    // setTvAudioMode() hide panel children wholesale, which would take an open
    // overlay down with them.
    amberBuildOverlays(scr_main);
}
