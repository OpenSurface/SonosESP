#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <Arduino.h>
#include "lvgl.h"
#include "config.h"   // GT911 pins + TOUCH_PANEL_* come from the SCREEN_SIZE block

// Function declarations
bool touch_init(void);
void touch_read(lv_indev_t *indev, lv_indev_data_t *data);

// Test-and-clear, returning true once per press that should wake a dimmed screen.
// The sampler task must not call resetScreenTimeout() itself — see the comment
// block in touch_driver.cpp — so mainAppTask does it on the sampler's behalf.
bool touch_take_wake_request(void);

#endif // TOUCH_DRIVER_H
