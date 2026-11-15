// sq_items.cpp — rows for SMAPI items (search results, albums, playlists).
#include "sq_ui.h"
#include "sq_work.h"

const char* sqItemKind(const SvcItem& it) {
    const char* t = it.itemType;
    if (!strcmp(t, "track") || !strcmp(t, "audiobook") || !strcmp(t, "podcast")) return "Titre";
    if (!strcmp(t, "album")) return "Album";
    if (!strcmp(t, "artist")) return "Artiste";
    if (!strcmp(t, "playlist") || !strcmp(t, "albumList") || !strcmp(t, "trackList")) return "Playlist";
    if (!strcmp(t, "stream") || !strcmp(t, "program")) return "Radio";
    return it.collection ? "Liste" : "Titre";
}

lv_obj_t* sqItemRowCreate(lv_obj_t* list, const SvcItem& it, int index, lv_event_cb_t onTap,
                          lv_event_cb_t onStar, SqItemRow* out) {
    lv_obj_t* row = sqBox(list, LV_PCT(100), 62, SQ_ROW, 20);
    lv_obj_set_style_pad_left(row, 7, 0);
    lv_obj_set_style_pad_right(row, 7, 0);

    lv_obj_t* tap = lv_button_create(row);
    lv_obj_remove_style_all(tap);
    lv_obj_set_size(tap, 480 - 32 - 14 - 56, 62);
    lv_obj_align(tap, LV_ALIGN_LEFT_MID, 0, 0);
    sqPressFeedback(tap);
    lv_obj_add_event_cb(tap, onTap, LV_EVENT_CLICKED, (void*)(intptr_t)index);

    bool round = !strcmp(it.itemType, "artist");
    lv_obj_t* thumb = sqBox(tap, 48, 48, SQ_SURFACE, round ? 24 : 14);
    lv_obj_align(thumb, LV_ALIGN_LEFT_MID, 0, 0);
    out->ph = sqCoverPlaceholder(thumb, 48, 0, it.title);
    out->img = lv_image_create(thumb);
    lv_obj_set_size(out->img, 48, 48);
    sqSetHidden(out->img, true);
    strlcpy(out->art, it.art, sizeof(out->art));

    lv_obj_t* title = sqLabel(tap, it.title, &font_sq_18, SQ_TEXT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(title, 300, 25);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 60, -11);
    char sub[160];
    if (it.subtitle[0]) snprintf(sub, sizeof(sub), "%s · %s", sqItemKind(it), it.subtitle);
    else                snprintf(sub, sizeof(sub), "%s", sqItemKind(it));
    lv_obj_t* st = sqLabel(tap, sub, &font_sq_14, SQ_MUTED);
    lv_label_set_long_mode(st, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(st, 300, 19);
    lv_obj_align(st, LV_ALIGN_LEFT_MID, 60, 13);

    out->star = nullptr;
    out->fav_uri[0] = '\0';
    if (onStar && (it.canPlay || !it.collection)) {
        strlcpy(out->fav_uri, sqItemUri(it).c_str(), sizeof(out->fav_uri));
        out->star = sqRoundButton(row, 48, SQ_RAISED, IC_STAR_O, &font_sqi_26, SQ_YELLOW);
        lv_obj_align(out->star, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(out->star, onStar, LV_EVENT_CLICKED, (void*)(intptr_t)index);
        sqItemRowStar(out);
    }
    sqItemRowArt(out);
    return row;
}

void sqItemRowArt(SqItemRow* r) {
    const lv_image_dsc_t* img = r->art[0] ? sqArtGet(r->art, 48) : nullptr;
    if (!img) return;
    lv_image_set_src(r->img, img);
    sqSetHidden(r->img, false);
    sqSetHidden(r->ph, true);
}

void sqItemRowStar(SqItemRow* r) {
    if (!r->star) return;
    bool fav = r->fav_uri[0] && sqFavsFind(r->fav_uri) >= 0;
    lv_obj_set_style_bg_color(r->star, lv_color_hex(fav ? SQ_YELLOW : SQ_RAISED), 0);
    lv_obj_t* ic = lv_obj_get_child(r->star, 0);
    lv_label_set_text(ic, fav ? IC_STAR : IC_STAR_O);
    lv_obj_set_style_text_color(ic, lv_color_hex(fav ? SQ_INK : SQ_YELLOW), 0);
}

void sqItemToggleFav(const SvcItem& it, const char* fav_uri) {
    int f = fav_uri && fav_uri[0] ? sqFavsFind(fav_uri) : -1;
    if (f >= 0) {
        SqFav fav;
        if (sqFavsGet(f, &fav)) sqWorkPost(SQ_JOB_FAV_REMOVE, 0, fav.id);
        sqToast("Je retire des favoris…");
    } else {
        sqWorkPost(SQ_JOB_FAV_ADD_ITEM, 0, nullptr, &it);
        sqToast("J'ajoute aux favoris…");
    }
}

void sqItemOpen(const SvcItem& it, uint32_t color) {
    if (it.collection && it.canEnumerate) {
        sqBrowseOpen(it, color);
    } else if (it.canPlay || !it.collection) {
        sqWorkPost(SQ_JOB_PLAY_ITEM, 0, nullptr, &it);
        sqShow(SQ_SCR_PLAYER);
    }
}
