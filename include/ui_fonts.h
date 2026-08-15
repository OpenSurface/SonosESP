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
//
// ── The number in the name is a DESIGN SIZE, not a pixel size ──────────────
// These follow the same convention as SX()/SY(): the layout is authored against
// 800x480, and each name resolves to whatever face suits the active panel.
// font_text_14 is Montserrat 14 on the 4" and Montserrat 16 on the 7". Call
// sites just ask for the design size and never branch on SCREEN_SIZE.
// The mapping table lives in uiFontsInit() (ui_fonts.cpp).
// ---------------------------------------------------------------------------

// C linkage: LV_FONT_DEFAULT points at font_text_16, so LVGL re-declares it from
// lv_font.h (inside LVGL's extern "C" block). Both declarations must agree or any
// translation unit including both fails to compile.
#ifdef __cplusplus
extern "C" {
#endif

extern lv_font_t font_text_12;
extern lv_font_t font_text_14;
extern lv_font_t font_text_16;
extern lv_font_t font_text_20;
extern lv_font_t font_text_24;
extern lv_font_t font_text_32;
extern lv_font_t font_text_48;

#ifdef __cplusplus
}
#endif

// Must be called once at boot BEFORE any screen is built, and before lv_init().
void uiFontsInit(void);

#endif // UI_FONTS_H
