// sq_keyboard.cpp — big AZERTY keyboard for the square UI (search, Wi-Fi).
//
// One lv_buttonmatrix per keyboard rather than forty buttons: a single object
// to lay out and redraw, which matters when the whole thing is redrawn on every
// layer switch.
#include "sq_ui.h"

struct Kbd {
    lv_obj_t*     bm;
    lv_obj_t*     ta;
    lv_event_cb_t on_enter;
    int           layer;     // 0 lower, 1 upper, 2 digits/symbols
    const char*   action;
    const char**  maps[3];
};

#define BKSP IC_BKSP
#define SHIFT "Aa"            // Nunito has no ⇧ glyph

// Maps are rebuilt per keyboard because the action key label differs.
static const char** buildMap(int layer, const char* action) {
    static const char* lower[] = {
        "a","z","e","r","t","y","u","i","o","p","\n",
        "q","s","d","f","g","h","j","k","l","m","\n",
        SHIFT,"w","x","c","v","b","n","é",BKSP,"\n",
        "123"," ","@ACTION@",""};
    static const char* upper[] = {
        "A","Z","E","R","T","Y","U","I","O","P","\n",
        "Q","S","D","F","G","H","J","K","L","M","\n",
        SHIFT,"W","X","C","V","B","N","É",BKSP,"\n",
        "123"," ","@ACTION@",""};
    static const char* sym[] = {
        "1","2","3","4","5","6","7","8","9","0","\n",
        "-","_",".",",","'","!","?","@","&","/","\n",
        "#","%","+","=","(",")",":",";",BKSP,"\n",
        "abc"," ","@ACTION@",""};
    const char** src = layer == 0 ? lower : layer == 1 ? upper : sym;
    size_t n = 0;
    while (src[n][0]) n++;
    const char** out = (const char**)lv_malloc((n + 1) * sizeof(char*));
    for (size_t i = 0; i <= n; ++i)
        out[i] = !strcmp(src[i], "@ACTION@") ? action : src[i];
    return out;
}

static void applyCtrl(lv_obj_t* bm, int layer) {
    // Row 3: shift, 7 keys, a double-width backspace. Row 4: mode, space, action.
    // Widths are relative units within a row.
    int idx = 0;
    for (int row = 0; row < 4; ++row) {
        int keys = row < 2 ? 10 : (row == 2 ? 9 : 3);
        for (int k = 0; k < keys; ++k, ++idx) {
            lv_buttonmatrix_ctrl_t c = LV_BUTTONMATRIX_CTRL_NO_REPEAT;
            int w = 1;
            if (row == 2 && k == 8) { w = 2; c = (lv_buttonmatrix_ctrl_t)0; }   // backspace repeats
            if (row == 3) w = k == 1 ? 5 : (k == 0 ? 2 : 3);
            lv_buttonmatrix_set_button_ctrl(bm, idx, c);
            lv_buttonmatrix_set_button_width(bm, idx, w);
        }
    }
    (void)layer;
}

static void setLayer(Kbd* k, int layer) {
    k->layer = layer;
    lv_buttonmatrix_set_map(k->bm, k->maps[layer]);
    applyCtrl(k->bm, layer);
}

static void kbdEvent(lv_event_t* e) {
    Kbd* k = (Kbd*)lv_event_get_user_data(e);
    lv_obj_t* bm = (lv_obj_t*)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(bm);
    const char* txt = lv_buttonmatrix_get_button_text(bm, id);
    if (!txt) return;
    if (!strcmp(txt, BKSP)) { lv_textarea_delete_char(k->ta); return; }
    if (!strcmp(txt, SHIFT)) { setLayer(k, k->layer == 0 ? 1 : 0); return; }
    if (!strcmp(txt, "123")) { setLayer(k, 2); return; }
    if (!strcmp(txt, "abc")) { setLayer(k, 0); return; }
    if (txt == k->action || !strcmp(txt, k->action)) {
        if (k->on_enter) k->on_enter(e);
        return;
    }
    lv_textarea_add_text(k->ta, txt);
    if (k->layer == 1) setLayer(k, 0);   // one capital, like a phone
}

lv_obj_t* sqKeyboardCreate(lv_obj_t* parent, lv_obj_t* ta, const char* action,
                           uint32_t action_color, lv_event_cb_t on_enter) {
    Kbd* k = (Kbd*)lv_malloc(sizeof(Kbd));
    memset(k, 0, sizeof(*k));
    k->ta = ta;
    k->on_enter = on_enter;
    k->action = action;
    for (int i = 0; i < 3; ++i) k->maps[i] = buildMap(i, action);

    lv_obj_t* bm = lv_buttonmatrix_create(parent);
    k->bm = bm;
    lv_obj_remove_style_all(bm);
    const int key_h = SQ_TALL ? 60 : 56;   // taller keys where the screen allows
    lv_obj_set_size(bm, SQ_W - 16, 4 * key_h + 3 * 8);
    lv_obj_set_style_pad_row(bm, 8, 0);
    lv_obj_set_style_pad_column(bm, 5, 0);
    lv_obj_set_style_radius(bm, 14, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bm, lv_color_hex(SQ_RAISED), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bm, lv_color_hex(SQ_RAISED2), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(bm, lv_color_hex(SQ_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(bm, &font_sq_24, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(bm, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(bm, 0, LV_PART_ITEMS);
    // Icon keys (backspace) need the icon font; the matrix has one font, so the
    // backspace glyph falls back through font_sq_24 -> font_sqi_26.
    static lv_font_t kbd_font;
    static bool kbd_font_ready = false;
    if (!kbd_font_ready) {
        kbd_font = font_sq_24;
        kbd_font.fallback = &font_sqi_26;
        kbd_font_ready = true;
    }
    lv_obj_set_style_text_font(bm, &kbd_font, LV_PART_ITEMS);
    lv_obj_add_event_cb(bm, kbdEvent, LV_EVENT_VALUE_CHANGED, k);
    lv_obj_add_event_cb(bm, [](lv_event_t* e) {
        // Colour the action key: draw-task hook on the matrix items.
        lv_draw_task_t* t = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t* base = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(t);
        if (base->part != LV_PART_ITEMS) return;
        Kbd* k = (Kbd*)lv_event_get_user_data(e);
        lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
        const char* txt = lv_buttonmatrix_get_button_text(obj, base->id1);
        if (!txt || strcmp(txt, k->action)) return;
        if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) {
            lv_draw_fill_dsc_t* f = lv_draw_task_get_fill_dsc(t);
            if (f) f->color = lv_color_hex((uint32_t)(uintptr_t)lv_obj_get_user_data(obj));
        } else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_LABEL) {
            lv_draw_label_dsc_t* l = lv_draw_task_get_label_dsc(t);
            if (l) l->color = lv_color_hex(SQ_INK);
        }
    }, LV_EVENT_DRAW_TASK_ADDED, k);
    lv_obj_set_send_draw_task_events(bm, true);
    lv_obj_set_user_data(bm, (void*)(uintptr_t)action_color);
    setLayer(k, 0);
    return bm;
}
