#pragma once
/**
 * Battery badge - one speaker's battery as icon + percentage (issue #165).
 *
 * Hidden for speakers without a battery, so a screen can create one per row
 * unconditionally. Every live badge is refreshed once a second by one shared
 * timer, so a new reading - or the simulator - shows without rebuilding the
 * screen. Display rules are in battery.h.
 */
#include "lvgl.h"

// deviceIndex is the speaker's index for sonos.getDevice().
lv_obj_t* batteryBadgeCreate(lv_obj_t* parent, int deviceIndex);
