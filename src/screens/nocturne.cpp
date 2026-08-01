#include "nocturne.h"

lv_obj_t* nocFaceRoot(lv_obj_t* parent) {
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, SX(800), SY(480));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    // A vertical linear gradient stands in for the design's radial glow — LVGL
    // has no radial gradient in the RGB565 path this project renders with.
    lv_obj_set_style_bg_color(root, NOC_BG, 0);
    lv_obj_set_style_bg_grad_color(root, NOC_ACCENT_D, 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    return root;
}

lv_obj_t* nocLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t col,
                   const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}
