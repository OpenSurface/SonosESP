// sq_search.cpp — Chercher (orange): keyboard straight onto Spotify, then results.
#include "sq_ui.h"
#include "sq_work.h"
#include <Preferences.h>

// ── Search screen ───────────────────────────────────────────────────────────
static lv_obj_t* s_search;
static lv_obj_t* s_ta;
static lv_obj_t* s_recent_row;
static String    s_recent[4];

static void recentLoad(void) {
    Preferences p;
    p.begin("sq_ui", true);
    String all = p.getString("recent", "");
    p.end();
    int i = 0, start = 0;
    while (i < 4 && start < (int)all.length()) {
        int sep = all.indexOf('\x1f', start);
        if (sep < 0) sep = all.length();
        s_recent[i++] = all.substring(start, sep);
        start = sep + 1;
    }
}

static void recentPush(const String& term) {
    String t = term;
    t.trim();
    if (!t.length()) return;
    String list[4] = {t};
    int n = 1;
    for (int i = 0; i < 4 && n < 4; ++i)
        if (s_recent[i].length() && !s_recent[i].equalsIgnoreCase(t)) list[n++] = s_recent[i];
    String all;
    for (int i = 0; i < 4; ++i) {
        s_recent[i] = i < n ? list[i] : String();
        if (i < n) { if (i) all += '\x1f'; all += list[i]; }
    }
    Preferences p;
    p.begin("sq_ui", false);
    p.putString("recent", all);
    p.end();
}

static void recentClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_textarea_set_text(s_ta, s_recent[i].c_str());
}

static void recentBuild(void) {
    lv_obj_clean(s_recent_row);
    sqLabel(s_recent_row, s_recent[0].length() ? "Récents" : "Écris le nom d'un artiste ou d'une chanson",
            &font_sq_14, SQ_MUTED);
    for (int i = 0; i < 4; ++i) {
        if (!s_recent[i].length()) continue;
        lv_obj_t* c = sqPillButton(s_recent_row, 40, SQ_RAISED, SQ_TEXT, nullptr, s_recent[i].c_str(), &font_sq_16);
        lv_obj_add_event_cb(c, recentClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

static void goClicked(lv_event_t*) {
    String term = lv_textarea_get_text(s_ta);
    term.trim();
    if (!term.length()) return;
    recentPush(term);
    sqResultsStart(term.c_str());
}

static void clearClicked(lv_event_t*) { lv_textarea_set_text(s_ta, ""); }
static void backClicked(lv_event_t*) { sqShow(SQ_SCR_PLAYER); }

void sqSearchCreate(void) {
    s_search = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_search, nullptr, SQ_ORANGE, false);
    lv_obj_t* back = sqRoundButton(hdr, 48, SQ_RAISED, IC_BACK, &font_sqi_32, SQ_ORANGE);
    lv_obj_add_event_cb(back, backClicked, LV_EVENT_CLICKED, nullptr);
    sqLabel(hdr, "Chercher sur Spotify", &font_sq_24, SQ_ORANGE);

    lv_obj_t* field = sqBox(s_search, SQ_W - 32, 60, SQ_SURFACE, 30);
    lv_obj_set_pos(field, 16, SQ_HEADER_H);
    lv_obj_set_style_border_width(field, 3, 0);
    lv_obj_set_style_border_color(field, lv_color_hex(SQ_ORANGE), 0);
    lv_obj_t* ic = sqIcon(field, IC_SEARCH, &font_sqi_26, SQ_ORANGE);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    s_ta = lv_textarea_create(field);
    lv_obj_remove_style_all(s_ta);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_placeholder_text(s_ta, "Un artiste, une chanson…");
    lv_obj_set_size(s_ta, SQ_W - 32 - 110, 40);
    lv_obj_align(s_ta, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_set_style_text_font(s_ta, &font_sq_24, 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(SQ_TEXT), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(0x9C96CC), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(s_ta, lv_color_hex(SQ_ORANGE), LV_PART_CURSOR);
    lv_obj_set_style_border_width(s_ta, 3, LV_PART_CURSOR);
    lv_obj_set_style_border_side(s_ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_add_state(s_ta, LV_STATE_FOCUSED);   // keeps the cursor visible
    lv_obj_t* clr = sqRoundButton(field, 44, SQ_RAISED, IC_CLOSE, &font_sqi_20, SQ_TEXT);
    lv_obj_align(clr, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(clr, clearClicked, LV_EVENT_CLICKED, nullptr);

    s_recent_row = lv_obj_create(s_search);
    lv_obj_remove_style_all(s_recent_row);
    lv_obj_set_pos(s_recent_row, 16, SQ_HEADER_H + 64);
    lv_obj_set_size(s_recent_row, SQ_W - 32, 52);
    lv_obj_set_flex_flow(s_recent_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_recent_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_recent_row, 8, 0);
    lv_obj_set_scroll_dir(s_recent_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_recent_row, LV_SCROLLBAR_MODE_OFF);
    recentLoad();
    recentBuild();

    lv_obj_t* kb = sqKeyboardCreate(s_search, s_ta, "Chercher", SQ_ORANGE, goClicked);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);

    sqRegisterScreen(SQ_SCR_SEARCH, s_search, SQ_TAB_NONE);
}

// ── Results screen ──────────────────────────────────────────────────────────
#define MAX_RESULTS 30

static lv_obj_t* s_results;
static lv_obj_t* s_term_lbl;
static lv_obj_t* s_chips;
static lv_obj_t* s_list;
static String    s_term;

struct Chip { const char* id; const char* label; char mapped[48]; bool have; };
static Chip s_cat[5] = {
    {"all", "Tout", "", true},
    {"tracks", "Titres", "", false},
    {"albums", "Albums", "", false},
    {"artists", "Artistes", "", false},
    {"playlists", "Playlists", "", false},
};
static int s_filter = 0;

// "Tout" asks each category in turn and keeps the top few of each.
static const int kAllOrder[] = {3, 1, 2, 4};   // artists, tracks, albums, playlists
static const int kAllCount[] = {2, 5, 3, 3};
static int       s_step   = -1;                // index into kAllOrder; -1 idle
static uint32_t  s_req    = 0;
static bool      s_cats_ready = false;
static bool      s_failed = false;
static SvcItem*  s_items  = nullptr;           // PSRAM, MAX_RESULTS
static int       s_count  = 0;
static SqItemRow* s_rows   = nullptr;          // PSRAM, MAX_RESULTS
static int       s_row_count = 0;
static uint32_t  s_seen_art = 0, s_seen_favs = 0;

static void rowTap(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_count) sqItemOpen(s_items[i], SQ_ORANGE);
}
static void rowStar(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_row_count) sqItemToggleFav(s_items[i], s_rows[i].fav_uri);
}

static void listMessage(const char* text) {
    lv_obj_clean(s_list);
    s_row_count = 0;
    lv_obj_t* l = sqLabel(s_list, text, &font_sq_20, SQ_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
}

static void listBuild(void) {
    lv_obj_clean(s_list);
    s_row_count = 0;
    if (s_count == 0) { listMessage(s_failed ? "Spotify ne répond pas. Réessaie." : "Rien trouvé…"); return; }
    for (int i = 0; i < s_count && i < MAX_RESULTS; ++i) {
        sqItemRowCreate(s_list, s_items[i], i, rowTap, rowStar, &s_rows[i]);
        s_row_count++;
    }
}

static void chipsBuild(void) {
    lv_obj_clean(s_chips);
    for (int i = 0; i < 5; ++i) {
        if (!s_cat[i].have) continue;
        bool on = i == s_filter;
        lv_obj_t* c = sqPillButton(s_chips, 40, on ? SQ_ORANGE : SQ_RAISED, on ? SQ_INK : SQ_TEXT,
                                   nullptr, s_cat[i].label, &font_sq_16);
        lv_obj_set_style_pad_hor(c, 13, 0);
        lv_obj_add_event_cb(c, [](lv_event_t* e) {
            s_filter = (int)(intptr_t)lv_event_get_user_data(e);
            chipsBuild();
            sqResultsStart(s_term.c_str());
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

// Starts the next request of the current search, or finishes it.
static void nextRequest(void) {
    int sid, serial;
    if (!sqSpotify(&sid, &serial)) { s_step = -1; listMessage("Spotify n'est pas encore prêt…"); return; }
    if (s_filter != 0) {
        if (s_step > 0) { s_step = -1; listBuild(); return; }
        s_step = 1;
        s_req = musicServices.requestSearch(sid, serial, s_cat[s_filter].mapped, s_term.c_str(), 0, MAX_RESULTS);
        return;
    }
    while (s_step < 4) {
        int c = kAllOrder[s_step];
        if (s_cat[c].have) {
            s_req = musicServices.requestSearch(sid, serial, s_cat[c].mapped, s_term.c_str(), 0, kAllCount[s_step]);
            s_step++;
            return;
        }
        s_step++;
    }
    s_step = -1;
    listBuild();
}

void sqResultsStart(const char* term) {
    s_term = term;
    lv_label_set_text(s_term_lbl, term);
    s_count = 0;
    s_failed = false;
    s_step = 0;
    s_req = 0;
    listMessage("Je cherche…");
    if (sqCurrent() != SQ_SCR_RESULTS) sqShow(SQ_SCR_RESULTS);
}

void sqResultsRefresh(void) {
    // Categories: from Spotify's presentation map, fetched once.
    if (!s_cats_ready && s_step >= 0) {
        int sid, serial;
        if (!sqSpotify(&sid, &serial)) {
            if (musicServices.state() != SVC_LOADING && sq_svc_version > 0) {
                s_step = -1;
                listMessage("Spotify n'est pas relié à ce Sonos.");
            }
            return;
        }
        SvcCategory cats[12];
        int n = musicServices.searchCategories(sid, serial, cats, 12);
        if (n == SVC_CATS_LOADING) return;
        if (n <= 0) { s_step = -1; listMessage("La recherche Spotify n'est pas disponible."); return; }
        for (int i = 1; i < 5; ++i) {
            for (int k = 0; k < n; ++k) {
                if (!strcmp(cats[k].id, s_cat[i].id)) {
                    strlcpy(s_cat[i].mapped, cats[k].mappedId, sizeof(s_cat[i].mapped));
                    s_cat[i].have = true;
                }
            }
        }
        s_cats_ready = true;
        chipsBuild();
    }
    if (s_step == 0) { nextRequest(); return; }
    if (s_step > 0 && s_req) {
        SvcPage* p = musicServices.takePage(s_req);
        if (p) {
            if (!p->ok) s_failed = true;
            for (int i = 0; p->ok && i < p->count && s_count < MAX_RESULTS; ++i) s_items[s_count++] = p->items[i];
            MusicServices::freePage(p);
            s_req = 0;
            nextRequest();
        }
    }
    if (s_seen_art != sq_art_version) {
        s_seen_art = sq_art_version;
        for (int i = 0; i < s_row_count; ++i) sqItemRowArt(&s_rows[i]);
    }
    if (s_seen_favs != sq_favs_version) {
        s_seen_favs = sq_favs_version;
        for (int i = 0; i < s_row_count; ++i) sqItemRowStar(&s_rows[i]);
    }
}

static void editClicked(lv_event_t*) { sqShow(SQ_SCR_SEARCH); }

void sqResultsCreate(void) {
    s_items = (SvcItem*)heap_caps_calloc(MAX_RESULTS, sizeof(SvcItem), MALLOC_CAP_SPIRAM);
    s_rows  = (SqItemRow*)heap_caps_calloc(MAX_RESULTS, sizeof(SqItemRow), MALLOC_CAP_SPIRAM);
    s_results = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_results, nullptr, SQ_ORANGE, false);
    lv_obj_t* field = lv_button_create(hdr);
    lv_obj_remove_style_all(field);
    lv_obj_set_height(field, 52);
    lv_obj_set_flex_grow(field, 1);
    lv_obj_set_style_radius(field, 26, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(SQ_SURFACE), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(field, 3, 0);
    lv_obj_set_style_border_color(field, lv_color_hex(SQ_ORANGE), 0);
    lv_obj_add_event_cb(field, editClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ic = sqIcon(field, IC_SEARCH, &font_sqi_20, SQ_ORANGE);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    s_term_lbl = sqLabel(field, "", &font_sq_20, SQ_TEXT);
    lv_label_set_long_mode(s_term_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_term_lbl, 180);
    lv_obj_align(s_term_lbl, LV_ALIGN_LEFT_MID, 44, 0);
    sqRoomPill(hdr, true);

    s_chips = lv_obj_create(s_results);
    lv_obj_remove_style_all(s_chips);
    lv_obj_set_pos(s_chips, 0, SQ_HEADER_H);
    lv_obj_set_size(s_chips, SQ_W, 50);
    lv_obj_set_style_pad_hor(s_chips, 16, 0);
    lv_obj_set_flex_flow(s_chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s_chips, 7, 0);
    lv_obj_set_scroll_dir(s_chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_chips, LV_SCROLLBAR_MODE_OFF);
    chipsBuild();

    s_list = lv_obj_create(s_results);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, 0, SQ_HEADER_H + 50);
    lv_obj_set_size(s_list, SQ_W, SQ_H - SQ_HEADER_H - 50 - SQ_DOCK_H);
    lv_obj_set_style_pad_hor(s_list, 16, 0);
    lv_obj_set_style_pad_bottom(s_list, 12, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    sqRegisterScreen(SQ_SCR_RESULTS, s_results, SQ_TAB_SEARCH);
}

void sqSearchOnShow(void) {
    recentBuild();
}
