/**
 * Streaming-service tiles (Sources) and browse pages — see ui_service_browse.h.
 *
 * Everything here runs on the LVGL thread. The network work happens on the
 * music_services worker; this file only submits requests and polls for the
 * result from an lv_timer, so a slow provider never freezes the panel.
 */

#include "ui_common.h"
#include "ui_service_browse.h"
#include "music_services.h"
#include "smapi.h"
#include "config.h"
#include "ui_fonts.h"
#include "amber_icons.h"
#include "amber.h"

// ============================================================================
// Browse ids
// ============================================================================

bool svcIsBrowseId(const String& id) { return id.startsWith("svc|"); }

static String svcMakeId(int sid, int serial, bool canPlay, const char* type, const char* objectId) {
    String s = "svc|";
    s += sid;
    s += "|";
    s += serial;
    s += canPlay ? "|P|" : "|-|";
    s += type;
    s += "|";
    s += objectId;   // last: provider ids may themselves contain '|'
    return s;
}

static bool svcParseId(const String& id, int* sid, int* serial, bool* canPlay,
                       String* type, String* objectId) {
    if (!svcIsBrowseId(id)) return false;
    int p[5];
    int from = 0;
    for (int i = 0; i < 5; i++) {
        p[i] = id.indexOf('|', from);
        if (p[i] < 0) return false;
        from = p[i] + 1;
    }
    *sid      = id.substring(p[0] + 1, p[1]).toInt();
    *serial   = id.substring(p[1] + 1, p[2]).toInt();
    *canPlay  = id.substring(p[2] + 1, p[3]) == "P";
    *type     = id.substring(p[3] + 1, p[4]);
    *objectId = id.substring(p[4] + 1);
    return objectId->length() > 0;
}

// ============================================================================
// Sources tiles
// ============================================================================

static lv_obj_t*   s_src_list    = nullptr;   // the Sources grid (persistent)
static lv_obj_t*   s_placeholder = nullptr;   // "loading" tile while discovery runs
static lv_timer_t* s_src_timer   = nullptr;

static void svcFreeUserData(lv_event_t* e) {
    void* p = lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    if (p) free(p);
}

static void svcPlaceholderDeleted(lv_event_t* e) {
    if ((lv_obj_t*)lv_event_get_target(e) == s_placeholder) s_placeholder = nullptr;
}

static int svcAppendReadyTiles(lv_obj_t* list) {
    static SvcSource src[24];
    int n = musicServices.sources(src, 24);
    for (int i = 0; i < n; i++) {
        lv_obj_t* btn = sourceTileCreate(list, src[i].anonymous ? AMB_IC_RADIO : AMB_IC_MUSIC,
                                         src[i].label);
        String id = svcMakeId(src[i].serviceId, src[i].serial, false, "root", "root");
        char* copy = strdup(id.c_str());
        if (!copy) break;
        lv_obj_set_user_data(btn, copy);
        lv_obj_add_event_cb(btn, svcFreeUserData, LV_EVENT_DELETE, NULL);
        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            lv_obj_t* b = (lv_obj_t*)lv_event_get_target(ev);
            const char* objID = (const char*)lv_obj_get_user_data(b);
            if (!objID) return;
            browseEnterRoot(String(objID), String(lv_label_get_text(lv_obj_get_child(b, 1))));
        }, LV_EVENT_CLICKED, NULL);
    }
    return n;
}

static void svcShowPlaceholder(lv_obj_t* list);

static int svcAppendFailedTile(lv_obj_t* list) {
    Serial.printf("[SVC] Sources: services unavailable (%s)\n", musicServices.lastError().c_str());
    lv_obj_t* btn = sourceTileCreate(list, AMB_IC_REFRESH, "Music services - tap to retry");
    lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
        lv_obj_t* b = (lv_obj_t*)lv_event_get_target(ev);
        lv_obj_t* parent = lv_obj_get_parent(b);
        musicServices.requestLoad(true);
        lv_obj_delete_async(b);
        svcShowPlaceholder(parent);
    }, LV_EVENT_CLICKED, NULL);
    return 1;
}

// Waits for discovery to finish, then swaps the placeholder for the real tiles.
static void svcSourcesPoll(lv_timer_t* t) {
    if (musicServices.state() == SVC_LOADING) return;
    lv_timer_delete(t);
    s_src_timer = nullptr;
    if (!s_placeholder || !s_src_list) return;   // the list was rebuilt meanwhile
    lv_obj_delete(s_placeholder);                 // its delete cb clears s_placeholder
    if (musicServices.state() == SVC_READY) svcAppendReadyTiles(s_src_list);
    else                                    svcAppendFailedTile(s_src_list);
}

static void svcShowPlaceholder(lv_obj_t* list) {
    s_placeholder = sourceTileCreate(list, AMB_IC_MUSIC, "Music services...");
    lv_obj_set_style_text_color(lv_obj_get_child(s_placeholder, 1), AMB_TEXT3, 0);
    lv_obj_add_event_cb(s_placeholder, svcPlaceholderDeleted, LV_EVENT_DELETE, NULL);
    if (s_src_timer) lv_timer_delete(s_src_timer);
    s_src_timer = lv_timer_create(svcSourcesPoll, 250, nullptr);
}

int svcAppendSourceTiles(lv_obj_t* list) {
    s_src_list = list;
    if (s_src_timer) { lv_timer_delete(s_src_timer); s_src_timer = nullptr; }

    // A failed discovery is retried on every visit rather than left behind a
    // retry tile: the usual cause is a speaker that stopped answering for a few
    // seconds (seen on a weak link), which has long passed by the next visit.
    if (musicServices.state() == SVC_IDLE)   musicServices.requestLoad(false);
    if (musicServices.state() == SVC_FAILED) musicServices.requestLoad(true);
    switch (musicServices.state()) {
        case SVC_READY:  return svcAppendReadyTiles(list);
        case SVC_FAILED: return svcAppendFailedTile(list);
        default:         svcShowPlaceholder(list); return 1;
    }
}

// ============================================================================
// Browse page
// ============================================================================

struct SvcRowData {
    int     sid;
    int     serial;
    SvcItem item;
};

static lv_obj_t*   b_list   = nullptr;
static lv_obj_t*   b_status = nullptr;   // "Loading..." / error row
static lv_timer_t* b_timer  = nullptr;
static uint32_t    b_req    = 0;
static uint32_t    b_req_ms = 0;
static int         b_req_index = 0;
static int         b_sid = 0, b_serial = 0;
static bool        b_can_play = false;
static String      b_type, b_object;

static void svcRequestPage(int index);

void svcBrowseCancel() {
    if (b_timer) { lv_timer_delete(b_timer); b_timer = nullptr; }
    b_list = nullptr;
    b_status = nullptr;
    b_req = 0;
}

static void svcRowDelete(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    void* d = lv_obj_get_user_data(btn);
    if (d) { heap_caps_free(d); lv_obj_set_user_data(btn, NULL); }
}

static lv_obj_t* svcStatusRow(lv_obj_t* list, const char* text) {
    lv_obj_t* lbl = lv_label_create(list);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl, &font_text_16, 0);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    return lbl;
}

static const char* svcIconFor(const SvcItem& it) {
    String t = String(it.itemType);
    t.toLowerCase();
    if (t.indexOf("program") >= 0 || t.indexOf("stream") >= 0 || t.indexOf("station") >= 0)
        return AMB_IC_RADIO;
    if (t.indexOf("playlist") >= 0) return AMB_IC_QUEUE;
    if (t.indexOf("artist") >= 0)   return AMB_IC_STAR;
    if (t.indexOf("album") >= 0 || t.indexOf("track") >= 0) return AMB_IC_MUSIC;
    return it.collection ? AMB_IC_FOLDER : AMB_IC_MUSIC;
}

static void svcPlay(int sid, int serial, const SvcItem& item) {
    String uri, didl;
    bool container = false;
    if (!musicServices.buildPlayback(sid, serial, item, &uri, &didl, &container)) {
        Serial.printf("[SVC] Not playable: %s (%s)\n", item.title, item.itemType);
        return;
    }
    Serial.printf("[SVC] Play %s: %s\n", container ? "container" : "item", uri.c_str());
    if (container) {
        // playContainer() decodes its metadata once before re-encoding it for
        // SOAP (it is written for favourites, whose r:resMD arrives escaped), so
        // it gets the DIDL escaped once more here. playURI() takes it plain.
        String escaped = smapi::xmlEscape(didl.c_str()).c_str();
        sonos.playContainer(uri.c_str(), escaped.c_str());
    } else {
        sonos.playURI(uri.c_str(), didl.c_str());
    }
    lv_screen_load(scr_main);
}

static lv_obj_t* svcRowCreate(lv_obj_t* list, const char* icon, const char* title,
                              const char* subtitle, const char* trailing) {
    lv_obj_t* btn = lv_button_create(list);
    lv_obj_set_size(btn, lv_pct(100), SY(60));
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, AMB_CARD, 0);
    lv_obj_set_style_bg_color(btn, AMB_BORDER, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(btn, SMIN(15), 0);
    lv_obj_set_style_pad_ver(btn, SMIN(6), 0);

    lv_obj_t* ico = lv_label_create(btn);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, AMB_ACCENT, 0);
    lv_obj_set_style_text_font(ico, &font_icon_16, 0);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, SX(5), 0);

    // Content width 536 (see SOURCE_TILE_W) less the icon column and the
    // trailing glyph. ambOneLine bounds the height too, so a long title
    // ellipsises instead of wrapping over the subtitle (issue #151).
    const int textW = 536 - 40 - 50;
    const bool two = subtitle && subtitle[0];
    lv_obj_t* lbl = ambOneLine(btn, &font_text_16, AMB_TEXT, title, textW);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SX(40), two ? -SY(9) : 0);

    if (two) {
        lv_obj_t* sub = ambOneLine(btn, &font_text_12, AMB_TEXT3, subtitle, textW);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, SX(40), SY(11));
    }

    if (trailing) {
        lv_obj_t* tr = lv_label_create(btn);
        lv_label_set_text(tr, trailing);
        lv_obj_set_style_text_color(tr, AMB_TEXT3, 0);
        lv_obj_set_style_text_font(tr, &font_icon_16, 0);
        lv_obj_align(tr, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return btn;
}

static void svcItemClicked(lv_event_t* e) {
    SvcRowData* d = (SvcRowData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    if (!d) return;
    const SvcItem& it = d->item;
    if (it.collection && it.canEnumerate) {
        browseDescend(svcMakeId(d->sid, d->serial, it.canPlay, it.itemType, it.id), String(it.title));
    } else if (it.canPlay) {
        svcPlay(d->sid, d->serial, it);
    } else {
        Serial.printf("[SVC] Row neither browsable nor playable: %s\n", it.title);
    }
}

static void svcPlayAllClicked(lv_event_t* e) {
    SvcItem it;
    memset(&it, 0, sizeof(it));
    strncpy(it.id, b_object.c_str(), sizeof(it.id) - 1);
    strncpy(it.title, current_browse_title.c_str(), sizeof(it.title) - 1);
    strncpy(it.itemType, b_type.c_str(), sizeof(it.itemType) - 1);
    it.collection = true;
    it.canPlay = true;
    it.canEnumerate = true;
    svcPlay(b_sid, b_serial, it);
}

static void svcLoadMoreClicked(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int next = (int)(intptr_t)lv_obj_get_user_data(btn);
    lv_obj_set_user_data(btn, NULL);   // an index, not a heap pointer
    lv_obj_delete_async(btn);
    svcRequestPage(next);
}

static void svcRender(const SvcPage* p) {
    if (!p->ok) {
        String msg = String("Could not load: ") + p->error;
        svcStatusRow(b_list, msg.c_str());
        return;
    }
    if (p->count == 0 && b_req_index == 0) {
        svcStatusRow(b_list, "No items found");
        return;
    }
    for (int i = 0; i < p->count; i++) {
        const SvcItem& it = p->items[i];
        const bool browsable = it.collection && it.canEnumerate;
        lv_obj_t* btn = svcRowCreate(b_list, svcIconFor(it), it.title, it.subtitle,
                                     browsable ? AMB_IC_CHEV : (it.canPlay ? AMB_IC_PLAY : nullptr));
        SvcRowData* d = (SvcRowData*)heap_caps_malloc(sizeof(SvcRowData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!d) { Serial.println("[SVC] Row alloc failed"); break; }
        d->sid = b_sid;
        d->serial = b_serial;
        d->item = it;
        lv_obj_set_user_data(btn, d);
        lv_obj_add_event_cb(btn, svcRowDelete, LV_EVENT_DELETE, NULL);
        lv_obj_add_event_cb(btn, svcItemClicked, LV_EVENT_CLICKED, NULL);
    }

    // Providers' "index" is not always reliable; what we asked for is.
    int next = b_req_index + p->count;
    if (p->count > 0 && next < p->total) {
        lv_obj_t* more = lv_button_create(b_list);
        lv_obj_set_size(more, lv_pct(100), SY(50));
        lv_obj_set_style_radius(more, 10, 0);
        lv_obj_set_style_shadow_width(more, 0, 0);
        lv_obj_set_style_bg_color(more, AMB_PANEL, 0);
        lv_obj_set_style_bg_color(more, AMB_BORDER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(more, 1, 0);
        lv_obj_set_style_border_color(more, AMB_BORDER, 0);
        lv_obj_set_user_data(more, (void*)(intptr_t)next);
        lv_obj_add_event_cb(more, svcLoadMoreClicked, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl = lv_label_create(more);
        lv_label_set_text_fmt(lbl, "Load more  (%d of %d shown)", next, p->total);
        lv_obj_set_style_text_color(lbl, AMB_ACCENT, 0);
        lv_obj_set_style_text_font(lbl, &font_text_16, 0);
        lv_obj_center(lbl);
    }
}

static void svcBrowsePoll(lv_timer_t* t) {
    SvcPage* p = musicServices.takePage(b_req);
    if (!p) {
        // The worker gives every request a definite answer; this only catches
        // one that was dropped because a newer request replaced it.
        if (millis() - b_req_ms > 90000 && b_status) {
            lv_label_set_text(b_status, "The service did not answer");
            lv_timer_delete(t);
            b_timer = nullptr;
        }
        return;
    }
    lv_timer_delete(t);
    b_timer = nullptr;
    if (b_status) { lv_obj_delete(b_status); b_status = nullptr; }
    if (b_list) svcRender(p);
    MusicServices::freePage(p);
}

static void svcRequestPage(int index) {
    if (!b_list) return;
    b_status = svcStatusRow(b_list, "Loading...");
    b_req_index = index;
    b_req = musicServices.requestBrowse(b_sid, b_serial, b_object.c_str(), index, SVC_PAGE_SIZE);
    b_req_ms = millis();
    if (!b_req) { lv_label_set_text(b_status, "Music services are not available"); return; }
    if (b_timer) lv_timer_delete(b_timer);
    b_timer = lv_timer_create(svcBrowsePoll, 150, nullptr);
}

void svcBrowseBegin(lv_obj_t* list) {
    svcBrowseCancel();
    if (!svcParseId(current_browse_id, &b_sid, &b_serial, &b_can_play, &b_type, &b_object)) {
        svcStatusRow(list, "Bad service location");
        return;
    }
    b_list = list;

    // A playable container (album, playlist) gets "Play all" on top; its rows
    // below are the tracks, each playable on its own.
    if (b_can_play && b_object != "root") {
        lv_obj_t* btn = svcRowCreate(list, AMB_IC_PLAY, "Play all", nullptr, nullptr);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 1), AMB_ACCENT, 0);
        lv_obj_add_event_cb(btn, svcPlayAllClicked, LV_EVENT_CLICKED, NULL);
    }
    svcRequestPage(0);
}
