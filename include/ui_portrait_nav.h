#pragma once
/**
 * Portrait navigation frame (UI_PORTRAIT builds only).
 *
 * Every portrait screen ends in the same two bands, bottom up:
 *   - the tab bar     (PNAV_TAB_H)   Player · Library · Search · Rooms · Settings
 *   - the now-playing dock (PNAV_DOCK_H), on every screen except the player
 *
 * Like the landscape settings dock, each screen builds its OWN copy instead of
 * sharing one on lv_layer_top: a shared bar would have to be hidden by hand on
 * the clock, the boot screen and every overlay, and would repaint on top of a
 * screen mid-transition.
 *
 * Geometry is in the 480x800 portrait design space (ui_scale.h).
 */
#include "lvgl.h"
#include "config.h"

#if UI_PORTRAIT

enum PortraitTab {
    PTAB_PLAYER = 0,
    PTAB_LIBRARY,
    PTAB_SEARCH,
    PTAB_ROOMS,
    PTAB_SETTINGS,
    PTAB_COUNT
};

#define PNAV_TAB_H     64
#define PNAV_DOCK_H    60
#define PNAV_BACK_H    48    // "‹ Settings" bar on screens one level down

// Builds the tab bar (and the dock when with_dock) along the bottom of
// `screen`, with `active` highlighted. Returns the free area above them: full
// width, top-aligned, no padding, not scrollable.
lv_obj_t* portraitFrame(lv_obj_t* screen, PortraitTab active, bool with_dock);

// Height in design units of what portraitFrame() leaves free.
static inline int portraitFreeH(bool with_dock) {
    return 800 - PNAV_TAB_H - (with_dock ? PNAV_DOCK_H : 0);
}

// createSettingsSidebar() index for a Browse screen opened from a search
// result: no back bar (Browse draws its own), Search tab lit. 0-7 are the
// landscape rail's sections.
#define SB_IDX_SEARCH_BROWSE 8

// The magnifier glyph (neither icon font has one), `size` px square.
lv_obj_t* portraitSearchGlyph(lv_obj_t* parent, int size, lv_color_t col);

// The Settings tab's landing page (General, Display, WiFi, Clock, Update) and
// the Search tab (ui_search_screen.cpp). Built once at boot, like every other
// screen.
void createSettingsMenuScreen();
void createSearchScreen();
extern lv_obj_t* scr_settings_menu;
extern lv_obj_t* scr_search;

#endif  // UI_PORTRAIT
