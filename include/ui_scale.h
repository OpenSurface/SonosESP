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
// Type scales too, but not here — see uiFontsInit() in ui_fonts.cpp. The
// font_text_* names are DESIGN-SPACE sizes, exactly like the numbers passed to
// SX()/SY(): font_text_14 means "the face for design size 14", which resolves to
// Montserrat 14 on the 4" and 16 on the 7". Call sites never choose a tier.
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

// NOTE: FONT_TITLE / FONT_BODY / FONT_SMALL used to live here. They were never
// called from anywhere (0 uses against 130 direct font_text_* references) and
// are deliberately NOT reinstated: now that uiFontsInit() resolves each name per
// panel, a second tier layer on top would scale the same text twice — FONT_TITLE
// on the 7" would have picked font_text_32, which itself already resolves to a
// larger face. One scaling step, in one place.

#endif // UI_SCALE_H
