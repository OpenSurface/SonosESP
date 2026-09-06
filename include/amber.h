/**
 * "Amber" design tokens — the warm palette from the Claude Design canvas
 * (SonosESP Amber.dc.html / SonosESP Boot + Screensaver.dc.html).
 *
 * One place for the palette so the boot sequence, the Amber player theme and
 * the Amber screensaver face stay in step and a retune lands everywhere at once
 * — the same job nocturne.h does for the cool Nocturne faces.
 *
 * These are DELIBERATELY not the COL_* globals. COL_* is the shipped SonosESP
 * palette that Classic/Ambient/Immersive and every settings screen render with;
 * Amber is a second, self-contained look that opts in by naming these tokens.
 * Nothing here changes what an existing theme draws.
 *
 * The canvas names eight swatches. The extra entries below are the intermediate
 * surfaces the artboards actually use (the artwork column is a shade lighter
 * than the page, the queue drawer a shade lighter again), kept as named tokens
 * rather than inline literals so the UI linter can see them.
 */
#ifndef AMBER_H
#define AMBER_H

#include "lvgl.h"
#include "ui_scale.h"

// ── Surfaces, darkest to lightest ───────────────────────────────────────────
#define AMB_HEX_BG          0x0B0A09   // the page itself
#define AMB_HEX_BG_ART      0x0E0D0C   // artwork column
#define AMB_HEX_BG_DRAWER   0x100F0E   // queue drawer
#define AMB_HEX_PANEL       0x131211   // settings rail, now-playing dock
#define AMB_HEX_MODAL       0x151311   // rooms dialog
#define AMB_HEX_CARD        0x171513   // card, chip, header button
#define AMB_HEX_RAISED      0x1C1A18   // dropdown, round icon button

// ── Lines and grooves ───────────────────────────────────────────────────────
#define AMB_HEX_LINE_SOFT   0x1E1B18   // row separator inside a page
#define AMB_HEX_LINE        0x23201C   // column / structural hairline
#define AMB_HEX_BORDER      0x2A2622   // card border, slider groove
#define AMB_HEX_GROOVE      0x33302B   // groove on a raised surface, dashed border

// ── Gold: the one action colour ─────────────────────────────────────────────
#define AMB_HEX_ACCENT      0xE0B252
#define AMB_HEX_ACCENT_HI   0xEFC468   // pressed / hover
#define AMB_HEX_ACCENT_DIM  0x4A3D22   // gold hairline border
#define AMB_HEX_ACCENT_WASH 0x241F16   // gold-tinted surface (check chip, active pill)
#define AMB_HEX_ON_ACCENT   0x1A1408   // text/icon ON gold — near-black, not pure

// ── Text ────────────────────────────────────────────────────────────────────
#define AMB_HEX_TEXT_HI     0xFFFFFF   // pure white - the synced lyric line ONLY.
                                      // Nothing else on the panel earns it: the
                                      // warm ramp is built around F5F1EA, and a
                                      // second white makes both look like a
                                      // mistake. The current lyric is on screen
                                      // for a few seconds at a time and is the
                                      // one string worth the extra contrast.
#define AMB_HEX_TEXT        0xF5F1EA   // primary
#define AMB_HEX_TEXT_BRIGHT 0xEDE8E0   // neutral fill (volume indicator)
#define AMB_HEX_TEXT2       0xC9C2B8   // secondary
#define AMB_HEX_TEXT3       0x8E877D   // muted / caption
#define AMB_HEX_FAINT       0x4E4941   // disabled glyph

// ── Semantic: green means "this speaker is live" and nothing else ───────────
#define AMB_HEX_LIVE        0x6FCF8E
#define AMB_HEX_LIVE_DIM    0x2C4A35   // its hairline border

// lv_color_t forms — what the builders actually pass to LVGL.
#define AMB_BG          lv_color_hex(AMB_HEX_BG)
#define AMB_BG_ART      lv_color_hex(AMB_HEX_BG_ART)
#define AMB_BG_DRAWER   lv_color_hex(AMB_HEX_BG_DRAWER)
#define AMB_PANEL       lv_color_hex(AMB_HEX_PANEL)
#define AMB_MODAL       lv_color_hex(AMB_HEX_MODAL)
#define AMB_CARD        lv_color_hex(AMB_HEX_CARD)
#define AMB_RAISED      lv_color_hex(AMB_HEX_RAISED)
#define AMB_LINE_SOFT   lv_color_hex(AMB_HEX_LINE_SOFT)
#define AMB_LINE        lv_color_hex(AMB_HEX_LINE)
#define AMB_BORDER      lv_color_hex(AMB_HEX_BORDER)
#define AMB_GROOVE      lv_color_hex(AMB_HEX_GROOVE)
#define AMB_ACCENT      lv_color_hex(AMB_HEX_ACCENT)
#define AMB_ACCENT_HI   lv_color_hex(AMB_HEX_ACCENT_HI)
#define AMB_ACCENT_DIM  lv_color_hex(AMB_HEX_ACCENT_DIM)
#define AMB_ACCENT_WASH lv_color_hex(AMB_HEX_ACCENT_WASH)
#define AMB_ON_ACCENT   lv_color_hex(AMB_HEX_ON_ACCENT)
#define AMB_TEXT_HI     lv_color_hex(AMB_HEX_TEXT_HI)
#define AMB_TEXT        lv_color_hex(AMB_HEX_TEXT)
#define AMB_TEXT_BRIGHT lv_color_hex(AMB_HEX_TEXT_BRIGHT)
#define AMB_TEXT2       lv_color_hex(AMB_HEX_TEXT2)
#define AMB_TEXT3       lv_color_hex(AMB_HEX_TEXT3)
#define AMB_FAINT       lv_color_hex(AMB_HEX_FAINT)
#define AMB_LIVE        lv_color_hex(AMB_HEX_LIVE)
#define AMB_LIVE_DIM    lv_color_hex(AMB_HEX_LIVE_DIM)

// ── Shared primitives ───────────────────────────────────────────────────────
// A plain filled rectangle: no border, no radius, not clickable, not scrollable.
// Both artboards are built almost entirely from these (hairlines, grooves, bars,
// equaliser segments), and lv_obj_create() defaults every one of those the wrong
// way, so hand-rolling it each time is six calls that are easy to get wrong.
lv_obj_t* ambRect(lv_obj_t* parent, int w, int h, lv_color_t col);

// ambRect() plus a corner radius, for pills and progress fills.
lv_obj_t* ambRoundRect(lv_obj_t* parent, int w, int h, int radius, lv_color_t col);

// Label helper, mirroring nocLabel().
lv_obj_t* ambLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t col,
                  const char* txt);

// Tracked caption ("NEXT", "HUMIDITY", "PAUSED · LIVING ROOM"). The canvas sets
// these at 10.5-12px/700 with .16-.22em letter-spacing; LVGL has no letter-space
// shorthand on creation, so this bundles the three calls.
lv_obj_t* ambCaption(lv_obj_t* parent, lv_color_t col, const char* txt, int track);

#endif // AMBER_H
