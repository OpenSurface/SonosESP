#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <Arduino.h>
#include "lvgl.h"
#include "config.h"   // GT911 pins + TOUCH_PANEL_* come from the SCREEN_SIZE block

// Function declarations
bool touch_init(void);
void touch_read(lv_indev_t *indev, lv_indev_data_t *data);

#endif // TOUCH_DRIVER_H
