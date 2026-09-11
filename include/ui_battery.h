#pragma once
/**
 * Battery badge - one speaker's battery as icon + number (issue #165).
 *
 * Hidden for speakers without a battery, so a screen can create one per row
 * unconditionally. Every live badge is refreshed once a second by one shared
 * timer, so a new reading - or the simulator - shows without rebuilding the
 * screen. Display rules are in battery.h.
 */
#include "lvgl.h"

// Follow whichever speaker is selected rather than a fixed one (Now Playing).
#define BATTERY_BADGE_CURRENT (-1)

// deviceIndex: the speaker's index for sonos.getDevice(), or BATTERY_BADGE_CURRENT.
// compact:     the number in smaller text and without "%", for the Amber header,
//              where "100%" does not fit between the room pill and LRC.
// Returns the badge's row object: position it like any other object.
lv_obj_t* batteryBadgeCreate(lv_obj_t* parent, int deviceIndex, bool compact = false);
