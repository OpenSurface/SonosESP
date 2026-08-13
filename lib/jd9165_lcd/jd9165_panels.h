#pragma once
#include <stdint.h>
#include "esp_lcd_jd9165.h"

/**
 * Panel variant registry for the 7" JD9165 boards.
 *
 * GUITION ship different LCD panels under the SAME product code — their own
 * demo package carries separate "New_Panel" and "Old_Panel" projects with
 * different init sequences AND different DSI timings. Nothing in firmware can
 * tell them apart: same JD9165 controller, same 1024x600, same chip ID. Reading
 * the panel ID over DSI is not an option either — esp_lcd_panel_io_rx_param()
 * hangs on ESP32-P4 (espressif/esp-idf#15358).
 *
 * So this is a registry, not a pair of #ifdefs. Adding a future revision is ONE
 * row here: no new build environment, no new firmware binary, no extra release
 * artifact, and no change to OTA or the web installer. The boot wizard cycles
 * through whatever is in this table until the user confirms one, so a new entry
 * is picked up automatically.
 */

typedef struct {
    const char* name;        ///< shown in Settings and the boot wizard
    const char* desc;        ///< one-line hint

    const jd9165_lcd_init_cmd_t* init_cmds;   ///< NULL = driver's built-in table
    uint16_t    init_cmds_size;

    uint32_t pclk_mhz;       ///< DPI pixel clock
    uint16_t hsync_pulse, hsync_back, hsync_front;
    uint16_t vsync_pulse, vsync_back, vsync_front;
} JD9165PanelDef;

extern const JD9165PanelDef JD9165_PANELS[];
extern const uint8_t        JD9165_PANEL_COUNT;

/// Clamps an index into the registry.
uint8_t jd9165PanelClamp(int idx);
