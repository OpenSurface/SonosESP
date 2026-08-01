#ifndef UI_FONTS_H
#define UI_FONTS_H

#include "lvgl.h"

// ---------------------------------------------------------------------------
// Text fonts with Latin-1 / Latin Extended-A fallback
// ---------------------------------------------------------------------------
// LVGL's built-in Montserrat fonts only carry ASCII (0x20-0x7E) plus a sparse
// set of symbols — verified in the generated cmaps:
//     .range_start = 32,  .range_length = 95
//     .range_start = 176, .range_length = 63475   (sparse: degree, bullet, LV_SYMBOL_*)
// Everything else rendered as a tofu box, which is why decodeHTML() used to
// transliterate accents away (Beyoncé -> Beyonce, Björk -> Bjork).
//
// Rather than regenerate the built-ins — which would mean rebuilding the
// LV_SYMBOL_* glyphs the numeric keyboard depends on — each font below is a
// copy of the built-in with `.fallback` pointing at a supplement font holding
// only U+00A0-U+017F. LVGL resolves fallbacks recursively at glyph lookup, so
// ASCII and symbols still come from the original and only accented characters
// come from the supplement. Purely additive: nothing that rendered before can
// stop rendering.
//
// Covers essentially all Western/Central European text. Cyrillic, Greek, CJK
// and RTL scripts are still out of range (RTL would also need LV_USE_BIDI).
// ---------------------------------------------------------------------------

extern lv_font_t font_text_12;
extern lv_font_t font_text_14;
extern lv_font_t font_text_16;
extern lv_font_t font_text_20;
extern lv_font_t font_text_24;
extern lv_font_t font_text_32;
extern lv_font_t font_text_48;

// Must be called once at boot BEFORE any screen is built.
void uiFontsInit(void);

#endif // UI_FONTS_H
