#include "display_driver.h"
#include "config.h"
#include "../lib/st7701_lcd/st7701_lcd.h"
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_private/esp_cache_private.h>
#include <driver/ppa.h>

#define USE_PPA_ACCELERATION 0  // Disable hardware acceleration (causes glitches)

static st7701_lcd* lcd = NULL;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;
static lv_color_t *rotate_buf = NULL;  // Rotation buffer
static lv_display_t *disp = NULL;
static bsp_lcd_handles_t lcd_handles;

#if USE_PPA_ACCELERATION
static ppa_client_handle_t ppa_handle = NULL;
static size_t cache_line_size = 0;
#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))
#endif

#if USE_PPA_ACCELERATION
// Hardware-accelerated rotation using ESP32-P4 PPA
static void rotate_image_90_ppa(const uint16_t *src, uint16_t *dst, int width, int height) {
    ppa_srm_oper_config_t oper_config;

    // Input configuration
    oper_config.in.buffer = (void *)src;
    oper_config.in.pic_w = width;
    oper_config.in.pic_h = height;
    oper_config.in.block_w = width;
    oper_config.in.block_h = height;
    oper_config.in.block_offset_x = 0;
    oper_config.in.block_offset_y = 0;
    oper_config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    // Output configuration
    oper_config.out.buffer = dst;
    oper_config.out.buffer_size = ALIGN_UP(sizeof(uint16_t) * width * height, cache_line_size);
    oper_config.out.pic_w = height;  // Swapped for rotation
    oper_config.out.pic_h = width;   // Swapped for rotation
    oper_config.out.block_offset_x = 0;
    oper_config.out.block_offset_y = 0;
    oper_config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    // Rotation settings
    oper_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;  // 270° = 90° clockwise
    oper_config.scale_x = 1.0;
    oper_config.scale_y = 1.0;
    oper_config.rgb_swap = 0;
    oper_config.byte_swap = 0;
    oper_config.mode = PPA_TRANS_MODE_BLOCKING;

    ppa_do_scale_rotate_mirror(ppa_handle, &oper_config);
}
#endif

// Software rotation function - rotate landscape 800x480 to portrait 480x800
static void rotate_image_90(const uint16_t *src, uint16_t *dst, int width, int height) {
    // Block sizes for cache-efficient rotation
    constexpr int block_w = 256;
    constexpr int block_h = 32;

    for (int i = 0; i < height; i += block_h) {
        int max_height = (i + block_h > height) ? height : (i + block_h);

        for (int j = 0; j < width; j += block_w) {
            int max_width = (j + block_w > width) ? width : (j + block_w);

            for (int x = i; x < max_height; x++) {
                for (int y = j; y < max_width; y++) {
                    // Source pixel at (x, y) -> reading as (row, col)
                    const uint16_t *src_pixel = src + (x * width + y);

                    // 90° rotation formula from reference: (x, y) -> (y, height - 1 - x)
                    uint16_t *dst_pixel = dst + (y * height + (height - 1 - x));
                    *dst_pixel = *src_pixel;
                }
            }
        }
    }
}

bool display_init(void) {
    Serial.println("[Display] Initializing MIPI DSI interface for ST7701...");

#if USE_PPA_ACCELERATION
    // Initialize PPA for hardware-accelerated rotation
    ppa_client_config_t ppa_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    if (ppa_register_client(&ppa_config, &ppa_handle) == ESP_OK) {
        esp_cache_get_alignment(MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM, &cache_line_size);
        Serial.println("[Display] PPA hardware acceleration enabled");
    } else {
        Serial.println("[Display] WARNING: PPA acceleration failed, using software rotation");
        ppa_handle = NULL;
    }
#endif

    // Create ST7701 LCD instance
    lcd = new st7701_lcd(LCD_RST);
    if (!lcd) {
        Serial.println("[Display] ERROR: Failed to create LCD instance!");
        return false;
    }

    // Initialize the LCD
    lcd->begin();
    lcd->get_handle(&lcd_handles);

    Serial.println("[Display] ST7701 LCD initialized successfully");

    // Allocate LVGL buffers in PSRAM - LANDSCAPE dimensions for LVGL (800x480)
    buf1 = (lv_color_t *)heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t *)heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    // Allocate rotation buffer - PORTRAIT dimensions for panel (480x800)
    rotate_buf = (lv_color_t *)heap_caps_malloc(DISPLAY_HEIGHT * DISPLAY_WIDTH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    if (!buf1 || !buf2 || !rotate_buf) {
        Serial.println("[Display] ERROR: Failed to allocate buffers!");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        if (rotate_buf) heap_caps_free(rotate_buf);
        return false;
    }

    Serial.printf("[Display] LVGL buffers: %d bytes each (landscape %dx%d)\n",
                  DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t), DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Serial.printf("[Display] Rotate buffer: %d bytes (portrait %dx%d)\n",
                  PANEL_WIDTH * PANEL_HEIGHT * sizeof(lv_color_t), PANEL_WIDTH, PANEL_HEIGHT);
    Serial.printf("[Display] Free PSRAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // LVGL v9 display initialization - Create as LANDSCAPE (800x480)
    // App renders in landscape, we rotate to portrait in flush callback
    disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!disp) {
        Serial.println("[Display] ERROR: Failed to create display");
        return false;
    }

    lv_display_set_flush_cb(disp, display_flush);
    lv_display_set_buffers(disp, buf1, buf2, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);

    // DON'T use lv_display_set_rotation - we do rotation manually in flush callback

    Serial.println("[Display] Ready! 800x480 landscape with manual 90° rotation to portrait panel");
    return true;
}

void display_set_brightness(uint8_t brightness_percent) {
    if (lcd) {
        // Clamp brightness to 0-100%
        if (brightness_percent > 100) brightness_percent = 100;
        lcd->example_bsp_set_lcd_backlight(brightness_percent);
    }
}

void display_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) {
    if (!lcd || !lcd_handles.panel || !rotate_buf) {
        lv_display_flush_ready(disp_drv);
        return;
    }

    // Rotate the entire frame from landscape 800x480 to portrait 480x800 for panel
    // Panel DPI is now configured for 480×800 portrait
#if USE_PPA_ACCELERATION
    if (ppa_handle) {
        // Use hardware-accelerated rotation
        rotate_image_90_ppa((uint16_t *)px_map, (uint16_t *)rotate_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    } else {
        // Fallback to software rotation
        rotate_image_90((uint16_t *)px_map, (uint16_t *)rotate_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
#else
    // Software rotation only
    rotate_image_90((uint16_t *)px_map, (uint16_t *)rotate_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif

    // Send rotated buffer to panel in portrait orientation
    lcd->lcd_draw_bitmap(0, 0, PANEL_WIDTH, PANEL_HEIGHT, (uint16_t *)rotate_buf);

    lv_display_flush_ready(disp_drv);
}

// Cleanup function to free all display resources
void display_deinit() {
    if (lcd) {
        delete lcd;
        lcd = NULL;
    }
    if (buf1) {
        heap_caps_free(buf1);
        buf1 = NULL;
    }
    if (buf2) {
        heap_caps_free(buf2);
        buf2 = NULL;
    }
    if (rotate_buf) {
        heap_caps_free(rotate_buf);
        rotate_buf = NULL;
    }
#if USE_PPA_ACCELERATION
    if (ppa_handle) {
        ppa_unregister_client(ppa_handle);
        ppa_handle = NULL;
    }
#endif
}
