// sq_favs.cpp — Favoris (yellow): the household's Sonos favourites as big covers.
#include "sq_ui.h"
#include "sq_work.h"
#include "ui_common.h"

#define TILE_W  141
#define COVER_H 136
#define MAX_TILES 60

static lv_obj_t* s_scr;
static lv_obj_t* s_grid;
static lv_obj_t* s_count;
static lv_obj_t* s_confirm;
static lv_obj_t* s_confirm_lbl;
static uint32_t  s_seen_favs = 0xFFFFFFFF;
static uint32_t  s_seen_art  = 0;
static int       s_pending_remove = -1;

struct Tile { lv_obj_t* img; lv_obj_t* ph; char art[256]; };
static Tile* s_tiles = nullptr;   // PSRAM, MAX_TILES
static int  s_tile_count = 0;

static void tileClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    sqWorkPost(SQ_JOB_PLAY_FAV, i);
    sqShow(SQ_SCR_PLAYER);
}

static void tileLongPressed(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    SqFav f;
    if (!sqFavsGet(i, &f)) return;
    s_pending_remove = i;
    lv_label_set_text_fmt(s_confirm_lbl, "Retirer « %s » des favoris ?", f.title);
    sqSetHidden(s_confirm, false);
    // A long press also ends in a click on release: swallow it.
    lv_indev_wait_release(lv_indev_active());
}

static void confirmYes(lv_event_t*) {
    SqFav f;
    if (s_pending_remove >= 0 && sqFavsGet(s_pending_remove, &f)) sqWorkPost(SQ_JOB_FAV_REMOVE, 0, f.id);
    s_pending_remove = -1;
    sqSetHidden(s_confirm, true);
}
static void confirmNo(lv_event_t*) { s_pending_remove = -1; sqSetHidden(s_confirm, true); }

void sqFavsCreate(void) {
    s_tiles = (Tile*)heap_caps_calloc(MAX_TILES, sizeof(Tile), MALLOC_CAP_SPIRAM);
    s_scr = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_scr, nullptr, SQ_YELLOW, false);
    lv_obj_t* t = sqLabel(hdr, "Favoris", &font_sq_28, SQ_YELLOW);
    (void)t;
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

    // Remove confirmation, over the grid
    s_confirm = sqBox(s_scr, SQ_W, SQ_H, SQ_BG, 0);
    lv_obj_set_style_bg_opa(s_confirm, LV_OPA_80, 0);
    lv_obj_set_clickable(s_confirm, true);
    lv_obj_t* card = sqBox(s_confirm, 400, LV_SIZE_CONTENT, SQ_SURFACE, 28);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 18, 0);
    s_confirm_lbl = sqLabel(card, "", &font_sq_20, SQ_TEXT);
    lv_obj_set_width(s_confirm_lbl, LV_PCT(100));
    lv_label_set_long_mode(s_confirm_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_confirm_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* no = sqPillButton(row, 56, SQ_RAISED, SQ_TEXT, nullptr, "Non", &font_sq_20);
    lv_obj_set_width(no, 150);
    lv_obj_add_event_cb(no, confirmNo, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* yes = sqPillButton(row, 56, SQ_YELLOW, SQ_INK, IC_STAR_O, "Retirer", &font_sq_20);
    lv_obj_set_width(yes, 150);
    lv_obj_add_event_cb(yes, confirmYes, LV_EVENT_CLICKED, nullptr);
    sqSetHidden(s_confirm, true);

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
            lv_obj_add_event_cb(b, tileLongPressed, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);

            lv_obj_t* cover = sqBox(b, TILE_W, COVER_H, SQ_SURFACE, 26);
            Tile& t = s_tiles[s_tile_count++];
            t.ph = sqCoverPlaceholder(cover, COVER_H, 0, f.title);
            lv_obj_set_width(t.ph, TILE_W);
            t.img = lv_image_create(cover);
            lv_obj_set_size(t.img, COVER_H, COVER_H);
            lv_obj_center(t.img);
            sqSetHidden(t.img, true);
            strlcpy(t.art, f.art, sizeof(t.art));

            lv_obj_t* kind = sqBox(cover, LV_SIZE_CONTENT, 24, SQ_BG, 12);
            lv_obj_set_style_pad_hor(kind, 9, 0);
            lv_obj_set_pos(kind, 10, 10);
            lv_obj_t* kl = sqLabel(kind, f.kind, &font_sq_14, SQ_TEXT);
            lv_obj_center(kl);

            lv_obj_t* title = sqLabel(b, f.title, &font_sq_16, SQ_TEXT);
            lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(title, TILE_W);
            lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            tileArt(t);
        }
    }
    if (s_seen_art != sq_art_version) {
        s_seen_art = sq_art_version;
        for (int i = 0; i < s_tile_count; ++i) tileArt(s_tiles[i]);
    }
}
