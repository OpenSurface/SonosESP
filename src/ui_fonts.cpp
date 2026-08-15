#include "ui_fonts.h"
#include "config.h"   // DISPLAY_WIDTH (resolved from SCREEN_SIZE)

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
#if DISPLAY_WIDTH >= 1024
    // ── 7" (1024x600) ───────────────────────────────────────────────────────
    // SX/SY scale the layout by ~1.25 but glyphs cannot scale continuously, so
    // each design size is re-pointed at the next face up. Without this the type
    // kept its 800x480 size on a larger panel and ended up occupying ~78% of the
    // relative space it does on the 4" — the "everything looks small and sparse"
    // half of issue #89.
    //
    // Sizes are snapped to the faces we hold a Latin-Ext supplement for
    // (12/14/16/20/24/32/48). montserrat_18 and _28 are compiled in but have no
    // supplement, so using them here would render ASCII at one size and accented
    // characters at another.
    //
    // 32 and 48 stay put: the next supplement-backed step is 48, which is 1.5x
    // and overshoots badly. Both are already display sizes where holding still
    // is the lesser evil.
    WIRE(font_text_12, lv_font_montserrat_16, lv_font_latinext_16);   // 12 -> 16
    WIRE(font_text_14, lv_font_montserrat_16, lv_font_latinext_16);   // 14 -> 16
    WIRE(font_text_16, lv_font_montserrat_20, lv_font_latinext_20);   // 16 -> 20
    WIRE(font_text_20, lv_font_montserrat_24, lv_font_latinext_24);   // 20 -> 24
    WIRE(font_text_24, lv_font_montserrat_32, lv_font_latinext_32);   // 24 -> 32
    WIRE(font_text_32, lv_font_montserrat_32, lv_font_latinext_32);   // 32 -> 32
    WIRE(font_text_48, lv_font_montserrat_48, lv_font_latinext_48);   // 48 -> 48
#else
    // ── 4" (800x480) — the design space, so every name is its literal size ──
    WIRE(font_text_12, lv_font_montserrat_12, lv_font_latinext_12);
    WIRE(font_text_14, lv_font_montserrat_14, lv_font_latinext_14);
    WIRE(font_text_16, lv_font_montserrat_16, lv_font_latinext_16);
    WIRE(font_text_20, lv_font_montserrat_20, lv_font_latinext_20);
    WIRE(font_text_24, lv_font_montserrat_24, lv_font_latinext_24);
    WIRE(font_text_32, lv_font_montserrat_32, lv_font_latinext_32);
    WIRE(font_text_48, lv_font_montserrat_48, lv_font_latinext_48);
#endif
}
