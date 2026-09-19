#pragma once
/**
 * Sleep timer button (issue #173) - shared by every player theme.
 *
 * Amber built its own inline in v2.0.7, so on Classic and Immersive a running
 * timer was invisible: the speaker would stop the music with nothing on screen
 * to say why, and no way to cancel it. One implementation now, on the battery
 * badge's pattern - create one per player, and a single timer refreshes them
 * all, plus the sheet when it is open.
 *
 * Idle it reads "Sleep"; while a timer runs it shows the minutes left in the
 * theme's accent colour. Tapping opens the sleep sheet (amberShowSleep(), built
 * for every theme through amberBuildOverlays()).
 */
#include "lvgl.h"

typedef enum {
    SLEEP_BTN_PILL,    // icon + text: for a layout with a row to spare
    SLEEP_BTN_ROUND,   // icon only, round: Immersive's control bar
} SleepButtonStyle;

// Returns the button, for the caller to position.
lv_obj_t* sleepButtonCreate(lv_obj_t* parent, SleepButtonStyle style);
