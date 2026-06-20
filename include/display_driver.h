#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>
#include "lvgl.h"
#include "config.h"   // DISPLAY_WIDTH / DISPLAY_HEIGHT come from the SCREEN_SIZE block

// Display specifications (MIPI DSI). App sees LANDSCAPE dimensions; the driver
// rotates to the panel's portrait orientation. DISPLAY_WIDTH/HEIGHT are defined
// per variant in config.h (SCREEN_SIZE) — do not redefine them here.
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
