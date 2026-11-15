// sq_widgets.cpp — the square UI's building blocks (see sq_ui.h).
#include "sq_ui.h"
#include "ui_common.h"

static lv_style_t s_pressed;
static bool       s_styles_ready = false;

static void stylesInit(void) {
    if (s_styles_ready) return;
    lv_style_init(&s_pressed);
    lv_style_set_transform_scale(&s_pressed, 240);          // a small "squish"
    lv_style_set_bg_opa(&s_pressed, LV_OPA_COVER);
    lv_style_set_recolor(&s_pressed, lv_color_black());
    lv_style_set_recolor_opa(&s_pressed, LV_OPA_20);
    s_styles_ready = true;
}

void sqPressFeedback(lv_obj_t* btn) {
    stylesInit();
    lv_obj_add_style(btn, &s_pressed, LV_STATE_PRESSED);
}

uint32_t sqHash(const char* s) {
    uint32_t h = 2166136261u;
    while (s && *s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

void sqSetHidden(lv_obj_t* obj, bool hidden) {
    if (!obj) return;
    lv_obj_set_hidden(obj, hidden);
}

lv_obj_t* sqScreenCreate(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, SQ_W, SQ_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(SQ_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(SQ_TEXT), 0);
    lv_obj_set_style_text_font(scr, &font_sq_18, 0);
    lv_obj_set_scrollable(scr, false);
    return scr;
}

lv_obj_t* sqBox(lv_obj_t* parent, int w, int h, uint32_t bg, int radius) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(o, radius > 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t* sqLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

lv_obj_t* sqIcon(lv_obj_t* parent, const char* icon, const lv_font_t* font, uint32_t color) {
    return sqLabel(parent, icon, font, color);
}

lv_obj_t* sqRoundButton(lv_obj_t* parent, int size, uint32_t bg, const char* icon,
                        const lv_font_t* icon_font, uint32_t fg) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(b, 4);
    sqPressFeedback(b);
    lv_obj_t* ic = sqIcon(b, icon, icon_font, fg);
    lv_obj_center(ic);
    return b;
}

lv_obj_t* sqPillButton(lv_obj_t* parent, int h, uint32_t bg, uint32_t fg, const char* icon,
                       const char* text, const lv_font_t* font) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, h);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b, 16, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 8, 0);
    sqPressFeedback(b);
    if (icon) sqIcon(b, icon, &font_sqi_20, fg);
    if (text) sqLabel(b, text, font, fg);
    return b;
}

static void backClicked(lv_event_t*) { sqBack(); }

lv_obj_t* sqHeader(lv_obj_t* scr, const char* title, uint32_t color, bool back) {
    lv_obj_t* row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, SQ_W, SQ_HEADER_H);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_scrollable(row, false);
    if (back) {
        lv_obj_t* b = sqRoundButton(row, 48, SQ_RAISED, IC_BACK, &font_sqi_32, color);
        lv_obj_add_event_cb(b, backClicked, LV_EVENT_CLICKED, nullptr);
    }
    if (title) {
        lv_obj_t* t = sqLabel(row, title, &font_sq_28, color);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_flex_grow(t, 1);
    }
    return row;
}

// ── Speaker pills ───────────────────────────────────────────────────────────
#define MAX_PILLS 10
struct Pill { lv_obj_t* name; lv_obj_t* extra; };
static Pill s_pills[MAX_PILLS];
static int  s_pill_count = 0;

static void pillClicked(lv_event_t*) { sqShow(SQ_SCR_ROOMS); }

lv_obj_t* sqRoomPill(lv_obj_t* parent, bool compact) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, 48);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, compact ? 200 : 300, 0);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(SQ_SKY_DEEP), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(b, 7, 0);
    lv_obj_set_style_pad_right(b, 12, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 8, 0);
    sqPressFeedback(b);
    lv_obj_add_event_cb(b, pillClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* dot = sqBox(b, 34, 34, SQ_SKY, 17);
    lv_obj_t* ic  = sqIcon(dot, IC_SPEAKER, &font_sqi_20, SQ_INK);
    lv_obj_center(ic);

    Pill p = {};
    p.name = sqLabel(b, "…", &font_sq_18, SQ_SKY_TEXT);
    lv_label_set_long_mode(p.name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(p.name, compact ? 120 : 140, 0);
    lv_obj_set_height(p.name, 25);
    if (!compact) {
        p.extra = sqLabel(b, "", &font_sq_14, 0xA9C9F0);
    }
    sqIcon(b, IC_DOWN, &font_sqi_20, SQ_SKY_TEXT);
    if (s_pill_count < MAX_PILLS) s_pills[s_pill_count++] = p;
    return b;
}

void sqRoomPillsRefresh(void) {
    if (sonos.isDiscovering()) return;   // the device list is being rebuilt
    String room = "Aucune";
    int others = 0;
    SonosDevice* d = sonos.getCurrentDevice();
    if (d && xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(30))) {
        room   = d->roomName.length() ? d->roomName : String("Enceinte");
        others = d->groupMemberCount > 1 ? d->groupMemberCount - 1 : 0;
        xSemaphoreGive(sonos.getDeviceMutex());
    }
    static String shown;
    if (room != shown) { shown = room; Serial.printf("[SQ] Room pill: %s (+%d)\n", room.c_str(), others); }
    char extra[24] = "";
    if (others > 0) snprintf(extra, sizeof(extra), "+ %d", others);
    for (int i = 0; i < s_pill_count; ++i) {
        lv_label_set_text(s_pills[i].name, room.c_str());
        if (s_pills[i].extra) {
            lv_label_set_text(s_pills[i].extra, extra);
            sqSetHidden(s_pills[i].extra, others == 0);
        }
    }
}

// ── Cover placeholder ───────────────────────────────────────────────────────
static const uint32_t kCandy[] = {SQ_PINK, SQ_ORANGE, SQ_YELLOW, SQ_MINT, SQ_SKY, SQ_LILAC};

lv_obj_t* sqCoverPlaceholder(lv_obj_t* parent, int size, int radius, const char* seed) {
    uint32_t h  = sqHash(seed);
    uint32_t c1 = kCandy[h % 6];
    uint32_t c2 = kCandy[(h / 7 + 1 + (h % 5)) % 6];
    if (c2 == c1) c2 = SQ_BG;
    lv_obj_t* box = sqBox(parent, size, size, c1, radius);
    lv_obj_t* big = sqBox(box, size * 3 / 4, size * 3 / 4, c2, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(big, -size / 6, size / 2);
    lv_obj_t* dot = sqBox(box, size / 4, size / 4, (h & 1) ? SQ_BG : SQ_TEXT, LV_RADIUS_CIRCLE);
    // Lower right: the top corners carry the type badge and the favourite star.
    lv_obj_set_pos(dot, size - size / 4 - size / 8, size - size / 4 - size / 8);
    return box;
}
