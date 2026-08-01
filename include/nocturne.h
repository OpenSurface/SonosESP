/**
 * Shared "Nocturne" tokens and helpers for the screensaver faces
 * (Orbit, Monolith, Horizon) — from nocturne-styles.css in the design handoff.
 *
 * One place for the palette and the face-root construction so the three faces
 * stay visually consistent and a token change lands everywhere at once.
 */
#ifndef NOCTURNE_H
#define NOCTURNE_H

#include "lvgl.h"
#include "ui_scale.h"

#define NOC_BG        lv_color_hex(0x161826)   // --color-bg
#define NOC_TEXT      lv_color_hex(0xE9E9ED)   // --color-text / neutral-100
#define NOC_ACCENT    lv_color_hex(0x9184D9)   // --color-accent
#define NOC_ACCENT_D  lv_color_hex(0x2A2545)   // deep accent for washes/fills
#define NOC_N300      lv_color_hex(0xB8B8C2)
#define NOC_N400      lv_color_hex(0x9A9AA6)
#define NOC_N500      lv_color_hex(0x7C7C8A)

// The clock font tier. Bitmap fonts do not scale with SX()/SY() (issue #89), so
// each panel links its own — the same pair StandBy uses.
#if defined(SCREEN_SIZE) && SCREEN_SIZE == 7
    LV_FONT_DECLARE(lv_font_clock_300);
    #define NOC_FONT  lv_font_clock_300
    #define NOC_INK   218
#else
    LV_FONT_DECLARE(lv_font_clock_240);
    #define NOC_FONT  lv_font_clock_240
    #define NOC_INK   174
#endif

// Full-screen face container: Nocturne base with the accent wash rising from the
// bottom edge, created hidden. applyClockStyle() reveals it by walking
// scr_clock's children, so a face only has to expose its root.
lv_obj_t* nocFaceRoot(lv_obj_t* parent);

// Label helper — the faces create a lot of these.
lv_obj_t* nocLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t col,
                   const char* txt);

// Clears LV_OBJ_FLAG_CLICKABLE across `root` and every descendant.
// createClockScreen() makes scr_clock clickable with a handler that calls
// exitClockScreen(), and every legacy widget clears CLICKABLE so taps fall
// through to it. lv_obj_create() is clickable by DEFAULT, so a face root and
// its children swallow the tap and the screensaver can never be dismissed.
// Recursive on purpose: naming widgets is what caused the overlapping-face bug.
// Every face MUST call this at the end of its builder.
void nocMakeInert(lv_obj_t* root);

#endif // NOCTURNE_H
