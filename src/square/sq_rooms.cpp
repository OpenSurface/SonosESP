// sq_rooms.cpp — "Où on écoute ?" (blue): choose the room, group rooms.
#include "sq_ui.h"
#include "sq_work.h"
#include "ui_common.h"

static lv_obj_t* s_scr;
static lv_obj_t* s_grid;
static lv_obj_t* s_busy;
static uint32_t  s_seen = 0xFFFFFFFF;

static const char* roomIcon(const String& name) {
    String n = name;
    n.toLowerCase();
    if (n.indexOf("salon") >= 0 || n.indexOf("living") >= 0 || n.indexOf("séjour") >= 0) return IC_SOFA;
    if (n.indexOf("cuisine") >= 0 || n.indexOf("kitchen") >= 0) return IC_POT;
    if (n.indexOf("enfant") >= 0 || n.indexOf("kid") >= 0 || n.indexOf("jeu") >= 0) return IC_TEDDY;
    if (n.indexOf("chambre") >= 0 || n.indexOf("bed") >= 0) return IC_BED;
    if (n.indexOf("bureau") >= 0 || n.indexOf("office") >= 0) return IC_MONITOR;
    if (n.indexOf("terrasse") >= 0 || n.indexOf("jardin") >= 0 || n.indexOf("garden") >= 0 ||
        n.indexOf("balcon") >= 0) return IC_SUN;
    return IC_SPEAKER;
}

static void tileClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    sqWorkPost(SQ_JOB_SELECT_ROOM, i);
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_set_style_bg_color(t, lv_color_hex(SQ_SKY), 0);   // instant feedback
    sqSetHidden(s_busy, false);
}

static void groupClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    sqWorkPost(SQ_JOB_TOGGLE_GROUP, i);
    sqSetHidden(s_busy, false);
}

static void allClicked(lv_event_t*) {
    sqWorkPost(SQ_JOB_GROUP_ALL);
    sqSetHidden(s_busy, false);
}

static void settingsClicked(lv_event_t*) { sqShow(SQ_SCR_SETTINGS); }
static void closeClicked(lv_event_t*) { sqBack(); }

void sqRoomsCreate(void) {
    s_scr = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_scr, "Où on écoute ?", SQ_SKY, false);
    s_busy = sqLabel(hdr, IC_REFRESH, &font_sqi_26, SQ_SKY);
    sqSetHidden(s_busy, true);
    lv_obj_t* close = sqRoundButton(hdr, 48, SQ_RAISED, IC_CLOSE, &font_sqi_26, SQ_TEXT);
    lv_obj_add_event_cb(close, closeClicked, LV_EVENT_CLICKED, nullptr);

    s_grid = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_pos(s_grid, 0, SQ_HEADER_H);
    lv_obj_set_size(s_grid, SQ_W, SQ_H - SQ_HEADER_H - 88);
    lv_obj_set_style_pad_hor(s_grid, 16, 0);
    lv_obj_set_style_pad_bottom(s_grid, 8, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_grid, 12, 0);
    lv_obj_set_style_pad_column(s_grid, 12, 0);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* bottom = lv_obj_create(s_scr);
    lv_obj_remove_style_all(bottom);
    lv_obj_set_size(bottom, SQ_W, 88);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_hor(bottom, 16, 0);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bottom, 12, 0);
    lv_obj_t* all = sqPillButton(bottom, 60, SQ_BG, SQ_SKY, IC_HOME, "Toute la maison", &font_sq_20);
    lv_obj_set_style_border_width(all, 3, 0);
    lv_obj_set_style_border_color(all, lv_color_hex(SQ_SKY), 0);
    lv_obj_set_flex_grow(all, 1);
    lv_obj_add_event_cb(all, allClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* gear = sqRoundButton(bottom, 60, SQ_RAISED, IC_TUNE, &font_sqi_26, SQ_MUTED);
    lv_obj_add_event_cb(gear, settingsClicked, LV_EVENT_CLICKED, nullptr);

    sqRegisterScreen(SQ_SCR_ROOMS, s_scr, SQ_TAB_NONE);
}

void sqRoomsRefresh(void) {
    if (sonos.isDiscovering()) return;   // the device list is being rebuilt
    if (s_seen == sq_rooms_version && lv_obj_get_child_count(s_grid) > 0) return;
    s_seen = sq_rooms_version;
    sqSetHidden(s_busy, sqWorkBusy());
    lv_obj_clean(s_grid);

    SonosDevice* cur = sonos.getCurrentDevice();
    int n = sonos.getDeviceCount();
    if (n == 0 || !cur) {
        lv_obj_t* l = sqLabel(s_grid, sqWorkBusy() ? "Je cherche les enceintes…" : "Aucune enceinte trouvée",
                              &font_sq_20, SQ_MUTED);
        lv_obj_set_width(l, LV_PCT(100));
        return;
    }
    SonosDevice* coord = sonos.groupCoordinatorFor(cur);
    int ci = -1;
    for (int i = 0; i < n; ++i) if (sonos.getDevice(i) == coord) ci = i;

    // Snapshot names under the lock, build widgets after.
    struct Row { String name; bool selected, grouped, playing; int vol; };
    Row rows[MAX_SONOS_DEVICES];
    String leader;
    if (!xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(50))) { s_seen--; return; }
    for (int i = 0; i < n && i < MAX_SONOS_DEVICES; ++i) {
        SonosDevice* d = sonos.getDevice(i);
        rows[i].name     = d->roomName.length() ? d->roomName : d->ip.toString();
        rows[i].selected = (i == ci) || d == cur;
        rows[i].grouped  = !rows[i].selected && ci >= 0 && sonos.isDeviceInGroup(i, ci);
        rows[i].playing  = d->isPlaying;
        rows[i].vol      = d->volume;
    }
    leader = coord ? coord->roomName : cur->roomName;
    xSemaphoreGive(sonos.getDeviceMutex());

    for (int i = 0; i < n && i < MAX_SONOS_DEVICES; ++i) {
        const Row& r = rows[i];
        uint32_t bg = r.selected ? SQ_SKY : (r.grouped ? SQ_SKY_DEEP : SQ_SURFACE);
        uint32_t fg = r.selected ? SQ_INK : SQ_TEXT;

        lv_obj_t* tile = lv_button_create(s_grid);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 218, 92);
        lv_obj_set_style_radius(tile, 26, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(bg), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, r.grouped ? 3 : 0, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(SQ_SKY), 0);
        lv_obj_set_style_pad_left(tile, 12, 0);
        sqPressFeedback(tile);
        lv_obj_add_event_cb(tile, tileClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* ic = sqBox(tile, 48, 48, r.selected ? SQ_INK : SQ_RAISED, 24);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* g = sqIcon(ic, roomIcon(r.name), &font_sqi_26, SQ_SKY);
        lv_obj_center(g);

        lv_obj_t* name = sqLabel(tile, r.name.c_str(), &font_sq_20, fg);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(name, r.selected ? 140 : 100, 27);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 60, -11);
        char st[48];
        if (r.selected)      snprintf(st, sizeof(st), "On écoute ici · %d %%", r.vol);
        else if (r.grouped)  snprintf(st, sizeof(st), "Avec %s", leader.c_str());
        else if (r.playing)  snprintf(st, sizeof(st), "Joue autre chose");
        else                 snprintf(st, sizeof(st), "Libre");
        lv_obj_t* sub = sqLabel(tile, st, &font_sq_14, r.selected ? SQ_INK : (r.grouped ? 0xA9C9F0 : SQ_MUTED));
        lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(sub, 146, 19);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 60, 14);

        if (!r.selected) {
            lv_obj_t* gb = sqRoundButton(tile, 44, r.grouped ? SQ_SKY : SQ_RAISED,
                                         r.grouped ? IC_CHECK : IC_PLUS, &font_sqi_26,
                                         r.grouped ? SQ_INK : SQ_SKY);
            lv_obj_align(gb, LV_ALIGN_TOP_RIGHT, -8, 8);
            lv_obj_add_event_cb(gb, groupClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
    }
}
