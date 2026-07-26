/**
 * Player theme system (issue #87).
 *
 * A theme is one row in the THEMES[] registry in ui_theme.cpp. Adding a new one
 * is a single entry — the settings dropdown, NVS clamping and the render path
 * all iterate the registry, so nothing else needs editing. This extensibility is
 * the core requirement: an earlier hardcoded two-theme attempt was reverted for
 * lacking it.
 *
 * A theme controls two independent things:
 *   1. `bg`    — how the player backdrop is painted (see ThemeBgMode).
 *   2. `build` — which screen builder constructs the player, so a theme can be a
 *                genuinely different LAYOUT, not just a recolour.
 *
 * CONTRACT FOR BUILDERS: every builder MUST assign all player widget globals
 * (scr_main, panel_art, panel_right, img_album, lbl_title, btn_play, …) because
 * updateUI() and the mode handlers dereference them without null checks. A theme
 * that doesn't show a widget must still create it and hide it.
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>
#include "lvgl.h"

// How the player backdrop is painted.
typedef enum {
    THEME_BG_BLUR_ART = 0,   // fullscreen blurred album art (the original look)
    THEME_BG_AMBIENT_TINT,   // deep, muted tint derived from the art's dominant colour
    THEME_BG_AMBIENT_SOLID,  // saturated full-bleed ambient colour
} ThemeBgMode;

// How the artwork is presented. displayCompletedArt() re-applies geometry every
// time new art arrives, so this has to be part of the theme rather than something
// the builder sets once.
typedef enum {
    THEME_ART_HERO = 0,   // large square, centred in the art panel (Classic)
    THEME_ART_THUMB,      // small header thumbnail (Immersive)
} ThemeArtMode;

typedef void (*ThemeBuildFn)(void);   // must create scr_main + all player globals

typedef struct {
    const char*  name;    // label in the settings dropdown
    const char*  desc;    // one-line description shown under the dropdown
    ThemeBgMode  bg;
    ThemeArtMode art;
    ThemeBuildFn build;
} ThemeDef;

extern const ThemeDef THEMES[];
extern const uint8_t  THEME_COUNT;

// Active theme index — always clamped to [0, THEME_COUNT).
extern uint8_t active_theme;

const ThemeDef* themeCurrent(void);

// Reads the saved index from NVS and clamps it. Call once in setup() BEFORE
// createMainScreen() so the first build already uses the chosen theme.
void themeLoad(void);

// Persists + applies the index. Rebuilds the player screen when it changes.
// Must be called from the main LVGL thread (settings callbacks qualify).
void themeSet(uint8_t idx);

// True when the current theme wants the blurred-art image as its backdrop.
// Gates the blur bg upload in displayCompletedArt().
bool themeUsesBlurBg(void);

// Paints the backdrop for the current theme from an (already interpolated)
// dominant colour. No-op for THEME_BG_BLUR_ART. Called from the art colour
// animation so backdrop changes cross-fade with the rest of the accent colours.
void themeApplyBackdrop(uint32_t rgb);

// Sizes/positions img_album for the current theme. Called by displayCompletedArt()
// each time new artwork is published, replacing the previously hardcoded
// "full size + centre" so a theme can show the art as a header thumbnail instead.
void themeApplyArtGeometry(lv_obj_t* img);

// Builders (registry entries point at these).
void buildClassicPlayer(void);     // ui_main_screen.cpp   — Classic + Ambient
void buildImmersivePlayer(void);   // ui_theme_immersive.cpp

#endif // UI_THEME_H
