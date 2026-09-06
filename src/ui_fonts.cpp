#include "ui_fonts.h"
#include "config.h"   // DISPLAY_WIDTH (resolved from SCREEN_SIZE)
#include "amber_icons.h"   // the generated Amber icon faces

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

lv_font_t font_icon_16;
lv_font_t font_icon_24;
lv_font_t font_icon_32;
lv_font_t font_icon_40;
lv_font_t font_icon_wx_32;
lv_font_t font_icon_wx_64;

// Intermediate links in the icon fallback chain — see the note on uiFontsInit().
LV_FONT_DECLARE(lv_font_mdi_16);
LV_FONT_DECLARE(lv_font_mdi_24);
LV_FONT_DECLARE(lv_font_mdi_32);
LV_FONT_DECLARE(lv_font_mdi_40);
static lv_font_t mdi_fb_16;
static lv_font_t mdi_fb_24;
static lv_font_t mdi_fb_32;
static lv_font_t mdi_fb_40;

// The built-ins live in flash as `const`, so their .fallback cannot be set in
// place — writing to rodata would fault. Copy the struct into RAM (one
// lv_font_t each, ~40 bytes) and set the fallback on the copy instead.
#define WIRE(dst, base, fb)  do { (dst) = (base); (dst).fallback = &(fb); } while (0)

// ── The icon fallback chain ─────────────────────────────────────────────────
//
//     font_icon_N  ->  mdi_fb_N  ->  font_text_N  ->  lv_font_latinext_N
//     (Amber icons)   (MDI icons)   (ASCII)         (accented Latin)
//
// LVGL resolves .fallback recursively at glyph lookup, so one font handles every
// character a label can be given. That matters more than it looks:
//
//   - The Amber faces hold ICONS ONLY. Without a chain, "AMB_IC_REFRESH \" Scan\""
//     would render the icon and then four tofu boxes.
//   - A handful of glyphs have no counterpart in the design canvas (MDI_ALERT,
//     MDI_ARROW_LEFT, the line-in/TV heroes). They stay on MDI and still resolve.
//   - ui_handlers.cpp writes MDI glyphs into lbl_wifi_status and lbl_ota_status,
//     which the screens now build with an icon font. Those keep working without
//     ui_handlers.cpp having to be rewritten in step.
//
// ORDER MATTERS, twice over: WIRE copies the struct BY VALUE, so every link must
// be wired before whatever points at it. Text first, then the MDI links, then
// the icon faces. Do not reorder.
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

    // Icons step up with the layout. Only 16/24/32/40 are generated, so each
    // design size takes the nearest face at or above its scaled size; 40 has
    // nothing above it and holds, exactly as font_text_32/48 do.
    WIRE(mdi_fb_16, lv_font_mdi_24, font_text_16);
    WIRE(mdi_fb_24, lv_font_mdi_32, font_text_20);
    WIRE(mdi_fb_32, lv_font_mdi_40, font_text_24);
    WIRE(mdi_fb_40, lv_font_mdi_40, font_text_32);

    WIRE(font_icon_16, lv_font_amber_24, mdi_fb_16);   // 16 -> 20, snapped to 24
    WIRE(font_icon_24, lv_font_amber_32, mdi_fb_24);   // 24 -> 30, snapped to 32
    WIRE(font_icon_32, lv_font_amber_40, mdi_fb_32);   // 32 -> 40
    WIRE(font_icon_40, lv_font_amber_40, mdi_fb_40);   // 40 -> 50, held at 40
    font_icon_wx_32 = lv_font_amber_wx_64;                // 32 -> 40, snapped to 64
    font_icon_wx_64 = lv_font_amber_wx_64;
#else
    // ── 4" (800x480) — the design space, so every name is its literal size ──
    WIRE(font_text_12, lv_font_montserrat_12, lv_font_latinext_12);
    WIRE(font_text_14, lv_font_montserrat_14, lv_font_latinext_14);
    WIRE(font_text_16, lv_font_montserrat_16, lv_font_latinext_16);
    WIRE(font_text_20, lv_font_montserrat_20, lv_font_latinext_20);
    WIRE(font_text_24, lv_font_montserrat_24, lv_font_latinext_24);
    WIRE(font_text_32, lv_font_montserrat_32, lv_font_latinext_32);
    WIRE(font_text_48, lv_font_montserrat_48, lv_font_latinext_48);

    WIRE(mdi_fb_16, lv_font_mdi_16, font_text_16);
    WIRE(mdi_fb_24, lv_font_mdi_24, font_text_20);
    WIRE(mdi_fb_32, lv_font_mdi_32, font_text_24);
    WIRE(mdi_fb_40, lv_font_mdi_40, font_text_32);

    WIRE(font_icon_16, lv_font_amber_16, mdi_fb_16);
    WIRE(font_icon_24, lv_font_amber_24, mdi_fb_24);
    WIRE(font_icon_32, lv_font_amber_32, mdi_fb_32);
    WIRE(font_icon_40, lv_font_amber_40, mdi_fb_40);
    font_icon_wx_32 = lv_font_amber_wx_32;
    font_icon_wx_64 = lv_font_amber_wx_64;
#endif
}
