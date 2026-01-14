#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>
#include "lvgl.h"

// Display specifications for ESP32-P4 JC4880P443C (MIPI DSI)
// App sees LANDSCAPE 800x480, driver rotates to panel's portrait orientation
#define DISPLAY_WIDTH  800  // Landscape width (app sees this)
#define DISPLAY_HEIGHT 480  // Landscape height (app sees this)
#define DISPLAY_BUF_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT)  // Full frame buffer

// ST7701 LCD Controller pins
#define LCD_RST     5  // Reset GPIO for ST7701

// Note: MIPI DSI interface uses dedicated hardware pins on ESP32-P4
// No manual pin configuration needed for DSI data/clock

// Function declarations
bool display_init(void);
void display_deinit(void);
void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void display_set_brightness(uint8_t brightness_percent);

#endif // DISPLAY_DRIVER_H
