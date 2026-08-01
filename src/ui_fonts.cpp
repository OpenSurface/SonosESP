#include "ui_fonts.h"

LV_FONT_DECLARE(lv_font_latinext_12);
LV_FONT_DECLARE(lv_font_latinext_14);
LV_FONT_DECLARE(lv_font_latinext_16);
LV_FONT_DECLARE(lv_font_latinext_20);
LV_FONT_DECLARE(lv_font_latinext_24);
LV_FONT_DECLARE(lv_font_latinext_32);
LV_FONT_DECLARE(lv_font_latinext_48);

lv_font_t font_text_12;
lv_font_t font_text_14;
lv_font_t font_text_16;
lv_font_t font_text_20;
lv_font_t font_text_24;
lv_font_t font_text_32;
lv_font_t font_text_48;

// The built-ins live in flash as `const`, so their .fallback cannot be set in
// place — writing to rodata would fault. Copy the struct into RAM (one
// lv_font_t each, ~40 bytes) and set the fallback on the copy instead.
#define WIRE(dst, base, fb)  do { (dst) = (base); (dst).fallback = &(fb); } while (0)

void uiFontsInit(void) {
    WIRE(font_text_12, lv_font_montserrat_12, lv_font_latinext_12);
    WIRE(font_text_14, lv_font_montserrat_14, lv_font_latinext_14);
    WIRE(font_text_16, lv_font_montserrat_16, lv_font_latinext_16);
    WIRE(font_text_20, lv_font_montserrat_20, lv_font_latinext_20);
    WIRE(font_text_24, lv_font_montserrat_24, lv_font_latinext_24);
    WIRE(font_text_32, lv_font_montserrat_32, lv_font_latinext_32);
    WIRE(font_text_48, lv_font_montserrat_48, lv_font_latinext_48);
}
