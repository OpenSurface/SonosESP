// sq_nav.cpp — screens, the bottom dock and toasts for the square UI.
//
// The dock lives on lv_layer_top(), so there is exactly one of it and it never
// re-animates between screens; screens without a tab (keyboard, Enceintes,
// Réglages) simply hide it.
#include "sq_ui.h"

struct ScreenInfo { lv_obj_t* obj; SqTab tab; };
static ScreenInfo s_screens[SQ_SCR_COUNT] = {};
static SqScreen   s_current = SQ_SCR_PLAYER;
static SqScreen   s_history[8];
static int        s_hist_len = 0;

static lv_obj_t* s_dock = nullptr;
struct DockItem { lv_obj_t* bubble; lv_obj_t* icon; lv_obj_t* label; uint32_t color; SqScreen target; };
static DockItem  s_items[4];

static lv_obj_t* s_toast = nullptr;
static lv_obj_t* s_toast_lbl = nullptr;

static void dockClicked(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_hist_len = 0;   // a tab is a fresh start, not a step deeper
    sqShow(s_items[i].target);
}

static void dockSetActive(SqTab tab) {
    for (int i = 0; i < 4; ++i) {
        bool on = (int)tab == i;
        lv_obj_set_style_bg_opa(s_items[i].bubble, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_items[i].icon, lv_color_hex(on ? SQ_INK : s_items[i].color), 0);
        lv_obj_set_style_text_color(s_items[i].label, lv_color_hex(on ? s_items[i].color : SQ_MUTED), 0);
    }
}

static void dockCreate(void) {
    s_dock = sqBox(lv_layer_top(), SQ_W, SQ_DOCK_H, SQ_DOCK, 0);
    lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_side(s_dock, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(s_dock, 2, 0);
    lv_obj_set_style_border_color(s_dock, lv_color_hex(SQ_RAISED), 0);
    lv_obj_set_style_pad_hor(s_dock, 12, 0);
    lv_obj_set_flex_flow(s_dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dock, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const struct { const char* icon; const char* label; uint32_t color; SqScreen target; } kTabs[4] = {
        {IC_NOTE,   "Lecture",  SQ_PINK,   SQ_SCR_PLAYER},
        {IC_SEARCH, "Chercher", SQ_ORANGE, SQ_SCR_SEARCH},
        {IC_STAR,   "Favoris",  SQ_YELLOW, SQ_SCR_FAVS},
        {IC_ALBUM,  "Spotify",  SQ_MINT,   SQ_SCR_SPOTIFY},
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* b = lv_button_create(s_dock);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 104, 74);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 3, 0);
        sqPressFeedback(b);
        lv_obj_add_event_cb(b, dockClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        DockItem& it = s_items[i];
        it.color  = kTabs[i].color;
        it.target = kTabs[i].target;
        it.bubble = sqBox(b, 64, 40, kTabs[i].color, 20);
        it.icon   = sqIcon(it.bubble, kTabs[i].icon, &font_sqi_26, kTabs[i].color);
        lv_obj_center(it.icon);
        it.label  = sqLabel(b, kTabs[i].label, &font_sq_16, SQ_MUTED);
    }
}

void sqNavInit(void) {
    dockCreate();
    s_toast = sqBox(lv_layer_top(), 400, LV_SIZE_CONTENT, SQ_TEXT, 24);
    lv_obj_set_style_pad_all(s_toast, 14, 0);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, 14);
    s_toast_lbl = sqLabel(s_toast, "", &font_sq_18, SQ_INK);
    lv_obj_set_width(s_toast_lbl, LV_PCT(100));
    lv_obj_set_style_text_align(s_toast_lbl, LV_TEXT_ALIGN_CENTER, 0);
    sqSetHidden(s_toast, true);
}

static void toastHide(lv_timer_t* t) {
    sqSetHidden(s_toast, true);
    lv_timer_delete(t);
}

void sqToast(const char* text) {
    static lv_timer_t* timer = nullptr;
    lv_label_set_text(s_toast_lbl, text);
    sqSetHidden(s_toast, false);
    lv_obj_move_foreground(s_toast);
    if (timer) lv_timer_delete(timer);
    timer = lv_timer_create([](lv_timer_t* t) { toastHide(t); timer = nullptr; }, 2600, nullptr);
}

void sqRegisterScreen(SqScreen id, lv_obj_t* obj, SqTab tab) {
    s_screens[id].obj = obj;
    s_screens[id].tab = tab;
}

lv_obj_t* sqScreenObj(SqScreen scr) { return s_screens[scr].obj; }
SqScreen  sqCurrent(void) { return s_current; }

// Called when a screen comes up, so it can re-resolve its data and artwork.
extern void sqOnShow(SqScreen scr);

void sqShow(SqScreen scr) {
    if (!s_screens[scr].obj) return;
    if (s_current == SQ_SCR_FAVS && scr != SQ_SCR_FAVS) sqFavsCommit();
    if (scr != s_current) {
        if (s_hist_len == (int)(sizeof(s_history) / sizeof(s_history[0])))
            memmove(s_history, s_history + 1, sizeof(s_history) - sizeof(s_history[0])), s_hist_len--;
        s_history[s_hist_len++] = s_current;
    }
    s_current = scr;
    sqOnShow(scr);
    lv_screen_load(s_screens[scr].obj);
    bool dock = s_screens[scr].tab != SQ_TAB_NONE;
    sqSetHidden(s_dock, !dock);
    if (dock) dockSetActive(s_screens[scr].tab);
}

void sqBack(void) {
    SqScreen to = SQ_SCR_PLAYER;
    if (s_hist_len > 0) to = s_history[--s_hist_len];
    // Showing it would push the current screen back on the stack.
    int keep = s_hist_len;
    sqShow(to);
    s_hist_len = keep;
}
