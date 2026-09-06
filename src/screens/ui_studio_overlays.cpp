/**
 * Studio overlays — the Queue drawer and Rooms modal from the design canvas.
 *
 * The canvas draws these OVER the player, not as separate screens: a scrim dims
 * the player, a 400px drawer slides against the right edge for the queue, and a
 * 520px card centres for rooms. The firmware had them as full screens
 * (scr_queue, scr_devices), which is why the transport vanished behind them.
 *
 * ── How they hook in without disturbing the other themes ───────────────────
 * ev_queue() and ev_devices() are shared by every theme. Each now asks
 * studioShowQueue() / studioShowRooms() first: those return false unless the
 * Studio player is the built theme, in which case the handlers fall through to
 * the original lv_screen_load(). Classic, Ambient and Immersive are untouched.
 *
 * ── Why the overlays live on their own layer ───────────────────────────────
 * They are children of scr_main but are created AFTER panel_art/panel_right, so
 * they draw on top without any z-order juggling. They are also outside those two
 * panels on purpose: setLineInMode() and setTvAudioMode() hide panel children
 * wholesale, and an overlay caught by that would vanish mid-interaction.
 */

#include "ui_common.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "studio.h"
#include "studio_icons.h"

// ── Geometry, 800x480 design space ──────────────────────────────────────────
#define OV_DRAWER_W    400
#define OV_ROOMS_W     520
#define OV_ROOMS_X     140
#define OV_ROOMS_Y     56
#define OV_ROW_H       62
#define OV_SLIDER_W    150

static lv_obj_t* ov_scrim       = nullptr;
static lv_obj_t* ov_queue       = nullptr;
static lv_obj_t* ov_queue_list  = nullptr;
static lv_obj_t* ov_queue_sub   = nullptr;
static lv_obj_t* ov_rooms       = nullptr;
static lv_obj_t* ov_rooms_list  = nullptr;
static lv_obj_t* ov_clear_btn   = nullptr;   // arms on first tap, clears on second
static uint32_t  ov_clear_armed_ms = 0;
#define OV_CLEAR_ARM_MS 4000

// Which screen the pointers above belong to.
static lv_obj_t* ov_owner = nullptr;

// Cleared with scr_main so a theme switch cannot leave dangling pointers behind
// (themeSet() deletes the whole screen).
//
// The owner check is NOT paranoia. themeSet() builds the NEW player before it
// deletes the old one, and both Classic and Studio now build overlays — so the
// old screen's delete callback fires AFTER the new overlays have already been
// stored here, and without this it would null out the live set. The symptom
// would be overlays that work until the first theme switch and then silently
// fall back to full screens.
static void ov_deleted(lv_event_t* e) {
    if ((lv_obj_t*)lv_event_get_target(e) != ov_owner) return;
    ov_owner = nullptr;
    ov_scrim = ov_queue = ov_queue_list = ov_queue_sub = nullptr;
    ov_rooms = ov_rooms_list = ov_clear_btn = nullptr;
    ov_clear_armed_ms = 0;
}

// ── Shared pieces ───────────────────────────────────────────────────────────
static lv_obj_t* ovCloseBtn(lv_obj_t* parent, int d) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, SMIN(d), SMIN(d));
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, ST_RAISED, 0);
    lv_obj_set_style_bg_color(b, ST_BORDER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_add_event_cb(b, [](lv_event_t*) { studioHideOverlay(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico = lv_label_create(b);
    lv_label_set_text(ico, ST_IC_X);
    lv_obj_set_style_text_font(ico, &font_icon_16, 0);
    lv_obj_set_style_text_color(ico, ST_TEXT2, 0);
    lv_obj_center(ico);
    return b;
}

// A pill button with an icon and a label, used by the drawer's footer.
static lv_obj_t* ovPill(lv_obj_t* parent, const char* icon, const char* text,
                        lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_height(b, SY(44));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, SMIN(10), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, ST_BORDER, 0);
    lv_obj_set_style_bg_color(b, ST_CARD, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text_fmt(l, "%s  %s", icon, text);
    lv_obj_set_style_text_font(l, &font_icon_16, 0);
    lv_obj_set_style_text_color(l, ST_TEXT2, 0);
    lv_obj_center(l);
    return b;
}

// ── Queue drawer ────────────────────────────────────────────────────────────
static void ovBuildQueue(lv_obj_t* parent) {
    ov_queue = lv_obj_create(parent);
    lv_obj_set_size(ov_queue, SX(OV_DRAWER_W), SY(480));
    lv_obj_set_pos(ov_queue, SX(800 - OV_DRAWER_W), 0);
    lv_obj_set_style_bg_color(ov_queue, ST_BG_DRAWER, 0);
    lv_obj_set_style_radius(ov_queue, 0, 0);
    lv_obj_set_style_border_width(ov_queue, 1, 0);
    lv_obj_set_style_border_side(ov_queue, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(ov_queue, ST_BORDER, 0);
    lv_obj_set_style_pad_all(ov_queue, 0, 0);
    lv_obj_remove_flag(ov_queue, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov_queue, LV_OBJ_FLAG_HIDDEN);

    // Header
    lv_obj_t* head = lv_obj_create(ov_queue);
    lv_obj_set_size(head, SX(OV_DRAWER_W), SY(64));
    lv_obj_set_pos(head, 0, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(head, ST_LINE, 0);
    lv_obj_set_style_pad_hor(head, SX(18), 0);
    lv_obj_set_style_pad_ver(head, 0, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = stLabel(head, &font_text_20, ST_TEXT, "Queue");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, SY(-10));

    ov_queue_sub = stLabel(head, &font_text_12, ST_TEXT3, "");
    lv_obj_align(ov_queue_sub, LV_ALIGN_LEFT_MID, 0, SY(12));

    lv_obj_t* x = ovCloseBtn(head, 40);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);

    // Rows
    ov_queue_list = lv_obj_create(ov_queue);
    lv_obj_set_size(ov_queue_list, SX(OV_DRAWER_W), SY(480 - 64 - 68));
    lv_obj_set_pos(ov_queue_list, 0, SY(64));
    lv_obj_set_style_bg_opa(ov_queue_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ov_queue_list, 0, 0);
    lv_obj_set_style_radius(ov_queue_list, 0, 0);
    lv_obj_set_style_pad_all(ov_queue_list, 0, 0);
    lv_obj_set_style_pad_row(ov_queue_list, 0, 0);
    lv_obj_set_flex_flow(ov_queue_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(ov_queue_list, ST_BORDER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(ov_queue_list, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(ov_queue_list, SX(4), LV_PART_SCROLLBAR);

    // Footer
    lv_obj_t* foot = lv_obj_create(ov_queue);
    lv_obj_set_size(foot, SX(OV_DRAWER_W), SY(68));
    lv_obj_set_pos(foot, 0, SY(480 - 68));
    lv_obj_set_style_bg_opa(foot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(foot, 0, 0);
    lv_obj_set_style_border_width(foot, 1, 0);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(foot, ST_LINE, 0);
    lv_obj_set_style_pad_hor(foot, SX(18), 0);
    lv_obj_set_style_pad_ver(foot, SY(12), 0);
    lv_obj_set_style_pad_column(foot, SX(10), 0);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(foot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(foot, LV_OBJ_FLAG_CLICKABLE);

    ovPill(foot, ST_IC_SHUFFLE, "Shuffle", [](lv_event_t*) {
        SonosDevice* d = sonos.getCurrentDevice();
        if (d) sonos.setShuffle(!d->shuffleMode);
    });
    // ── Clear ───────────────────────────────────────────────────────────────
    // Two taps. RemoveAllTracksFromQueue is irreversible and household-wide — it
    // empties the queue for everyone on that speaker, not just this panel — and
    // this button sits 10px from Shuffle in a drawer people are scrolling with a
    // finger. One stray tap should not be able to do that.
    //
    // Arming rather than a modal: a confirmation dialog over a drawer that is
    // itself over the player is two layers of overlay for one destructive verb,
    // and the label can say what the next tap will do just as clearly.
    ov_clear_btn = ovPill(foot, ST_IC_X, "Clear", [](lv_event_t* e) {
        lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        const bool armed = ov_clear_armed_ms &&
                           (millis() - ov_clear_armed_ms) < OV_CLEAR_ARM_MS;
        if (!armed) {
            ov_clear_armed_ms = millis();
            if (l) {
                lv_label_set_text(l, ST_IC_X "  Sure?");
                lv_obj_set_style_text_color(l, ST_ACCENT, 0);
            }
            lv_obj_set_style_border_color(b, ST_ACCENT, 0);
            return;
        }
        ov_clear_armed_ms = 0;
        sonos.clearQueue();
        studioHideOverlay();
    });
}

static void ovFillQueue(void) {
    if (!ov_queue_list) return;

    // Disarm on every open. An arm left over from a previous visit would turn the
    // first tap of this one into a wipe.
    ov_clear_armed_ms = 0;
    if (ov_clear_btn && lv_obj_get_child_count(ov_clear_btn)) {
        lv_obj_t* l = lv_obj_get_child(ov_clear_btn, 0);
        lv_label_set_text_fmt(l, "%s  %s", ST_IC_X, "Clear");
        lv_obj_set_style_text_color(l, ST_TEXT2, 0);
        lv_obj_set_style_border_color(ov_clear_btn, ST_BORDER, 0);
    }

    lv_obj_clean(ov_queue_list);

    SonosDevice* d = sonos.getCurrentDevice();
    if (!d || d->queueSize == 0) {
        lv_label_set_text(ov_queue_sub, d ? "Queue is empty" : "No speaker");
        return;
    }
    if (d->totalTracks > 0 && d->queueSize < d->totalTracks)
        lv_label_set_text_fmt(ov_queue_sub, "%d of %d tracks", d->queueSize, d->totalTracks);
    else
        lv_label_set_text_fmt(ov_queue_sub, "%d track%s", d->queueSize,
                              d->queueSize == 1 ? "" : "s");

    for (int i = 0; i < d->queueSize; i++) {
        QueueItem* it = &d->queue[i];
        const bool playing = (it->trackNumber == d->currentTrackNumber);

        lv_obj_t* row = lv_button_create(ov_queue_list);
        lv_obj_set_size(row, SX(OV_DRAWER_W), SY(56));
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, playing ? ST_CARD : ST_BG_DRAWER, 0);
        lv_obj_set_style_bg_color(row, ST_CARD, LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(row, SX(18), 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        // The playing row carries a gold left edge, exactly as the canvas draws it.
        lv_obj_set_style_border_width(row, playing ? 3 : 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, ST_ACCENT, 0);
        lv_obj_set_user_data(row, (void*)(intptr_t)it->trackNumber);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            int n = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            sonos.playQueueItem(n);
            studioHideOverlay();
        }, LV_EVENT_CLICKED, NULL);

        // The playing row swaps its track number for a play glyph.
        lv_obj_t* lead = lv_label_create(row);
        if (playing) {
            lv_label_set_text(lead, ST_IC_PLAY);
            lv_obj_set_style_text_font(lead, &font_icon_16, 0);
            lv_obj_set_style_text_color(lead, ST_ACCENT, 0);
        } else {
            lv_label_set_text_fmt(lead, "%d", it->trackNumber);
            lv_obj_set_style_text_font(lead, &font_text_12, 0);
            lv_obj_set_style_text_color(lead, ST_TEXT3, 0);
        }
        lv_obj_align(lead, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* dur = nullptr;
        if (it->duration.length()) {
            dur = stLabel(row, &font_text_12, ST_TEXT3, it->duration.c_str());
            lv_obj_set_width(dur, SX(52));
            lv_label_set_long_mode(dur, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(dur, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(dur, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        const int tw = OV_DRAWER_W - 36 - 26 - (dur ? 60 : 0);

        lv_obj_t* t = stLabel(row, &font_text_14, playing ? ST_ACCENT : ST_TEXT,
                              it->title.c_str());
        lv_obj_set_width(t, SX(tw));
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, SX(26), SY(-9));

        lv_obj_t* a = stLabel(row, &font_text_12, ST_TEXT3, it->artist.c_str());
        lv_obj_set_width(a, SX(tw));
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
        lv_obj_align(a, LV_ALIGN_LEFT_MID, SX(26), SY(11));
    }
}

// ── Rooms modal ─────────────────────────────────────────────────────────────
static void ovBuildRooms(lv_obj_t* parent) {
    ov_rooms = lv_obj_create(parent);
    lv_obj_set_size(ov_rooms, SX(OV_ROOMS_W), LV_SIZE_CONTENT);
    lv_obj_set_pos(ov_rooms, SX(OV_ROOMS_X), SY(OV_ROOMS_Y));
    lv_obj_set_style_bg_color(ov_rooms, ST_MODAL, 0);
    lv_obj_set_style_radius(ov_rooms, SMIN(16), 0);
    lv_obj_set_style_border_width(ov_rooms, 1, 0);
    lv_obj_set_style_border_color(ov_rooms, ST_BORDER, 0);
    lv_obj_set_style_pad_all(ov_rooms, 0, 0);
    lv_obj_set_style_max_height(ov_rooms, SY(400), 0);
    lv_obj_remove_flag(ov_rooms, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov_rooms, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* head = lv_obj_create(ov_rooms);
    lv_obj_set_size(head, SX(OV_ROOMS_W), SY(58));
    lv_obj_set_pos(head, 0, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(head, ST_LINE, 0);
    lv_obj_set_style_pad_hor(head, SX(20), 0);
    lv_obj_set_style_pad_ver(head, 0, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* t = stLabel(head, &font_text_20, ST_TEXT, "Rooms");
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* x = ovCloseBtn(head, 38);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);

    ov_rooms_list = lv_obj_create(ov_rooms);
    lv_obj_set_size(ov_rooms_list, SX(OV_ROOMS_W), LV_SIZE_CONTENT);
    lv_obj_set_pos(ov_rooms_list, 0, SY(58));
    lv_obj_set_style_max_height(ov_rooms_list, SY(330), 0);
    lv_obj_set_style_bg_opa(ov_rooms_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ov_rooms_list, 0, 0);
    lv_obj_set_style_radius(ov_rooms_list, 0, 0);
    lv_obj_set_style_pad_all(ov_rooms_list, SMIN(14), 0);
    lv_obj_set_style_pad_row(ov_rooms_list, SY(8), 0);
    lv_obj_set_flex_flow(ov_rooms_list, LV_FLEX_FLOW_COLUMN);
}

// Inline volume slider. user_data carries the device INDEX, so the callback can
// address a speaker that is not the selected one.
static void ovVolChanged(lv_event_t* e) {
    lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    sonos.setDeviceVolume(idx, lv_slider_get_value(s));
}

static void ovFillRooms(void) {
    if (!ov_rooms_list) return;
    lv_obj_clean(ov_rooms_list);

    const int cnt = sonos.getDeviceCount();
    SonosDevice* cur = sonos.getCurrentDevice();

    for (int i = 0; i < cnt; i++) {
        SonosDevice* d = sonos.getDevice(i);
        if (!d) continue;
        const bool sel = (cur && d->ip == cur->ip);

        lv_obj_t* card = lv_obj_create(ov_rooms_list);
        lv_obj_set_size(card, lv_pct(100), SY(OV_ROW_H));
        lv_obj_set_style_bg_color(card, sel ? ST_RAISED : ST_MODAL, 0);
        lv_obj_set_style_radius(card, SMIN(12), 0);
        lv_obj_set_style_border_width(card, sel ? 1 : 0, 0);
        lv_obj_set_style_border_color(card, ST_ACCENT_DIM, 0);
        lv_obj_set_style_pad_hor(card, SX(14), 0);
        lv_obj_set_style_pad_ver(card, 0, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

        // Tapping the name selects the room; the slider is a separate target, so
        // the card itself must NOT be clickable or it would swallow slider drags.
        lv_obj_t* hit = lv_button_create(card);
        lv_obj_set_size(hit, SX(200), SY(OV_ROW_H));
        lv_obj_align(hit, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(hit, 0, 0);
        lv_obj_set_style_shadow_width(hit, 0, 0);
        lv_obj_set_style_pad_all(hit, 0, 0);
        lv_obj_set_user_data(hit, (void*)(intptr_t)i);
        lv_obj_add_event_cb(hit, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            sonos.selectDevice(idx);
            sonos.startTasks();
            studioHideOverlay();
        }, LV_EVENT_CLICKED, NULL);

        lv_obj_t* ico = stLabel(hit, &font_icon_24, sel ? ST_ACCENT : ST_TEXT3,
                                ST_IC_SPEAKER);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* name = stLabel(hit, &font_text_16, sel ? ST_TEXT : ST_TEXT2,
                                 d->roomName.c_str());
        lv_obj_set_width(name, SX(160));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, SX(34), SY(-9));

        // Green is reserved for live playback, per the canvas.
        char sub[96];
        if (d->isPlaying && d->currentArtist.length())
            snprintf(sub, sizeof(sub), "Playing · %s", d->currentArtist.c_str());
        else
            snprintf(sub, sizeof(sub), "%s", d->isPlaying ? "Playing" : "Idle");
        lv_obj_t* st = stLabel(hit, &font_text_12, d->isPlaying ? ST_LIVE : ST_TEXT3, sub);
        lv_obj_set_width(st, SX(160));
        lv_label_set_long_mode(st, LV_LABEL_LONG_DOT);
        lv_obj_align(st, LV_ALIGN_LEFT_MID, SX(34), SY(11));

        // ── Inline volume ───────────────────────────────────────────────────
        // Selected speaker only: see the note in ui_devices_screen.cpp. Every
        // other device's `volume` is the placeholder written at discovery, and a
        // slider jumps to wherever it is pressed, so an unselected row's control
        // would send a level nobody chose.
        if (!sel) continue;

        lv_obj_t* sl = lv_slider_create(card);
        lv_obj_set_size(sl, SX(OV_SLIDER_W), SY(6));
        lv_obj_align(sl, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_slider_set_range(sl, 0, 100);
        lv_slider_set_value(sl, d->volume, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(sl, ST_GROOVE, LV_PART_MAIN);
        lv_obj_set_style_radius(sl, SMIN(3), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, sel ? ST_ACCENT : ST_TEXT3, LV_PART_INDICATOR);
        lv_obj_set_style_radius(sl, SMIN(3), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, sel ? ST_ACCENT : ST_TEXT3, LV_PART_KNOB);
        lv_obj_set_style_pad_all(sl, SMIN(6), LV_PART_KNOB);
        // RELEASED, not VALUE_CHANGED: the latter fires on every pixel of a drag
        // and would put a SOAP call on the command queue for each one.
        lv_obj_add_event_cb(sl, ovVolChanged, LV_EVENT_RELEASED, (void*)(intptr_t)i);
    }

    if (cnt == 0) {
        lv_obj_t* l = stLabel(ov_rooms_list, &font_text_14, ST_TEXT3,
                              "No speakers found - scan in Settings");
        lv_obj_set_width(l, lv_pct(100));
    }
}

// ── Public API ──────────────────────────────────────────────────────────────
void studioBuildOverlays(lv_obj_t* screen) {
    // The scrim dims the player and swallows taps outside the panels, so tapping
    // the background dismisses — which is how the canvas behaves.
    ov_scrim = lv_obj_create(screen);
    lv_obj_set_size(ov_scrim, SX(800), SY(480));
    lv_obj_set_pos(ov_scrim, 0, 0);
    lv_obj_set_style_bg_color(ov_scrim, lv_color_hex(0x060605), 0);
    lv_obj_set_style_bg_opa(ov_scrim, 160, 0);
    lv_obj_set_style_border_width(ov_scrim, 0, 0);
    lv_obj_set_style_radius(ov_scrim, 0, 0);
    lv_obj_remove_flag(ov_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ov_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ov_scrim, [](lv_event_t*) { studioHideOverlay(); },
                        LV_EVENT_CLICKED, NULL);

    ovBuildQueue(screen);
    ovBuildRooms(screen);
    ov_owner = screen;
    lv_obj_add_event_cb(screen, ov_deleted, LV_EVENT_DELETE, nullptr);

    // Close on every (re)entry to the player. The scrim and the close button were
    // the only dismissal paths, so leaving via the screensaver or Settings and
    // coming back landed you on the player with a stale panel still over it.
    lv_obj_add_event_cb(screen, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) studioHideOverlay();
    }, LV_EVENT_SCREEN_LOADED, nullptr);
}

bool studioShowQueue(void) {
    if (!ov_queue || !ov_scrim) return false;
    // Mutually exclusive. In practice the scrim makes the other trigger
    // unreachable while one is open, but nothing enforces that, and two stacked
    // panels would be a confusing way to find out.
    if (ov_rooms) lv_obj_add_flag(ov_rooms, LV_OBJ_FLAG_HIDDEN);
    ovFillQueue();
    lv_obj_remove_flag(ov_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ov_queue, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ov_scrim);
    lv_obj_move_foreground(ov_queue);
    return true;
}

bool studioShowRooms(void) {
    if (!ov_rooms || !ov_scrim) return false;
    if (ov_queue) lv_obj_add_flag(ov_queue, LV_OBJ_FLAG_HIDDEN);
    ovFillRooms();
    lv_obj_remove_flag(ov_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ov_rooms, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ov_scrim);
    lv_obj_move_foreground(ov_rooms);
    return true;
}

void studioRefreshQueue(void) {
    // ev_queue() requests a windowed fetch and opens the drawer immediately, so
    // the drawer is filled from whatever was cached at that moment. The fetch
    // lands asynchronously on the Sonos task; the only existing refresh hook is
    // gated on scr_queue being the ACTIVE screen, which it never is under Studio
    // because the drawer floats over scr_main. Without this the drawer showed
    // "Queue is empty" until it was closed and reopened.
    if (!ov_queue || lv_obj_has_flag(ov_queue, LV_OBJ_FLAG_HIDDEN)) return;
    ovFillQueue();
}

void studioHideOverlay(void) {
    if (ov_scrim) lv_obj_add_flag(ov_scrim, LV_OBJ_FLAG_HIDDEN);
    if (ov_queue) lv_obj_add_flag(ov_queue, LV_OBJ_FLAG_HIDDEN);
    if (ov_rooms) lv_obj_add_flag(ov_rooms, LV_OBJ_FLAG_HIDDEN);
}

bool studioOverlayOpen(void) {
    return ov_scrim && !lv_obj_has_flag(ov_scrim, LV_OBJ_FLAG_HIDDEN);
}
