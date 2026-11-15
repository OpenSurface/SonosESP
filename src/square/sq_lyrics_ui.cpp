// sq_lyrics_ui.cpp — the Paroles screen: full screen, no dock.
//
// Opened from the cover on Lecture. The current line is big and yellow, the
// lines already sung fade, the next ones wait in lilac; the list scrolls on its
// own so the current line sits at the same height. A finger can scroll it (the
// auto-follow waits a few seconds) or tap a line to jump the music there.
// The background takes the cover's colour, darkened, so it feels like the song.
#include "sq_ui.h"
#include "sq_work.h"
#include "ui_common.h"
#include "position_estimate.h"
#include <esp_heap_caps.h>

#define HDR          72
#define FOOT         76
#define VIEW_H       (SQ_H - HDR - FOOT)   // 332
#define ANCHOR       96      // where the current line's top sits in the view
#define LEAD_MS      150     // light a line a hair early: reading takes a moment
#define HOLD_MS      4000    // auto-follow pauses this long after a finger scroll
#define MISSING_MS   5000    // "no lyrics" stays this long, then back to Lecture
#define COL_NEXT     0xE3DAF5
#define COL_SOFT     0xD9CFF2

static lv_obj_t* s_scr;
static lv_obj_t* s_thumb_box;
static lv_obj_t* s_thumb_img;
static lv_obj_t* s_thumb_ph;
static lv_obj_t* s_title;
static lv_obj_t* s_artist;
static lv_obj_t* s_close;
static lv_obj_t* s_view;        // scrolls
static lv_obj_t* s_col;         // the lines
static lv_obj_t* s_fade_top;
static lv_obj_t* s_fade_bot;
static lv_obj_t* s_msg;
static lv_obj_t* s_msg_circle;
static lv_obj_t* s_msg_icon;
static lv_obj_t* s_msg_title;
static lv_obj_t* s_msg_sub;
static lv_obj_t* s_play_btn;
static lv_obj_t* s_play_icon;
static lv_obj_t* s_bar;
static lv_obj_t* s_bar_fill;
static lv_obj_t* s_time;

static lv_obj_t** s_lbl = nullptr;   // one label per line
static int        s_nlbl = 0;
static bool       s_synced = false;
static uint32_t   s_built = 0xFFFFFFFF;  // sq_lyrics_version on screen
static int        s_cur = -2;            // -2: nothing styled yet
static uint32_t   s_user_ms = 0;         // last finger scroll
static bool       s_refollow = false;    // scroll back once the hold expires
static uint32_t   s_missing_ms = 0;
static String     s_hdr_title;
static String     s_hdr_art;
static uint32_t   s_art_seen = 0;
static uint32_t   s_bg = SQ_BG;
static uint32_t   s_raised = SQ_RAISED;
static bool       s_playing = false;
static uint32_t   s_toggle_ms = 0;
static position_estimate::Estimator s_est;

// ── Colours from the cover ──────────────────────────────────────────────────
static uint32_t mix(uint32_t a, uint32_t b, int pct_a) {
    int r = (((a >> 16) & 0xFF) * pct_a + ((b >> 16) & 0xFF) * (100 - pct_a)) / 100;
    int g = (((a >> 8) & 0xFF) * pct_a + ((b >> 8) & 0xFF) * (100 - pct_a)) / 100;
    int bl = ((a & 0xFF) * pct_a + (b & 0xFF) * (100 - pct_a)) / 100;
    return (r << 16) | (g << 8) | bl;
}

// Average of the cover, as 0xRRGGBB; false when the cover is not decoded yet.
static bool coverAverage(const char* url, uint32_t* out) {
    const lv_image_dsc_t* img = (url && *url) ? sqArtGet(url, 188) : nullptr;
    if (!img || img->header.cf != LV_COLOR_FORMAT_RGB565) return false;
    const uint16_t* px = (const uint16_t*)img->data;
    int n = img->header.w * img->header.h;
    uint32_t r = 0, g = 0, b = 0, k = 0;
    for (int i = 0; i < n; i += 7, ++k) {
        uint16_t c = px[i];
        r += (c >> 11) << 3;
        g += ((c >> 5) & 0x3F) << 2;
        b += (c & 0x1F) << 3;
    }
    if (!k) return false;
    *out = ((r / k) << 16) | ((g / k) << 8) | (b / k);
    return true;
}

static void applyColours(void) {
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(s_bg), 0);
    for (lv_obj_t* f : {s_fade_top, s_fade_bot}) {
        lv_obj_set_style_bg_color(f, lv_color_hex(s_bg), 0);
        lv_obj_set_style_bg_grad_color(f, lv_color_hex(s_bg), 0);
    }
    for (lv_obj_t* o : {s_close, s_msg_circle, s_bar})
        lv_obj_set_style_bg_color(o, lv_color_hex(s_raised), 0);
}

static void themeFromCover(void) {
    uint32_t avg;
    if (coverAverage(s_hdr_art.c_str(), &avg)) {
        // A third of the cover over the night navy: the song's colour, dark
        // enough for white and yellow text.
        s_bg = mix(avg, SQ_BG, 32);
        int r = (s_bg >> 16) & 0xFF, g = (s_bg >> 8) & 0xFF, b = s_bg & 0xFF;
        int mx = max(r, max(g, b));
        if (mx > 92) s_bg = ((r * 92 / mx) << 16) | ((g * 92 / mx) << 8) | (b * 92 / mx);
        s_raised = mix(0xFFFFFF, s_bg, 13);
    } else {
        s_bg = SQ_BG;
        s_raised = SQ_RAISED;
    }
    applyColours();
}

// ── Header ──────────────────────────────────────────────────────────────────
static void thumbShow(void) {
    const lv_image_dsc_t* img = s_hdr_art.length() ? sqArtGet(s_hdr_art.c_str(), 48) : nullptr;
    if (img) lv_image_set_src(s_thumb_img, img);
    sqSetHidden(s_thumb_img, !img);
    sqSetHidden(s_thumb_ph, img != nullptr);
}

static void headerRefresh(bool force) {
    String title = sqPlayerTitle();
    String art = sqPlayerArtUrl();
    if (!force && title == s_hdr_title && art == s_hdr_art) return;
    if (title != s_hdr_title) s_est.reset();
    s_hdr_title = title;
    s_hdr_art = art;
    lv_label_set_text(s_title, title.length() ? title.c_str() : "Rien en cours");
    lv_label_set_text(s_artist, sqPlayerArtist());
    lv_obj_t* old = s_thumb_ph;
    s_thumb_ph = sqCoverPlaceholder(s_thumb_box, 48, 14, (title + sqPlayerArtist()).c_str());
    lv_obj_move_background(s_thumb_ph);
    lv_obj_delete(old);
    thumbShow();
    themeFromCover();
}

// ── Lines ───────────────────────────────────────────────────────────────────
static void styleLine(int i) {
    lv_obj_t* l = s_lbl[i];
    if (!s_synced) return;
    if (i == s_cur) {
        lv_obj_set_style_text_font(l, &font_sq_28, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(SQ_YELLOW), 0);
        lv_obj_set_style_text_opa(l, LV_OPA_COVER, 0);
    } else if (i < s_cur) {
        lv_obj_set_style_text_font(l, &font_sq_20, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(SQ_TEXT), 0);
        lv_obj_set_style_text_opa(l, LV_OPA_40, 0);
    } else {
        lv_obj_set_style_text_font(l, &font_sq_20, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(COL_NEXT), 0);
        lv_obj_set_style_text_opa(l, LV_OPA_COVER, 0);
    }
}

// Scrolls so the current line's top sits at ANCHOR (the top of the list before
// the first line).
static void follow(lv_anim_enable_t anim) {
    if (!s_synced || !s_nlbl) return;
    int32_t y = 0;
    if (s_cur >= 0) {
        lv_obj_update_layout(s_col);
        lv_area_t a, c;
        lv_obj_get_coords(s_lbl[s_cur], &a);
        lv_obj_get_coords(s_col, &c);
        y = (a.y1 - c.y1) - ANCHOR;
    }
    lv_obj_scroll_to_y(s_view, y < 0 ? 0 : y, anim);
}

static void setCurrent(int idx, lv_anim_enable_t anim) {
    int old = s_cur;
    s_cur = idx;
    if (old == -2) {
        for (int i = 0; i < s_nlbl; ++i) styleLine(i);
    } else {
        int lo = max(0, min(old, idx)), hi = min(s_nlbl - 1, max(old, idx));
        for (int i = lo; i <= hi; ++i) styleLine(i);
    }
    if (millis() - s_user_ms > HOLD_MS) follow(anim);
    else s_refollow = true;
}

static void lineClicked(lv_event_t* e) {
    if (!s_synced) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    sqLyricsLock();
    int ms = sqLyricsMs(i);
    sqLyricsUnlock();
    if (ms < 0) return;
    sonos.seek(ms / 1000);
    s_est.onSeek(ms / 1000, millis());
    // The speaker reports TRANSITIONING while it seeks: keep showing Pause.
    s_toggle_ms = millis();
    s_user_ms = 0;
    setCurrent(i, LV_ANIM_ON);
}

static void viewScrollBegin(lv_event_t*) {
    // Only a finger counts: our own animated scrolls start from a timer.
    lv_indev_t* in = lv_indev_active();
    if (in && lv_indev_get_scroll_obj(in) == s_view) s_user_ms = millis();
}

static void showMessage(const char* icon, const char* title, const char* sub) {
    sqSetHidden(s_view, true);
    sqSetHidden(s_fade_top, true);
    sqSetHidden(s_fade_bot, true);
    sqSetHidden(s_msg, false);
    lv_label_set_text(s_msg_icon, icon);
    lv_label_set_text(s_msg_title, title);
    lv_label_set_text(s_msg_sub, sub ? sub : "");
    sqSetHidden(s_msg_sub, !sub);
}

static void rebuild(void) {
    s_built = sq_lyrics_version;
    lv_obj_clean(s_col);
    if (s_lbl) { heap_caps_free(s_lbl); s_lbl = nullptr; }
    s_nlbl = 0;
    s_cur = -2;
    s_refollow = false;
    s_missing_ms = 0;

    SqLyrState st = sqLyricsState();
    if (st == SQ_LYR_LOADING) {
        showMessage(IC_MIC, "Je cherche les paroles…", nullptr);
        return;
    }
    if (st != SQ_LYR_SYNCED && st != SQ_LYR_PLAIN) {
        showMessage(IC_MIC_OFF, "Pas de paroles pour cette chanson",
                    "On retourne à la lecture dans un instant.");
        s_missing_ms = millis();
        return;
    }

    s_synced = st == SQ_LYR_SYNCED;
    sqSetHidden(s_msg, true);
    sqSetHidden(s_view, false);
    sqSetHidden(s_fade_top, false);
    sqSetHidden(s_fade_bot, false);
    lv_obj_set_style_pad_top(s_col, s_synced ? ANCHOR : 6, 0);
    lv_obj_set_style_pad_bottom(s_col, s_synced ? VIEW_H - 60 : 28, 0);

    if (!s_synced) {
        lv_obj_t* chip = sqBox(s_col, LV_SIZE_CONTENT, 30, s_raised, 15);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_hor(chip, 12, 0);
        lv_obj_t* t = sqLabel(chip, "Paroles sans le rythme : fais-les défiler du doigt", &font_sq_14, 0xFFE58A);
        lv_obj_center(t);
    }

    sqLyricsLock();
    int n = sqLyricsCount();
    s_lbl = (lv_obj_t**)heap_caps_malloc((n ? n : 1) * sizeof(lv_obj_t*), MALLOC_CAP_SPIRAM);
    for (int i = 0; i < n && s_lbl; ++i) {
        const char* text = sqLyricsText(i);
        lv_obj_t* l = sqLabel(s_col, text, s_synced ? &font_sq_20 : &font_sq_24,
                              s_synced ? COL_NEXT : SQ_TEXT);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        if (!*text) lv_obj_set_height(l, 10);   // paragraph break in plain lyrics
        if (s_synced) {
            lv_obj_set_style_pad_ver(l, 6, 0);   // a fatter target for a finger
            lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(l, lineClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
        s_lbl[s_nlbl++] = l;
    }
    sqLyricsUnlock();
    lv_obj_scroll_to_y(s_view, 0, LV_ANIM_OFF);
}

// ── Footer ──────────────────────────────────────────────────────────────────
static void playClicked(lv_event_t*) {
    if (s_playing) sonos.pause(); else sonos.play();
    s_playing = !s_playing;
    s_toggle_ms = millis();
    lv_label_set_text(s_play_icon, s_playing ? IC_PAUSE : IC_PLAY);
}

static void closeClicked(lv_event_t*) { sqBack(); }

static void fmtTime(char* out, size_t n, int s) {
    if (s < 0) s = 0;
    snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

// ── Screen ──────────────────────────────────────────────────────────────────
void sqLyricsCreate(void) {
    s_scr = sqScreenCreate();

    // Header: thumbnail, title, artist, close.
    lv_obj_t* hdr = lv_obj_create(s_scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, SQ_W, HDR);
    lv_obj_set_style_pad_hor(hdr, 16, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 12, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    s_thumb_box = sqBox(hdr, 48, 48, SQ_SURFACE, 14);
    s_thumb_ph = sqCoverPlaceholder(s_thumb_box, 48, 14, "paroles");
    s_thumb_img = lv_image_create(s_thumb_box);
    lv_obj_set_size(s_thumb_img, 48, 48);
    lv_obj_set_style_radius(s_thumb_img, 14, 0);
    lv_obj_set_style_clip_corner(s_thumb_img, true, 0);
    sqSetHidden(s_thumb_img, true);
    lv_obj_t* names = lv_obj_create(hdr);
    lv_obj_remove_style_all(names);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(names, 1);
    lv_obj_set_flex_flow(names, LV_FLEX_FLOW_COLUMN);
    s_title = sqLabel(names, "", &font_sq_18, SQ_TEXT);
    lv_obj_set_width(s_title, LV_PCT(100));
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_DOTS);
    s_artist = sqLabel(names, "", &font_sq_14, COL_SOFT);
    lv_obj_set_width(s_artist, LV_PCT(100));
    lv_label_set_long_mode(s_artist, LV_LABEL_LONG_MODE_DOTS);
    s_close = sqRoundButton(hdr, 48, SQ_RAISED, IC_CLOSE, &font_sqi_26, SQ_TEXT);
    lv_obj_add_event_cb(s_close, closeClicked, LV_EVENT_CLICKED, nullptr);

    // The lines.
    s_view = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_view);
    lv_obj_set_pos(s_view, 0, HDR);
    lv_obj_set_size(s_view, SQ_W, VIEW_H);
    lv_obj_set_scroll_dir(s_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_view, viewScrollBegin, LV_EVENT_SCROLL_BEGIN, nullptr);
    s_col = lv_obj_create(s_view);
    lv_obj_remove_style_all(s_col);
    lv_obj_set_pos(s_col, 24, 0);
    lv_obj_set_size(s_col, SQ_W - 48, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_col, 4, 0);
    lv_obj_remove_flag(s_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_col, LV_OBJ_FLAG_CLICKABLE);

    // Soft edges over the list: lines fade in and out instead of being cut.
    s_fade_top = sqBox(s_scr, SQ_W, 30, SQ_BG, 0);
    lv_obj_set_pos(s_fade_top, 0, HDR);
    s_fade_bot = sqBox(s_scr, SQ_W, 44, SQ_BG, 0);
    lv_obj_set_pos(s_fade_bot, 0, HDR + VIEW_H - 44);
    for (lv_obj_t* f : {s_fade_top, s_fade_bot}) {
        lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);   // a finger scrolls through it
        lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0);
    }
    lv_obj_set_style_bg_main_opa(s_fade_top, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_opa(s_fade_top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_main_opa(s_fade_bot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(s_fade_bot, LV_OPA_COVER, 0);

    // Loading / no lyrics.
    s_msg = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_msg);
    lv_obj_set_pos(s_msg, 0, HDR);
    lv_obj_set_size(s_msg, SQ_W, VIEW_H);
    lv_obj_set_flex_flow(s_msg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_msg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_msg, 14, 0);
    lv_obj_remove_flag(s_msg, LV_OBJ_FLAG_SCROLLABLE);
    s_msg_circle = sqBox(s_msg, 96, 96, SQ_RAISED, 48);
    s_msg_icon = sqIcon(s_msg_circle, IC_MIC, &font_sqi_44, SQ_PINK);
    lv_obj_center(s_msg_icon);
    s_msg_title = sqLabel(s_msg, "", &font_sq_24, SQ_TEXT);
    lv_obj_set_width(s_msg_title, 400);
    lv_obj_set_style_text_align(s_msg_title, LV_TEXT_ALIGN_CENTER, 0);
    s_msg_sub = sqLabel(s_msg, "", &font_sq_16, COL_SOFT);
    lv_obj_set_width(s_msg_sub, 400);
    lv_obj_set_style_text_align(s_msg_sub, LV_TEXT_ALIGN_CENTER, 0);
    sqSetHidden(s_msg, true);

    // Footer: play/pause, progress, time.
    lv_obj_t* foot = lv_obj_create(s_scr);
    lv_obj_remove_style_all(foot);
    lv_obj_set_pos(foot, 0, SQ_H - FOOT);
    lv_obj_set_size(foot, SQ_W, FOOT);
    lv_obj_set_style_pad_hor(foot, 16, 0);
    lv_obj_set_style_pad_bottom(foot, 12, 0);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(foot, 14, 0);
    lv_obj_remove_flag(foot, LV_OBJ_FLAG_SCROLLABLE);
    s_play_btn = sqRoundButton(foot, 56, SQ_PINK, IC_PAUSE, &font_sqi_32, SQ_INK);
    lv_obj_set_style_shadow_width(s_play_btn, 0, 0);
    lv_obj_set_style_shadow_offset_y(s_play_btn, 4, 0);
    lv_obj_set_style_shadow_color(s_play_btn, lv_color_hex(0xB84A80), 0);
    lv_obj_set_style_shadow_opa(s_play_btn, LV_OPA_COVER, 0);
    s_play_icon = lv_obj_get_child(s_play_btn, 0);
    lv_obj_add_event_cb(s_play_btn, playClicked, LV_EVENT_CLICKED, nullptr);
    s_bar = sqBox(foot, 10, 12, SQ_RAISED, 6);
    lv_obj_set_flex_grow(s_bar, 1);
    s_bar_fill = sqBox(s_bar, 0, 12, SQ_PINK, 6);
    s_time = sqLabel(foot, "", &font_sq_14, COL_SOFT);

    sqRegisterScreen(SQ_SCR_LYRICS, s_scr, SQ_TAB_NONE);
}

void sqLyricsOnShow(void) {
    headerRefresh(true);
    s_built = 0xFFFFFFFF;
    s_user_ms = 0;
    sqLyricsTick();
    if (s_synced) follow(LV_ANIM_OFF);
}

void sqLyricsTick(void) {
    if (sonos.isDiscovering()) return;
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) return;
    const uint32_t now = millis();

    headerRefresh(false);
    if (sq_art_version != s_art_seen) { s_art_seen = sq_art_version; thumbShow(); themeFromCover(); }
    if (sq_lyrics_version != s_built) rebuild();

    // No lyrics: say so, then hand the screen back to Lecture.
    if (s_missing_ms && now - s_missing_ms > MISSING_MS) {
        s_missing_ms = 0;
        if (sqCurrent() == SQ_SCR_LYRICS) sqBack();
        return;
    }

    if (now - s_toggle_ms > 3000) s_playing = d->isPlaying;
    lv_label_set_text(s_play_icon, s_playing ? IC_PAUSE : IC_PLAY);

    s_est.tick(d->relTimeSeconds, d->durationSeconds, d->isPlaying, d->posReadMs, now);
    int32_t pos = s_est.hasPosition() ? s_est.positionMs(now) : (int32_t)d->relTimeSeconds * 1000;
    if (pos < 0) pos = 0;

    if (s_synced && s_nlbl) {
        sqLyricsLock();
        int idx = sqLyricsIndexAt(pos + LEAD_MS);
        sqLyricsUnlock();
        if (idx != s_cur) setCurrent(idx, LV_ANIM_ON);
        else if (s_refollow && now - s_user_ms > HOLD_MS) { s_refollow = false; follow(LV_ANIM_ON); }
    }

    int dur = d->durationSeconds;
    int w = lv_obj_get_width(s_bar);
    lv_obj_set_width(s_bar_fill, dur > 0 ? (int)((int64_t)w * pos / (dur * 1000LL)) : 0);
    char a[12], b[12], t[28];
    fmtTime(a, sizeof(a), pos / 1000);
    fmtTime(b, sizeof(b), dur);
    snprintf(t, sizeof(t), "%s / %s", a, b);
    lv_label_set_text(s_time, dur > 0 ? t : "");
}
