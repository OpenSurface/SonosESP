# JD9165 LCD driver — 7" variant  (PLACEHOLDER / NOT YET PORTED)

This folder will hold the **MIPI DSI panel driver** for the 7" **GUITION JC1060P470C**
(JD9165 controller, **1024×600**), used by the `esp32_7inch` build environment.

## Status: not implemented yet
The 7" build (`pio run -e esp32_7inch`) does **not** link until this driver exists.
The 4" build (`pio run -e esp32_4inch`) is unaffected — it uses `lib/st7701_lcd`.

## What goes here (Phase 2 — needs a physical 7" unit to validate)
Port from the reference fork https://github.com/CoopsInChina/SonosESP :
- `esp_lcd_jd9165.c` / `esp_lcd_jd9165.h`
- `jd9165_lcd.cpp` / `jd9165_lcd.h`
- the matching **GT911 touch pin map** for the 7" panel

Then `src/display_driver.cpp` selects the panel by `SCREEN_SIZE`
(ST7701 for 4", JD9165 for 7").

See **docs/MULTI_SCREEN_SUPPORT.md** (Phases 2–3) for the full plan.
