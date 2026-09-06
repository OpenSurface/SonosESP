/**
 * Shared primitives for the Amber look — implementations for amber.h.
 */
#include "amber.h"
#include "ui_fonts.h"

lv_obj_t* ambRect(lv_obj_t* parent, int w, int h, lv_color_t col) {
    return ambRoundRect(parent, w, h, 0, col);
}

lv_obj_t* ambRoundRect(lv_obj_t* parent, int w, int h, int radius, lv_color_t col) {
    lv_obj_t* o = lv_obj_create(parent);
    // Sizes arrive in design space. A 1px hairline must stay 1px on both panels
    // rather than scaling to 1.25 and rounding to either 1 or 2 unpredictably,
    // so pass a literal 1 through untouched.
    lv_obj_set_size(o, w == 1 ? 1 : SX(w), h == 1 ? 1 : SY(h));
    lv_obj_set_style_bg_color(o, col, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius ? SMIN(radius) : 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* ambLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t col,
                  const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

lv_obj_t* ambCaption(lv_obj_t* parent, lv_color_t col, const char* txt, int track) {
    lv_obj_t* l = ambLabel(parent, &font_text_12, col, txt);
    lv_obj_set_style_text_letter_space(l, track, 0);
    return l;
}
