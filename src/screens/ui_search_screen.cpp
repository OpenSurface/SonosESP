/**
 * Search tab — catalogue search in the household's streaming services.
 *
 * UI_PORTRAIT builds only. Drawn from the "Recherche" artboard of the portrait
 * canvas: a search field, the categories as chips, the results right under
 * them, and an AZERTY keyboard while typing. The canvas has no service picker
 * (it shows one service); here a dropdown next to the field picks the account,
 * since a household often has several (Spotify for two people, Deezer...).
 *
 * The categories are the service's own, from its presentation map (see
 * smapi::parseSearchCategories): Spotify offers artists/albums/tracks/
 * playlists/all, TuneIn stations/podcasts, and each names them differently in
 * the search call. Results are ordinary service rows (svcAppendItems): a
 * container opens in the Browse screen, whose Back comes back here; a track
 * plays.
 *
 * Network work runs on the music_services worker. This screen submits requests
 * and polls for the answers from an lv_timer that only lives while the screen
 * is shown.
 */

#include "config.h"

#if UI_PORTRAIT

#include "ui_common.h"
#include "ui_fonts.h"
#include "amber.h"
#include "amber_icons.h"
#include "ui_portrait_nav.h"
#include "ui_service_browse.h"
#include "music_services.h"
#include <Preferences.h>

lv_obj_t* scr_search = nullptr;

// ── Geometry (480x800 portrait design space) ────────────────────────────────
#define SR_PAD        16
#define SR_TOP        14
#define SR_FIELD_H    48
#define SR_FIELD_W    268
#define SR_DD_X       (SR_PAD + SR_FIELD_W + 8)
#define SR_DD_W       (UI_DESIGN_W - SR_PAD - SR_DD_X)
#define SR_CHIPS_Y    (SR_TOP + SR_FIELD_H + 12)
#define SR_CHIPS_H    40
#define SR_LIST_Y     (SR_CHIPS_Y + SR_CHIPS_H + 12)
#define SR_KB_H       260
#define SR_KB_MARGIN  5
// The list ends above the keyboard while it is up, so the first results are
// not hidden behind it; otherwise it runs to the tab bar.
#define SR_LIST_H_FULL (UI_DESIGN_H - PNAV_TAB_H - SR_LIST_Y - 12)
#define SR_LIST_H_KB   (UI_DESIGN_H - SR_KB_MARGIN - SR_KB_H - SR_LIST_Y - 8)

#define SR_MAX_SOURCES 24
#define SR_MAX_CATS    12
#define SR_POLL_MS     150
#define SR_TIMEOUT_MS  90000   // as the browse page: the worker always answers, unless superseded

// ── State ───────────────────────────────────────────────────────────────────
static lv_obj_t*   s_field    = nullptr;   // the rounded box holding glyph + textarea
static lv_obj_t*   s_ta       = nullptr;
static lv_obj_t*   s_dd       = nullptr;   // account picker
static lv_obj_t*   s_chips    = nullptr;
static lv_obj_t*   s_list     = nullptr;
static lv_obj_t*   s_status   = nullptr;   // hint / "Searching..." / error row
static lv_obj_t*   s_kb       = nullptr;
static lv_timer_t* s_timer    = nullptr;

static SvcSource   s_src[SR_MAX_SOURCES];
static int         s_src_n    = -1;        // -1: services not listed yet
static int         s_src_sel  = -1;

static SvcCategory s_cats[SR_MAX_CATS];
static int         s_cat_n    = SVC_CATS_LOADING;   // or a count, or SVC_CATS_FAILED
static int         s_cat_sel  = 0;
static bool        s_cat_wait = false;     // categories requested, answer pending

static String      s_term;                 // what the results on screen are for
static bool        s_queued   = false;     // a search waits for the categories
static uint32_t    s_req      = 0;         // search in flight
static uint32_t    s_req_ms   = 0;
static int         s_req_index = 0;

static void startSearch(int index);

// ── Labels ──────────────────────────────────────────────────────────────────
// The presentation map's generic category ids. Anything else is shown with its
// first letter capitalised, which reads fine for the odd provider-specific id.
static String catLabel(const char* id) {
    static const char* const MAP[][2] = {
        {"all", "All"},             {"artists", "Artists"},   {"albums", "Albums"},
        {"tracks", "Tracks"},       {"playlists", "Playlists"}, {"stations", "Stations"},
        {"podcasts", "Podcasts"},   {"episodes", "Episodes"}, {"genres", "Genres"},
        {"hosts", "Hosts"},         {"composers", "Composers"}, {"people", "People"},
    };
    for (const auto& m : MAP) if (strcmp(m[0], id) == 0) return m[1];
    String s = id;
    if (s.length()) s.setCharAt(0, toupper(s[0]));
    return s;
}

// ── Status row ──────────────────────────────────────────────────────────────
static void clearResults() {
    if (!s_list) return;
    lv_obj_clean(s_list);   // rows free their SvcRowData in their DELETE callbacks
    s_status = nullptr;
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
}

static void setStatus(const char* text) {
    if (!s_list) return;
    if (!s_status) {
        s_status = lv_label_create(s_list);
        lv_obj_set_width(s_status, lv_pct(100));
        lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_status, &font_text_16, 0);
        lv_obj_set_style_text_color(s_status, AMB_TEXT3, 0);
        lv_obj_set_style_pad_ver(s_status, SY(10), 0);
        lv_obj_add_event_cb(s_status, [](lv_event_t*) { s_status = nullptr; }, LV_EVENT_DELETE, NULL);
    }
    lv_label_set_text(s_status, text);
}

static void showHint() {
    clearResults();
    if (s_src_sel < 0) return;
    String h = "Search ";
    h += s_src[s_src_sel].label;
    if (s_cat_n > 0) {
        h += " for ";
        int shown = 0;
        for (int i = 0; i < s_cat_n; i++) {
            if (strcmp(s_cats[i].id, "all") == 0) continue;
            if (shown) h += ", ";
            String l = catLabel(s_cats[i].id);
            l.toLowerCase();
            h += l;
            shown++;
        }
        h += ".";
    } else if (s_cat_n == 0) {
        h = s_src[s_src_sel].label;
        h += " does not offer search on Sonos.";
    }
    setStatus(h.c_str());
}

// ── Keyboard ────────────────────────────────────────────────────────────────
// AZERTY with the accents French titles need, as the canvas draws it. The
// numbers-and-symbols page is LVGL's own (its "abc" key comes back here).
// The mode-key texts lv_keyboard switches on. Its macros for them are private
// to lv_keyboard.c, so these must stay equal to its defaults.
#define KB_KEY_LOWER   "abc"
#define KB_KEY_UPPER   "ABC"
#define KB_KEY_SPECIAL "1#"
#define KB_MODE_FLAGS (LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | \
                       LV_BUTTONMATRIX_CTRL_CHECKED)
#define KB_C(x) ((lv_buttonmatrix_ctrl_t)(x))   // the ctrl type is an enum in C++

static const char* const KB_AZ_LC[] = {
    "a", "z", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "q", "s", "d", "f", "g", "h", "j", "k", "l", "m", "\n",
    KB_KEY_UPPER, "w", "x", "c", "v", "b", "n", "\xC3\xA9", "\xC3\xA8",
    LV_SYMBOL_BACKSPACE, "\n",
    KB_KEY_SPECIAL, LV_SYMBOL_KEYBOARD, "\xC3\xA0", " ", "'", LV_SYMBOL_OK, ""};

static const char* const KB_AZ_UC[] = {
    "A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "Q", "S", "D", "F", "G", "H", "J", "K", "L", "M", "\n",
    KB_KEY_LOWER, "W", "X", "C", "V", "B", "N", "\xC3\x89", "\xC3\x88",
    LV_SYMBOL_BACKSPACE, "\n",
    KB_KEY_SPECIAL, LV_SYMBOL_KEYBOARD, "\xC3\x80", " ", "'", LV_SYMBOL_OK, ""};

static const lv_buttonmatrix_ctrl_t KB_AZ_CTRL[] = {
    KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4),
    KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4),
    KB_C(KB_MODE_FLAGS | 6), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4), KB_C(4),
    KB_C(LV_BUTTONMATRIX_CTRL_CHECKED | 6),
    // Widths are 4 bits: 15 at most. 16 spills into LV_BUTTONMATRIX_CTRL_HIDDEN
    // and the space bar disappears.
    KB_C(KB_MODE_FLAGS | 6), KB_C(KB_MODE_FLAGS | 5), KB_C(4), KB_C(12), KB_C(4), KB_C(KB_MODE_FLAGS | 6)};

static void setKeyboard(bool up) {
    if (!s_kb || !s_list) return;
    lv_obj_set_hidden(s_kb, !up);
    lv_obj_set_height(s_list, SY(up ? SR_LIST_H_KB : SR_LIST_H_FULL));
    if (up) {
        lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
        lv_obj_move_foreground(s_kb);
    } else {
        lv_obj_remove_state(s_ta, LV_STATE_FOCUSED);
    }
}

// ── Categories ──────────────────────────────────────────────────────────────
static void styleChip(lv_obj_t* chip, bool on) {
    lv_obj_set_style_bg_color(chip, on ? AMB_ACCENT : AMB_CARD, 0);
    lv_obj_set_style_border_color(chip, on ? AMB_ACCENT : AMB_BORDER, 0);
    lv_obj_t* lbl = lv_obj_get_child(chip, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, on ? AMB_ON_ACCENT : AMB_TEXT, 0);
}

static void chipClicked(lv_event_t* e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == s_cat_sel) return;
    s_cat_sel = idx;
    const uint32_t n = lv_obj_get_child_count(s_chips);
    for (uint32_t i = 0; i < n; i++) styleChip(lv_obj_get_child(s_chips, i), (int)i == idx);
    // Switching category re-runs what is on screen, as the canvas's chips do.
    if (s_term.length()) startSearch(0);
}

static void buildChips() {
    lv_obj_clean(s_chips);
    for (int i = 0; i < s_cat_n; i++) {
        lv_obj_t* chip = lv_button_create(s_chips);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, SY(SR_CHIPS_H - 4));
        lv_obj_set_style_radius(chip, SMIN(18), 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_shadow_width(chip, 0, 0);
        lv_obj_set_style_pad_hor(chip, SX(16), 0);
        lv_obj_set_style_pad_ver(chip, 0, 0);
        lv_obj_add_event_cb(chip, chipClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* lbl = lv_label_create(chip);
        lv_label_set_text(lbl, catLabel(s_cats[i].id).c_str());
        lv_obj_set_style_text_font(lbl, &font_text_14, 0);
        lv_obj_center(lbl);
        styleChip(chip, i == s_cat_sel);
    }
    lv_obj_scroll_to_x(s_chips, 0, LV_ANIM_OFF);
}

// Takes what searchCategories() answered. "All" goes first, as on the canvas,
// and is the default: one search then shows a bit of everything.
static void applyCategories(int n, const SvcCategory* cats) {
    s_cat_wait = false;
    s_cat_n = n;
    if (n <= 0) {
        lv_obj_clean(s_chips);
        return;
    }
    int k = 0;
    for (int i = 0; i < n; i++) if (strcmp(cats[i].id, "all") == 0) s_cats[k++] = cats[i];
    for (int i = 0; i < n; i++) if (strcmp(cats[i].id, "all") != 0) s_cats[k++] = cats[i];
    s_cat_sel = 0;
    buildChips();
}

static void requestCategories() {
    if (s_src_sel < 0) return;
    static SvcCategory got[SR_MAX_CATS];
    int n = musicServices.searchCategories(s_src[s_src_sel].serviceId, s_src[s_src_sel].serial,
                                           got, SR_MAX_CATS);
    if (n == SVC_CATS_LOADING) {
        s_cat_n = SVC_CATS_LOADING;
        s_cat_wait = true;
        lv_obj_clean(s_chips);
        if (!s_term.length()) setStatus("Loading search options...");
        return;
    }
    if (n == SVC_CATS_FAILED) {
        s_cat_wait = false;
        s_cat_n = SVC_CATS_FAILED;
        lv_obj_clean(s_chips);
        s_queued = false;
        clearResults();
        setStatus("Could not load the search options. Search again to retry.");
        return;
    }
    applyCategories(n, got);
}

// ── Accounts ────────────────────────────────────────────────────────────────
// The account picked last time, kept across reboots: NVS "search"/"src".
static void saveSource() {
    if (s_src_sel < 0) return;
    char key[24];
    snprintf(key, sizeof(key), "%d:%d", s_src[s_src_sel].serviceId, s_src[s_src_sel].serial);
    Preferences p;
    if (!p.begin("search", false)) return;
    if (p.getString("src", "") != key) p.putString("src", key);
    p.end();
}

static int pickInitialSource() {
    String want;
    Preferences p;
    if (p.begin("search", true)) {
        want = p.getString("src", "");
        p.end();
    }
    for (int i = 0; i < s_src_n; i++) {
        char key[24];
        snprintf(key, sizeof(key), "%d:%d", s_src[i].serviceId, s_src[i].serial);
        if (want == key) return i;
    }
    // First time: a music catalogue, which is what people search. Sources are
    // sorted by name, so "first signed-in account" was Pocket Casts here, and
    // it has no search at all.
    static const char* const CATALOGUES[] = {"Spotify", "Apple Music", "Deezer", "TIDAL", "Qobuz",
                                             "Amazon Music", "YouTube Music"};
    for (const char* c : CATALOGUES)
        for (int i = 0; i < s_src_n; i++)
            if (strncmp(s_src[i].label, c, strlen(c)) == 0) return i;
    for (int i = 0; i < s_src_n; i++) if (!s_src[i].anonymous) return i;
    return s_src_n > 0 ? 0 : -1;
}

static void selectSource(int idx) {
    s_src_sel = idx;
    s_req = 0;
    s_queued = false;
    s_term = "";
    s_cat_n = SVC_CATS_LOADING;
    requestCategories();
    if (s_cat_n != SVC_CATS_LOADING && s_cat_n != SVC_CATS_FAILED) showHint();
}

static void sourceChanged(lv_event_t* e) {
    int idx = (int)lv_dropdown_get_selected(s_dd);
    if (idx < 0 || idx >= s_src_n || idx == s_src_sel) return;
    String keep = lv_textarea_get_text(s_ta);
    selectSource(idx);
    saveSource();
    // The same words in another service: run them straight away.
    keep.trim();
    if (keep.length()) {
        s_term = keep;
        startSearch(0);
    }
}

static void listSources() {
    s_src_n = musicServices.sources(s_src, SR_MAX_SOURCES);
    String opts;
    for (int i = 0; i < s_src_n; i++) {
        if (i) opts += "\n";
        opts += s_src[i].label;
    }
    if (!s_src_n) {
        lv_dropdown_set_options(s_dd, "No services");
        clearResults();
        setStatus("No streaming service is linked to this Sonos system. Add one in the Sonos app.");
        return;
    }
    lv_dropdown_set_options(s_dd, opts.c_str());
    int sel = pickInitialSource();
    lv_dropdown_set_selected(s_dd, sel);
    selectSource(sel);
}

// ── Searching ───────────────────────────────────────────────────────────────
static void loadMoreClicked(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int next = (int)(intptr_t)lv_obj_get_user_data(btn);
    lv_obj_set_user_data(btn, NULL);
    lv_obj_delete_async(btn);
    startSearch(next);
}

static void renderPage(const SvcPage* p) {
    if (!p->ok) {
        String msg = String("Search failed: ") + p->error;
        setStatus(msg.c_str());
        return;
    }
    if (s_status) { lv_obj_delete(s_status); s_status = nullptr; }
    if (p->count == 0 && s_req_index == 0) {
        String msg = String("No results for \"") + s_term + "\"";
        setStatus(msg.c_str());
        return;
    }
    const int s = s_src_sel;
    svcAppendItems(s_list, s_src[s].serviceId, s_src[s].serial, p->items, p->count);

    const int next = s_req_index + p->count;
    if (p->count > 0 && next < p->total) {
        lv_obj_t* more = lv_button_create(s_list);
        lv_obj_set_size(more, lv_pct(100), SY(50));
        lv_obj_set_style_radius(more, 10, 0);
        lv_obj_set_style_shadow_width(more, 0, 0);
        lv_obj_set_style_bg_color(more, AMB_PANEL, 0);
        lv_obj_set_style_bg_color(more, AMB_BORDER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(more, 1, 0);
        lv_obj_set_style_border_color(more, AMB_BORDER, 0);
        lv_obj_set_user_data(more, (void*)(intptr_t)next);
        lv_obj_add_event_cb(more, loadMoreClicked, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl = lv_label_create(more);
        lv_label_set_text_fmt(lbl, "Load more  (%d of %d shown)", next, p->total);
        lv_obj_set_style_text_color(lbl, AMB_ACCENT, 0);
        lv_obj_set_style_text_font(lbl, &font_text_16, 0);
        lv_obj_center(lbl);
    }
}

static void startSearch(int index) {
    if (s_src_sel < 0 || !s_term.length()) return;
    if (s_cat_n == SVC_CATS_FAILED) requestCategories();   // the retry the message promised
    if (s_cat_n == SVC_CATS_LOADING) {
        s_queued = true;
        clearResults();
        setStatus("Searching...");
        return;
    }
    if (s_cat_n <= 0) { showHint(); return; }

    s_queued = false;
    if (index == 0) clearResults();
    setStatus("Searching...");
    s_req_index = index;
    s_req = musicServices.requestSearch(s_src[s_src_sel].serviceId, s_src[s_src_sel].serial,
                                        s_cats[s_cat_sel].mappedId, s_term.c_str(), index, SVC_PAGE_SIZE);
    s_req_ms = millis();
    if (!s_req) setStatus("Music services are not available");
}

static void submit() {
    String t = lv_textarea_get_text(s_ta);
    t.trim();
    setKeyboard(false);
    if (!t.length()) { s_term = ""; showHint(); return; }
    s_term = t;
    startSearch(0);
}

// ── Poll ────────────────────────────────────────────────────────────────────
static void pollCb(lv_timer_t*) {
    // 1. The household's accounts.
    if (s_src_n < 0) {
        SvcState st = musicServices.state();
        if (st == SVC_IDLE) musicServices.requestLoad(false);
        if (st == SVC_READY) {
            listSources();
        } else if (st == SVC_FAILED) {
            String msg = "Music services unavailable: " + musicServices.lastError() +
                         ". Leave and come back to retry.";
            setStatus(msg.c_str());
        } else {
            setStatus("Loading your services...");
        }
        return;
    }

    // 2. The picked account's categories.
    if (s_cat_wait) {
        requestCategories();
        if (!s_cat_wait) {
            if (s_queued) startSearch(0);
            else if (s_cat_n >= 0 && !s_term.length()) showHint();
        }
    }

    // 3. A search page.
    if (s_req) {
        SvcPage* p = musicServices.takePage(s_req);
        if (!p) {
            if (millis() - s_req_ms > SR_TIMEOUT_MS) {
                s_req = 0;
                setStatus("The service did not answer");
            }
            return;
        }
        s_req = 0;
        renderPage(p);
        MusicServices::freePage(p);
    }
}

static void screenEvent(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCREEN_LOADED) {
        // A failed discovery is retried on every visit, as the Sources tiles do.
        if (musicServices.state() == SVC_FAILED) {
            musicServices.requestLoad(true);
            s_src_n = -1;
        }
        // A search left in flight was replaced if a browse ran meanwhile (one
        // request slot); ask again rather than wait on an answer that is gone.
        if (s_req) startSearch(s_req_index);
        if (!s_timer) s_timer = lv_timer_create(pollCb, SR_POLL_MS, nullptr);
        pollCb(nullptr);
    } else if (code == LV_EVENT_SCREEN_UNLOADED) {
        if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
        setKeyboard(false);
    }
}

// ── Build ───────────────────────────────────────────────────────────────────
void createSearchScreen() {
    scr_search = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_search, AMB_BG, 0);
    lv_obj_set_scrollable(scr_search, false);
    // No dock: the keyboard and the results need the height, and the canvas
    // draws this page without one.
    lv_obj_t* frame = portraitFrame(scr_search, PTAB_SEARCH, false);

    // Field: magnifier + one-line textarea in one rounded box.
    s_field = lv_obj_create(frame);
    lv_obj_set_pos(s_field, SX(SR_PAD), SY(SR_TOP));
    lv_obj_set_size(s_field, SX(SR_FIELD_W), SY(SR_FIELD_H));
    lv_obj_set_style_bg_color(s_field, AMB_CARD, 0);
    lv_obj_set_style_border_width(s_field, 1, 0);
    lv_obj_set_style_border_color(s_field, AMB_BORDER, 0);
    lv_obj_set_style_radius(s_field, SMIN(12), 0);
    lv_obj_set_style_pad_all(s_field, 0, 0);
    lv_obj_set_scrollable(s_field, false);
    lv_obj_t* glyph = portraitSearchGlyph(s_field, SMIN(18), AMB_TEXT3);
    lv_obj_align(glyph, LV_ALIGN_LEFT_MID, SX(14), 0);

    s_ta = lv_textarea_create(s_field);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 64);
    lv_obj_set_size(s_ta, SX(SR_FIELD_W - 44), SY(SR_FIELD_H - 2));
    lv_obj_align(s_ta, LV_ALIGN_LEFT_MID, SX(40), 0);
    lv_obj_set_style_bg_opa(s_ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ta, 0, 0);
    lv_obj_set_style_pad_hor(s_ta, SX(4), 0);
    lv_obj_set_style_pad_ver(s_ta, SY(12), 0);
    lv_obj_set_style_text_font(s_ta, &font_text_16, 0);
    lv_obj_set_style_text_color(s_ta, AMB_TEXT, 0);
    lv_obj_set_style_text_color(s_ta, AMB_TEXT3, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(s_ta, AMB_ACCENT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(s_ta, AMB_ACCENT, LV_PART_CURSOR);
    lv_textarea_set_placeholder_text(s_ta, "Search");
    lv_obj_add_event_cb(s_ta, [](lv_event_t*) { setKeyboard(true); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_field, [](lv_event_t*) { setKeyboard(true); }, LV_EVENT_CLICKED, NULL);

    // Account picker.
    s_dd = lv_dropdown_create(frame);
    lv_obj_set_pos(s_dd, SX(SR_DD_X), SY(SR_TOP));
    lv_obj_set_size(s_dd, SX(SR_DD_W), SY(SR_FIELD_H));
    lv_dropdown_set_options(s_dd, "...");
    lv_obj_set_style_bg_color(s_dd, AMB_RAISED, 0);
    lv_obj_set_style_border_color(s_dd, AMB_BORDER, 0);
    lv_obj_set_style_border_width(s_dd, 1, 0);
    lv_obj_set_style_radius(s_dd, SMIN(12), 0);
    lv_obj_set_style_text_color(s_dd, AMB_TEXT, 0);
    lv_obj_set_style_text_font(s_dd, &font_text_14, 0);
    lv_obj_set_style_pad_hor(s_dd, SX(12), 0);
    lv_obj_set_style_pad_ver(s_dd, SY(14), 0);
    // The open list's highlighted row (LV_PART_SELECTED) defaults to the light
    // theme otherwise - see makeDropdown() in ui_clock_settings.cpp.
    lv_obj_set_style_bg_color(s_dd, AMB_RAISED, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(s_dd, AMB_ACCENT,
        (lv_style_selector_t)((uint32_t)LV_PART_SELECTED | (uint32_t)LV_STATE_CHECKED));
    lv_obj_set_style_text_color(s_dd, AMB_TEXT, LV_PART_SELECTED);
    lv_obj_t* ddl = lv_dropdown_get_list(s_dd);
    if (ddl) {
        lv_obj_set_style_max_height(ddl, SY(360), 0);
        lv_obj_set_style_bg_color(ddl, AMB_RAISED, 0);
        lv_obj_set_style_text_color(ddl, AMB_TEXT, 0);
        lv_obj_set_style_text_font(ddl, &font_text_14, 0);
        lv_obj_set_style_border_color(ddl, AMB_BORDER, 0);
    }
    lv_obj_add_event_cb(s_dd, sourceChanged, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_dd, [](lv_event_t*) { setKeyboard(false); }, LV_EVENT_CLICKED, NULL);

    // Category chips: one row, scrolled sideways when a service has many.
    s_chips = lv_obj_create(frame);
    lv_obj_set_pos(s_chips, SX(SR_PAD), SY(SR_CHIPS_Y));
    lv_obj_set_size(s_chips, SX(UI_DESIGN_W - 2 * SR_PAD), SY(SR_CHIPS_H));
    lv_obj_set_style_bg_opa(s_chips, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chips, 0, 0);
    lv_obj_set_style_pad_all(s_chips, 0, 0);
    lv_obj_set_style_pad_column(s_chips, SX(8), 0);
    lv_obj_set_flex_flow(s_chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_chips, LV_SCROLLBAR_MODE_OFF);

    // Results.
    s_list = lv_obj_create(frame);
    lv_obj_set_pos(s_list, SX(SR_PAD), SY(SR_LIST_Y));
    lv_obj_set_size(s_list, SX(UI_DESIGN_W - 2 * SR_PAD), SY(SR_LIST_H_FULL));
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, SY(10), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_style_bg_color(s_list, AMB_BORDER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_list, SX(4), LV_PART_SCROLLBAR);
    // Scrolling the results means reading them: put the keyboard away.
    lv_obj_add_event_cb(s_list, [](lv_event_t*) { setKeyboard(false); }, LV_EVENT_SCROLL_BEGIN, NULL);

    // Keyboard, on the screen itself so it covers the tab bar while typing.
    s_kb = lv_keyboard_create(scr_search);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_AZ_LC, KB_AZ_CTRL);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_AZ_UC, KB_AZ_CTRL);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_kb, s_ta);
    // set_textarea focuses the field, which shows its blinking cursor before
    // anyone has tapped it. setKeyboard() owns that state from here on.
    lv_obj_remove_state(s_ta, LV_STATE_FOCUSED);
    lv_obj_set_size(s_kb, SX(UI_DESIGN_W - 2 * SR_KB_MARGIN), SY(SR_KB_H));
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, SY(-SR_KB_MARGIN));
    lv_obj_set_style_bg_color(s_kb, AMB_CARD, 0);
    lv_obj_set_style_pad_all(s_kb, SMIN(5), 0);
    lv_obj_set_style_radius(s_kb, 10, 0);
    lv_obj_set_style_bg_color(s_kb, AMB_RAISED, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, AMB_BORDER, (lv_style_selector_t)((uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_CHECKED));
    lv_obj_set_style_text_color(s_kb, AMB_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_kb, &font_text_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb, [](lv_event_t* e) {
        const lv_event_code_t c = lv_event_get_code(e);
        if (c == LV_EVENT_READY)  submit();
        if (c == LV_EVENT_CANCEL) setKeyboard(false);
    }, LV_EVENT_ALL, NULL);
    lv_obj_set_hidden(s_kb, true);

    lv_obj_add_event_cb(scr_search, screenEvent, LV_EVENT_ALL, NULL);
}

#endif  // UI_PORTRAIT
