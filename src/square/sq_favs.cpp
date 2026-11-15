// sq_favs.cpp — Favoris (yellow): the household's Sonos favourites as big covers.
//
// Each tile has a star. Tapping it empties the star and marks the favourite for
// removal, but the tile stays: tapping again puts it back. The removals are
// sent to Sonos only when the screen is left (sqFavsCommit), so a wrong tap,
// a child's included, is undone with a second one.
#include "sq_ui.h"
#include "sq_work.h"
#include "ui_common.h"

#define TILE_W    141
#define COVER_H   136
#define MAX_TILES 60

static lv_obj_t* s_scr;
static lv_obj_t* s_grid;
static lv_obj_t* s_count;
static uint32_t  s_seen_favs = 0xFFFFFFFF;
static uint32_t  s_seen_art  = 0;

struct Tile { lv_obj_t* img; lv_obj_t* ph; lv_obj_t* star; char art[256]; char id[24]; };
static Tile* s_tiles = nullptr;   // PSRAM, MAX_TILES
static int   s_tile_count = 0;

// Favourites marked for removal, by FV:2 id (stable across a reload).
static char s_pending[MAX_TILES][24];
static int  s_pending_n = 0;

static int pendingIndex(const char* id) {
    for (int i = 0; i < s_pending_n; ++i) if (!strcmp(s_pending[i], id)) return i;
    return -1;
}

static void starShow(Tile& t) {
    bool removing = pendingIndex(t.id) >= 0;
    lv_obj_set_style_bg_color(t.star, lv_color_hex(removing ? SQ_RAISED : SQ_YELLOW), 0);
    lv_obj_t* ic = lv_obj_get_child(t.star, 0);
    lv_label_set_text(ic, removing ? IC_STAR_O : IC_STAR);
    lv_obj_set_style_text_color(ic, lv_color_hex(removing ? SQ_YELLOW : SQ_INK), 0);
}

static void tileClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    sqWorkPost(SQ_JOB_PLAY_FAV, i);
    sqShow(SQ_SCR_PLAYER);
}

static void starClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_tile_count) return;
    Tile& t = s_tiles[i];
    int p = pendingIndex(t.id);
    if (p >= 0) {
        s_pending[p][0] = '\0';
        memmove(s_pending[p], s_pending[p + 1], (s_pending_n - p - 1) * sizeof(s_pending[0]));
        s_pending_n--;
    } else if (s_pending_n < MAX_TILES) {
        strlcpy(s_pending[s_pending_n++], t.id, sizeof(s_pending[0]));
    }
    starShow(t);
}

void sqFavsCommit(void) {
    if (s_pending_n == 0) return;
    for (int i = 0; i < s_pending_n; ++i) sqWorkPost(SQ_JOB_FAV_REMOVE, 1, s_pending[i]);
    sqWorkPost(SQ_JOB_FAVS_LOAD);
    char msg[48];
    snprintf(msg, sizeof(msg), s_pending_n == 1 ? "1 favori retiré" : "%d favoris retirés", s_pending_n);
    sqToast(msg);
    s_pending_n = 0;
}

void sqFavsCreate(void) {
    s_tiles = (Tile*)heap_caps_calloc(MAX_TILES, sizeof(Tile), MALLOC_CAP_SPIRAM);
    s_scr = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_scr, nullptr, SQ_YELLOW, false);
    sqLabel(hdr, "Favoris", &font_sq_28, SQ_YELLOW);
    s_count = sqBox(hdr, LV_SIZE_CONTENT, 28, 0x4A3F1A, 14);
    lv_obj_set_style_pad_hor(s_count, 9, 0);
    lv_obj_t* cl = sqLabel(s_count, "0", &font_sq_16, 0xFFE58A);
    lv_obj_center(cl);
    lv_obj_t* fill = lv_obj_create(hdr);
    lv_obj_remove_style_all(fill);
    lv_obj_set_flex_grow(fill, 1);
    sqRoomPill(hdr, true);

    s_grid = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_pos(s_grid, 0, SQ_HEADER_H);
    lv_obj_set_size(s_grid, SQ_W, SQ_H - SQ_HEADER_H - SQ_DOCK_H);
    lv_obj_set_style_pad_hor(s_grid, 16, 0);
    lv_obj_set_style_pad_bottom(s_grid, 16, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_grid, 14, 0);
    lv_obj_set_style_pad_column(s_grid, 12, 0);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);

    sqRegisterScreen(SQ_SCR_FAVS, s_scr, SQ_TAB_FAVS);
}

static void tileArt(Tile& t) {
    const lv_image_dsc_t* img = t.art[0] ? sqArtGet(t.art, COVER_H) : nullptr;
    if (img) {
        lv_image_set_src(t.img, img);
        sqSetHidden(t.img, false);
        sqSetHidden(t.ph, true);
    }
}

void sqFavsRefresh(void) {
    if (s_seen_favs != sq_favs_version) {
        s_seen_favs = sq_favs_version;
        lv_obj_clean(s_grid);
        s_tile_count = 0;
        int n = sqFavsCount();
        lv_label_set_text_fmt(lv_obj_get_child(s_count, 0), "%d", n);
        if (n == 0) {
            lv_obj_t* l = sqLabel(s_grid, sq_sonos_ready
                                      ? "Pas encore de favoris.\nTouche l'étoile d'une chanson pour la retrouver ici."
                                      : "Je cherche les favoris…",
                                  &font_sq_20, SQ_MUTED);
            lv_obj_set_width(l, LV_PCT(100));
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        }
        for (int i = 0; i < n && i < MAX_TILES; ++i) {
            SqFav f;
            if (!sqFavsGet(i, &f)) break;
            lv_obj_t* b = lv_button_create(s_grid);
            lv_obj_remove_style_all(b);
            lv_obj_set_size(b, TILE_W, COVER_H + 30);
            sqPressFeedback(b);
            lv_obj_add_event_cb(b, tileClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);

            lv_obj_t* cover = sqBox(b, TILE_W, COVER_H, SQ_SURFACE, 26);
            Tile& t = s_tiles[s_tile_count++];
            t.ph = sqCoverPlaceholder(cover, COVER_H, 0, f.title);
            lv_obj_set_width(t.ph, TILE_W);
            t.img = lv_image_create(cover);
            lv_obj_set_size(t.img, COVER_H, COVER_H);
            lv_obj_center(t.img);
            sqSetHidden(t.img, true);
            strlcpy(t.art, f.art, sizeof(t.art));
            strlcpy(t.id, f.id, sizeof(t.id));

            if (f.kind[0]) {   // no badge when the type is unknown
                lv_obj_t* kind = sqBox(cover, LV_SIZE_CONTENT, 24, SQ_BG, 12);
                lv_obj_set_style_pad_hor(kind, 9, 0);
                lv_obj_set_pos(kind, 10, 10);
                lv_obj_t* kl = sqLabel(kind, f.kind, &font_sq_14, SQ_TEXT);
                lv_obj_center(kl);
            }

            // On the tile, not the cover: the cover clips its children to its
            // rounded corners and is not clickable.
            t.star = sqRoundButton(b, 44, SQ_YELLOW, IC_STAR, &font_sqi_26, SQ_INK);
            lv_obj_align(t.star, LV_ALIGN_TOP_RIGHT, -6, 6);
            lv_obj_set_ext_click_area(t.star, 8);
            lv_obj_add_event_cb(t.star, starClicked, LV_EVENT_CLICKED, (void*)(intptr_t)(s_tile_count - 1));
            starShow(t);

            lv_obj_t* title = sqLabel(b, f.title, &font_sq_16, SQ_TEXT);
            lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_size(title, TILE_W, 22);   // one line; DOTS only cuts at a fixed height
            lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            tileArt(t);
        }
    }
    if (s_seen_art != sq_art_version) {
        s_seen_art = sq_art_version;
        for (int i = 0; i < s_tile_count; ++i) tileArt(s_tiles[i]);
    }
}
