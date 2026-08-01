#ifndef UI_SCALE_H
#define UI_SCALE_H

#include "lvgl.h"
#include "config.h"   // DISPLAY_WIDTH / DISPLAY_HEIGHT (resolved from SCREEN_SIZE)
#include "ui_fonts.h"

// ---------------------------------------------------------------------------
// Resolution-relative UI scaling
// ---------------------------------------------------------------------------
// All screen coordinates are authored in the original 800x480 "design space".
// SX()/SY() scale them to the active panel (DISPLAY_WIDTH x DISPLAY_HEIGHT) so a
// SINGLE layout adapts to any screen size — no per-variant layout code.
//
// On the 4" panel (800x480) the factors are exactly 1:1, so SX(v)==v and
// SY(v)==v → ZERO visual change versus the original hardcoded layout. This is
// what protects the deployed 4" fleet while we make the UI dynamic.
//
// Fonts cannot scale continuously, so FONT_* selects a size tier by width.
// See docs/MULTI_SCREEN_SUPPORT.md.
// ---------------------------------------------------------------------------

#define UI_DESIGN_W 800
#define UI_DESIGN_H 480

// Scale an X / Y coordinate (or width / height) from design space to the panel.
#define SX(v) ((lv_coord_t)((int32_t)(v) * DISPLAY_WIDTH  / UI_DESIGN_W))
#define SY(v) ((lv_coord_t)((int32_t)(v) * DISPLAY_HEIGHT / UI_DESIGN_H))

// Uniform scale for square / fixed-aspect elements (radii, circular buttons):
// uses the smaller axis factor so circles stay circular on non-matching aspects.
#define SMIN(v) (SX(v) < SY(v) ? SX(v) : SY(v))

// Font tiers by display width. Only the active SCREEN_SIZE's branch is compiled.
// (4" uses the existing fonts; the >=1024 tier is wired for the future 7" and
//  must have those font sizes enabled in lv_conf.h when that variant is built.)
#if DISPLAY_WIDTH >= 1024
    #define FONT_TITLE  &font_text_32
    #define FONT_BODY   &font_text_20
    #define FONT_SMALL  &font_text_16
#else
    #define FONT_TITLE  &font_text_24
    #define FONT_BODY   &font_text_16
    #define FONT_SMALL  &font_text_14
#endif

#endif // UI_SCALE_H
