# JD9165 LCD driver — 7" variant  (CODE-COMPLETE / NOT YET HARDWARE-VALIDATED)

MIPI DSI panel driver for the 7" **GUITION JC1060P470C** (JD9165 controller,
**1024×600 native landscape**), used by the `esp32_7inch` build environment.

## Status: builds & links, awaiting bench validation
`pio run -e esp32_7inch` compiles and links cleanly. The driver has **not** yet
been run on a physical 7" unit — panel init timings, touch orientation, and the
backlight/reset GPIOs (LCD_RST=23, BL=23, touch INT=11/RST=22) are taken from the
reference fork and must be confirmed on hardware before a 7" release.

## Contents (ported from https://github.com/CoopsInChina/SonosESP)
- `esp_lcd_jd9165.c` / `esp_lcd_jd9165.h` — Espressif-style JD9165 panel driver
  (init command table + `JD9165_1024_600_PANEL_60HZ_DPI_CONFIG`).
- `jd9165_lcd.cpp` / `jd9165_lcd.h` — thin wrapper matching the `st7701_lcd`
  API (`begin()/get_handle()/lcd_draw_bitmap()/example_bsp_set_lcd_backlight()`),
  so `src/display_driver.cpp` can select the panel by `SCREEN_SIZE`.

## How it wires up
- `include/config.h` (`SCREEN_SIZE == 7`) resolves the dims/pins.
- `src/display_driver.cpp` — native landscape, **no rotation** (unlike the 4"
  ST7701 which rotates a portrait panel); flush pushes the framebuffer directly.
- `src/touch_driver.cpp` — GT911 native-landscape mapping (no 90° rotation).
- `platformio.ini` `[env:esp32_7inch]` sets `-DSCREEN_SIZE=7` and
  `lib_ignore = st7701_lcd`.

See **docs/MULTI_SCREEN_SUPPORT.md** for the full plan.
