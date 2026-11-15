// sq_browse.cpp — Spotify (mint): the account's home as big tiles, then lists.
#include "sq_ui.h"
#include "sq_work.h"

// ── Spotify home ────────────────────────────────────────────────────────────
#define MAX_HOME 12

static lv_obj_t* s_home;
static lv_obj_t* s_home_grid;
static SvcItem*  s_home_items = nullptr;   // PSRAM
static int       s_home_count = 0;
static uint32_t  s_home_req   = 0;
static bool      s_home_loaded = false;
static uint32_t  s_home_svc_seen = 0;
static uint32_t  s_home_for_ver  = 0xFFFFFFFF;   // sq_svc_version the home was loaded for

static const uint32_t kTileColors[] = {SQ_MINT, SQ_PINK, SQ_YELLOW, SQ_SKY, SQ_ORANGE, SQ_LILAC};

static const char* homeIcon(const char* title) {
    String t = title;
    t.toLowerCase();
    if (t.indexOf("playlist") >= 0) return IC_PLAYLIST;
    if (t.indexOf("lik") >= 0 || t.indexOf("titre") >= 0 || t.indexOf("song") >= 0 ||
        t.indexOf("track") >= 0 || t.indexOf("aim") >= 0) return IC_HEART;
    if (t.indexOf("album") >= 0) return IC_ALBUM;
    if (t.indexOf("artist") >= 0) return IC_ARTIST;
    if (t.indexOf("radio") >= 0 || t.indexOf("podcast") >= 0) return IC_RADIO;
    if (t.indexOf("library") >= 0 || t.indexOf("biblioth") >= 0) return IC_LIBRARY;
    return IC_SPARKLE;
}

static void homeTileClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_home_count) sqItemOpen(s_home_items[i], kTileColors[i % 6]);
}

static void homeMessage(const char* text) {
    lv_obj_clean(s_home_grid);
    lv_obj_t* l = sqLabel(s_home_grid, text, &font_sq_20, SQ_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
}

static void homeBuild(void) {
    lv_obj_clean(s_home_grid);
    for (int i = 0; i < s_home_count; ++i) {
        const SvcItem& it = s_home_items[i];
        uint32_t c = kTileColors[i % 6];
        lv_obj_t* t = lv_button_create(s_home_grid);
        lv_obj_remove_style_all(t);
        lv_obj_set_size(t, 218, 118);
        lv_obj_set_style_radius(t, 28, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(c), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(t, 14, 0);
        sqPressFeedback(t);
        lv_obj_add_event_cb(t, homeTileClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* dot = sqBox(t, 40, 40, SQ_INK, 20);
        lv_obj_t* ic = sqIcon(dot, homeIcon(it.title), &font_sqi_20, c);
        lv_obj_center(ic);
        lv_obj_t* l = sqLabel(t, it.title, &font_sq_18, SQ_INK);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(l, 190);
        lv_obj_set_style_max_height(l, 48, 0);   // two lines, under the icon
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    }
}

void sqSpotifyRefresh(void) {
    int sid, serial;
    if (!sqSpotify(&sid, &serial)) {
        if (s_home_svc_seen != sq_svc_version || lv_obj_get_child_count(s_home_grid) == 0) {
            s_home_svc_seen = sq_svc_version;
            homeMessage(musicServices.state() == SVC_FAILED || (sq_svc_version > 0 && musicServices.state() == SVC_READY)
                            ? "Spotify n'est pas relié à ce Sonos."
                            : "Je prépare Spotify…");
        }
        return;
    }
    if (s_home_loaded && s_home_for_ver != sq_svc_version) s_home_loaded = false;   // account changed
    if (!s_home_loaded && !s_home_req) {
        s_home_for_ver = sq_svc_version;
        s_home_req = musicServices.requestBrowse(sid, serial, "root", 0, MAX_HOME);
        homeMessage("J'ouvre Spotify…");
        return;
    }
    if (s_home_req) {
        SvcPage* p = musicServices.takePage(s_home_req);
        if (!p) return;
        s_home_req = 0;
        if (p->ok) {
            s_home_count = 0;
            for (int i = 0; i < p->count && s_home_count < MAX_HOME; ++i) s_home_items[s_home_count++] = p->items[i];
            s_home_loaded = true;
            homeBuild();
        } else {
            homeMessage("Spotify ne répond pas. Réessaie dans un moment.");
        }
        MusicServices::freePage(p);
    }
}

void sqSpotifyCreate(void) {
    s_home_items = (SvcItem*)heap_caps_calloc(MAX_HOME, sizeof(SvcItem), MALLOC_CAP_SPIRAM);
    s_home = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_home, "Spotify", SQ_MINT, false);
    sqRoomPill(hdr, true);

    s_home_grid = lv_obj_create(s_home);
    lv_obj_remove_style_all(s_home_grid);
    lv_obj_set_pos(s_home_grid, 0, SQ_HEADER_H);
    lv_obj_set_size(s_home_grid, SQ_W, SQ_H - SQ_HEADER_H - SQ_DOCK_H);
    lv_obj_set_style_pad_hor(s_home_grid, 16, 0);
    lv_obj_set_style_pad_bottom(s_home_grid, 12, 0);
    lv_obj_set_flex_flow(s_home_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_home_grid, 12, 0);
    lv_obj_set_style_pad_column(s_home_grid, 12, 0);
    lv_obj_set_scroll_dir(s_home_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_home_grid, LV_SCROLLBAR_MODE_OFF);
    sqRegisterScreen(SQ_SCR_SPOTIFY, s_home, SQ_TAB_SPOTIFY);
}

// ── Browse (album, playlist, artist, section) ───────────────────────────────
#define MAX_ITEMS  60
#define MAX_DEPTH  6
#define PAGE       30

struct Level { SvcItem item; uint32_t color; };
static Level*    s_stack = nullptr;    // PSRAM, MAX_DEPTH
static int       s_depth = 0;

static lv_obj_t* s_scr;
static lv_obj_t* s_title;
static lv_obj_t* s_back;
static lv_obj_t* s_list;
static SvcItem*  s_items = nullptr;    // PSRAM, MAX_ITEMS
static int       s_count = 0, s_total = 0;
static uint32_t  s_req = 0;
static SqItemRow* s_rows = nullptr;   // PSRAM, MAX_ITEMS
static int       s_row_count = 0;
static uint32_t  s_seen_art = 0, s_seen_favs = 0;
static lv_obj_t* s_more = nullptr;
// Hero (only for a playable container: album, playlist)
static SqItemRow s_hero;
static bool      s_has_hero = false;

static void rowTap(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_count) sqItemOpen(s_items[i], s_stack[s_depth - 1].color);
}
static void rowStar(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_row_count) sqItemToggleFav(s_items[i], s_rows[i].fav_uri);
}
static void playAll(lv_event_t*) {
    sqWorkPost(SQ_JOB_PLAY_ITEM, 0, nullptr, &s_stack[s_depth - 1].item);
    sqShow(SQ_SCR_PLAYER);
}
static void heroStar(lv_event_t*) { sqItemToggleFav(s_stack[s_depth - 1].item, s_hero.fav_uri); }

static void request(int index) {
    int sid, serial;
    if (!sqSpotify(&sid, &serial)) return;
    s_req = musicServices.requestBrowse(sid, serial, s_stack[s_depth - 1].item.id, index, PAGE);
}

static void moreClicked(lv_event_t*) {
    if (s_req || s_count >= MAX_ITEMS) return;
    lv_label_set_text(lv_obj_get_child(s_more, 0), "…");
    request(s_count);
}

static void levelShow(void) {
    const Level& lv = s_stack[s_depth - 1];
    lv_label_set_text(s_title, lv.item.title);
    lv_obj_set_style_text_color(s_title, lv_color_hex(lv.color), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_back, 0), lv_color_hex(lv.color), 0);
    lv_obj_clean(s_list);
    s_row_count = 0;
    s_count = s_total = 0;
    s_more = nullptr;
    s_has_hero = false;

    // A playable container gets a hero: cover, "Tout lire", star.
    if (lv.item.canPlay) {
        lv_obj_t* hero = lv_obj_create(s_list);
        lv_obj_remove_style_all(hero);
        lv_obj_set_size(hero, LV_PCT(100), 120);
        lv_obj_t* cover = sqBox(hero, 112, 112, SQ_SURFACE, 24);
        lv_obj_align(cover, LV_ALIGN_LEFT_MID, 0, 0);
        s_hero.ph = sqCoverPlaceholder(cover, 112, 0, lv.item.title);
        s_hero.img = lv_image_create(cover);
        lv_obj_set_size(s_hero.img, 112, 112);
        sqSetHidden(s_hero.img, true);
        strlcpy(s_hero.art, lv.item.art, sizeof(s_hero.art));
        lv_obj_t* kind = sqLabel(hero, lv.item.subtitle[0] ? lv.item.subtitle : sqItemKind(lv.item),
                                 &font_sq_16, SQ_MUTED);
        lv_label_set_long_mode(kind, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(kind, 300);
        lv_obj_align(kind, LV_ALIGN_TOP_LEFT, 128, 8);
        lv_obj_t* play = sqPillButton(hero, 52, lv.color, SQ_INK, IC_PLAY, "Tout lire", &font_sq_20);
        lv_obj_align(play, LV_ALIGN_BOTTOM_LEFT, 128, -8);
        lv_obj_add_event_cb(play, playAll, LV_EVENT_CLICKED, nullptr);
        strlcpy(s_hero.fav_uri, sqItemUri(lv.item).c_str(), sizeof(s_hero.fav_uri));
        s_hero.star = sqRoundButton(hero, 52, SQ_RAISED, IC_STAR_O, &font_sqi_26, SQ_YELLOW);
        lv_obj_align(s_hero.star, LV_ALIGN_BOTTOM_LEFT, 128 + 172, -8);
        lv_obj_add_event_cb(s_hero.star, heroStar, LV_EVENT_CLICKED, nullptr);
        sqItemRowStar(&s_hero);
        sqItemRowArt(&s_hero);
        s_has_hero = true;
    }
    lv_obj_t* wait = sqLabel(s_list, "J'ouvre…", &font_sq_20, SQ_MUTED);
    lv_obj_set_user_data(wait, (void*)1);
    request(0);
}

static void backClicked(lv_event_t*) {
    if (s_depth > 1) { s_depth--; levelShow(); return; }
    s_depth = 0;
    sqBack();
}

void sqBrowseOpen(const SvcItem& item, uint32_t color) {
    if (sqCurrent() != SQ_SCR_BROWSE) s_depth = 0;   // a fresh trail from Spotify or a search
    if (s_depth >= MAX_DEPTH) s_depth = MAX_DEPTH - 1;
    s_stack[s_depth].item = item;
    s_stack[s_depth].color = color;
    s_depth++;
    if (sqCurrent() != SQ_SCR_BROWSE) sqShow(SQ_SCR_BROWSE);
    levelShow();
}

void sqBrowseRefresh(void) {
    if (s_req) {
        SvcPage* p = musicServices.takePage(s_req);
        if (p) {
            s_req = 0;
            // Drop the "J'ouvre…" placeholder and an old "more" row.
            uint32_t n = lv_obj_get_child_count(s_list);
            for (int32_t i = (int32_t)n - 1; i >= 0; --i) {
                lv_obj_t* c = lv_obj_get_child(s_list, i);
                if (lv_obj_get_user_data(c) == (void*)1 || c == s_more) lv_obj_delete(c);
            }
            s_more = nullptr;
            if (!p->ok) {
                lv_obj_t* l = sqLabel(s_list, p->error[0] ? p->error : "Spotify ne répond pas", &font_sq_18, SQ_MUTED);
                lv_obj_set_width(l, LV_PCT(100));
                lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
            } else {
                s_total = p->total;
                for (int i = 0; i < p->count && s_count < MAX_ITEMS; ++i) {
                    s_items[s_count] = p->items[i];
                    sqItemRowCreate(s_list, s_items[s_count], s_count, rowTap, rowStar, &s_rows[s_count]);
                    s_count++;
                    s_row_count = s_count;
                }
                if (s_count == 0) {
                    lv_obj_t* l = sqLabel(s_list, "C'est vide ici.", &font_sq_18, SQ_MUTED);
                    lv_obj_set_width(l, LV_PCT(100));
                }
                if (s_count < s_total && s_count < MAX_ITEMS) {
                    s_more = sqPillButton(s_list, 52, SQ_RAISED, SQ_TEXT, nullptr, "Voir la suite", &font_sq_18);
                    lv_obj_set_width(s_more, LV_PCT(100));
                    lv_obj_add_event_cb(s_more, moreClicked, LV_EVENT_CLICKED, nullptr);
                    // the label must be child 0 for moreClicked
                }
            }
            MusicServices::freePage(p);
        }
    }
    if (s_seen_art != sq_art_version) {
        s_seen_art = sq_art_version;
        for (int i = 0; i < s_row_count; ++i) sqItemRowArt(&s_rows[i]);
        if (s_has_hero) sqItemRowArt(&s_hero);
    }
    if (s_seen_favs != sq_favs_version) {
        s_seen_favs = sq_favs_version;
        for (int i = 0; i < s_row_count; ++i) sqItemRowStar(&s_rows[i]);
        if (s_has_hero) sqItemRowStar(&s_hero);
    }
}

void sqBrowseCreate(void) {
    s_stack = (Level*)heap_caps_calloc(MAX_DEPTH, sizeof(Level), MALLOC_CAP_SPIRAM);
    s_items = (SvcItem*)heap_caps_calloc(MAX_ITEMS, sizeof(SvcItem), MALLOC_CAP_SPIRAM);
    s_rows  = (SqItemRow*)heap_caps_calloc(MAX_ITEMS, sizeof(SqItemRow), MALLOC_CAP_SPIRAM);
    s_scr = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_scr, nullptr, SQ_MINT, false);
    s_back = sqRoundButton(hdr, 48, SQ_RAISED, IC_BACK, &font_sqi_32, SQ_MINT);
    lv_obj_add_event_cb(s_back, backClicked, LV_EVENT_CLICKED, nullptr);
    s_title = sqLabel(hdr, "", &font_sq_24, SQ_MINT);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_title, 1);

    s_list = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, 0, SQ_HEADER_H);
    lv_obj_set_size(s_list, SQ_W, SQ_H - SQ_HEADER_H - SQ_DOCK_H);
    lv_obj_set_style_pad_hor(s_list, 16, 0);
    lv_obj_set_style_pad_bottom(s_list, 12, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    sqRegisterScreen(SQ_SCR_BROWSE, s_scr, SQ_TAB_SPOTIFY);
}
