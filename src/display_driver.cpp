#include "display_driver.h"
#include "config.h"
#include "screenshot.h"

// Defined in ui_globals.cpp, loaded from NVS in setup() before display_init().
// Declared here rather than pulling in ui_common.h, which would drag the whole
// UI/Sonos surface into the display layer.
extern int panel_variant;
#if SCREEN_SIZE == 7
#include "../lib/jd9165_lcd/jd9165_lcd.h"
typedef jd9165_lcd panel_lcd_t;
#else
#include "../lib/st7701_lcd/st7701_lcd.h"
typedef st7701_lcd panel_lcd_t;
#endif
#include <esp_heap_caps.h>
#include <esp_timer.h>        // esp_timer_get_time() for DISPLAY_PERF_TRACE
#include <esp_lcd_panel_ops.h>
#include <esp_private/esp_cache_private.h>   // esp_cache_get_alignment()
#include <esp_cache.h>                        // esp_cache_msync() - PPA coherency
#include <driver/ppa.h>

// Hardware rotation on the ESP32-P4's Pixel Processing Accelerator.
//
// This was 0 from the initial commit - "causes glitches" - and the glitches were
// real, but the cause is known and it is not the PPA. Two things were missing:
//
//   1. ALIGNMENT. PPA needs both buffers aligned to the cache line (64B).
//      heap_caps_malloc() promises no alignment at all. The old code aligned
//      out.buffer_SIZE and never the addresses, so the transfer was rejected or
//      landed skewed. (LVGL issue #9978 is this exact error.)
//
//   2. CACHE COHERENCY. LVGL renders through the CPU cache; PPA is a DMA
//      peripheral reading PSRAM directly and cannot see dirty cache lines. With
//      no esp_cache_msync() the accelerator rotated STALE pixels, which is the
//      tearing and blinking people report (LVGL issue #9046). esp_cache_private.h
//      was already included here and never used.
//
// Measured before fixing: the software transpose cost 17.7ms per flush - 110% of
// an entire 60fps frame budget - against 0.6ms to hand the result to the panel.
#define USE_PPA_ACCELERATION 1

static panel_lcd_t* lcd = NULL;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;
static lv_display_t *disp = NULL;
static bsp_lcd_handles_t lcd_handles;

#if SCREEN_SIZE != 7
// ============================================================================
// 4" ST7701 — portrait panel (480x800) driven from a landscape (800x480)
// LVGL framebuffer via a software 90° rotation in the flush callback.
// ============================================================================
static lv_color_t *rotate_buf = NULL;  // Rotation buffer

#if USE_PPA_ACCELERATION
static ppa_client_handle_t ppa_handle = NULL;
static size_t cache_line_size = 0;
#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))
#endif

#if USE_PPA_ACCELERATION
// Hardware-accelerated rotation using ESP32-P4 PPA
static void rotate_image_90_ppa(const uint16_t *src, uint16_t *dst, int width, int height) {
    // ZERO-INITIALISED, and this is not a style preference - it is the third and
    // final reason hardware rotation was abandoned here.
    //
    // ppa_srm_oper_config_t has fields this function never assigns: mirror_x,
    // mirror_y, alpha_update_mode, the alpha union, user_data, and yuv_range /
    // yuv_std inside BOTH the in and out block configs. Declared bare, every one
    // of them is whatever was on the stack. Several are enums, so the driver was
    // handed values outside their valid range and aborted inside its descriptor
    // setup (dma2d_desc_pixel_format_to_pbyte_value, via
    // ppa_srm_transaction_on_picked).
    //
    // Stack contents vary run to run, which is exactly why the original symptom
    // was "glitches" rather than a clean, reproducible failure.
    ppa_srm_oper_config_t oper_config = {};

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

    // Write the CPU's dirty cache lines back to PSRAM so the accelerator reads
    // the frame LVGL just drew rather than whatever was in memory before it.
    // This is the step whose absence produced the "glitches".
    const size_t bytes = ALIGN_UP(sizeof(uint16_t) * width * height, cache_line_size);
    esp_cache_msync((void *)src, bytes,
                    ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ppa_do_scale_rotate_mirror(ppa_handle, &oper_config);

    // And drop any cached view of the destination, so nothing the CPU still holds
    // for that buffer can be evicted over what the accelerator just wrote.
    esp_cache_msync((void *)dst, bytes,
                    ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
#endif

#if DISPLAY_PERF_TRACE
// Accumulates flush costs and prints one line every 5s. Called from the flush
// callback, so it must stay cheap: two adds and a compare on the common path.
//
// rotate_us  - the 800x480 -> 480x800 transpose, 384,000 pixels, CPU, PSRAM.
// xfer_us    - handing the 750KB portrait buffer to the panel over MIPI DSI.
//
// The split is the whole point. A 60fps budget is 16,667us per frame; the two
// numbers say how much of it this consumes and which half to attack.
static void displayPerfTrace(uint32_t rotate_us, uint32_t xfer_us, const char *path) {
    static uint32_t frames = 0, rot_total = 0, rot_worst = 0,
                    xfer_total = 0, xfer_worst = 0, window_ms = 0;
    if (window_ms == 0) window_ms = millis();

    frames++;
    rot_total  += rotate_us;  if (rotate_us > rot_worst)  rot_worst  = rotate_us;
    xfer_total += xfer_us;    if (xfer_us   > xfer_worst) xfer_worst = xfer_us;

    const uint32_t elapsed = millis() - window_ms;
    if (elapsed < 5000 || frames == 0) return;

    const uint32_t rot_avg  = rot_total  / frames;
    const uint32_t xfer_avg = xfer_total / frames;
    Serial.printf("[PERF/flush] path=%s | %u flushes/%ums = %.1f fps | rotate avg %uus worst %uus"
                  " | xfer avg %uus worst %uus | frame avg %uus (%.0f%% of a 60fps budget)\n",
                  path, frames, elapsed, frames * 1000.0f / elapsed,
                  rot_avg, rot_worst, xfer_avg, xfer_worst,
                  rot_avg + xfer_avg, (rot_avg + xfer_avg) / 166.67f);

    frames = rot_total = rot_worst = xfer_total = xfer_worst = 0;
    window_ms = millis();
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
    lcd = new panel_lcd_t(LCD_RST);
    if (!lcd) {
        Serial.println("[Display] ERROR: Failed to create LCD instance!");
        return false;
    }

    // Initialize the LCD. The 4" ST7701 has only ever shipped with one panel,
    // so there is no variant to select here.
    lcd->begin();
    lcd->get_handle(&lcd_handles);

    Serial.println("[Display] ST7701 LCD initialized successfully");

    // Allocate LVGL buffers in PSRAM - LANDSCAPE dimensions for LVGL (800x480)
    //
    // Cache-line ALIGNED when PPA is driving the rotation: the accelerator reads
    // and writes these directly and rejects unaligned addresses. Both the address
    // and the length have to be aligned, which is why the size is rounded up too.
    // Plain heap_caps_malloc() guarantees neither, and that was half of why
    // hardware rotation was abandoned here.
    const size_t fb_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t);
#if USE_PPA_ACCELERATION
    if (cache_line_size == 0) cache_line_size = 64;   // PPA init failed; stay sane
    const size_t fb_alloc = ALIGN_UP(fb_bytes, cache_line_size);
    buf1       = (lv_color_t *)heap_caps_aligned_alloc(cache_line_size, fb_alloc, MALLOC_CAP_SPIRAM);
    buf2       = (lv_color_t *)heap_caps_aligned_alloc(cache_line_size, fb_alloc, MALLOC_CAP_SPIRAM);
    rotate_buf = (lv_color_t *)heap_caps_aligned_alloc(cache_line_size, fb_alloc, MALLOC_CAP_SPIRAM);
#else
    buf1       = (lv_color_t *)heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    buf2       = (lv_color_t *)heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    rotate_buf = (lv_color_t *)heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
#endif

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
        // Release the framebuffers. The allocation-failure path above already
        // frees them; this path stranded 1.5MB (4") / 2.4MB (7") of PSRAM.
        if (buf1)       { heap_caps_free(buf1);       buf1 = NULL; }
        if (buf2)       { heap_caps_free(buf2);       buf2 = NULL; }
#if SCREEN_SIZE == 4
        if (rotate_buf) { heap_caps_free(rotate_buf); rotate_buf = NULL; }
#endif
        return false;
    }

    lv_display_set_flush_cb(disp, display_flush);
    lv_display_set_buffers(disp, buf1, buf2, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);

    // DON'T use lv_display_set_rotation - we do rotation manually in flush callback

    Serial.println("[Display] Ready! 800x480 landscape with manual 90° rotation to portrait panel");
    return true;
}

void display_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) {
    if (!lcd || !lcd_handles.panel || !rotate_buf) {
        lv_display_flush_ready(disp_drv);
        return;
    }

    // Grab the frame for a pending screenshot before it is rotated: px_map is
    // the landscape frame LVGL drew, rotate_buf is the portrait panel version
    // which would come out sideways. No-op unless one has been requested.
    screenshotCaptureHook(px_map, area);

    // Rotate the entire frame from landscape 800x480 to portrait 480x800 for panel
    // Panel DPI is now configured for 480×800 portrait
#if DISPLAY_PERF_TRACE
    const int64_t t_rot0 = esp_timer_get_time();
#endif
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

#if DISPLAY_PERF_TRACE
    const int64_t t_rot1 = esp_timer_get_time();
#endif

    // Send rotated buffer to panel in portrait orientation
    lcd->lcd_draw_bitmap(0, 0, PANEL_WIDTH, PANEL_HEIGHT, (uint16_t *)rotate_buf);

#if DISPLAY_PERF_TRACE
    displayPerfTrace((uint32_t)(t_rot1 - t_rot0),
                     (uint32_t)(esp_timer_get_time() - t_rot1),
#if USE_PPA_ACCELERATION
                     ppa_handle ? "PPA" : "SW(ppa-init-failed)"
#else
                     "SW(compiled-out)"
#endif
                     );
#endif

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

#else  // SCREEN_SIZE == 7
// ============================================================================
// 7" JD9165 — native 1024x600 LANDSCAPE panel. No rotation: LVGL renders at
// the panel's native orientation and the framebuffer is pushed straight to the
// panel. NOTE: code-complete port (CoopsInChina fork), not yet hardware-tested.
// ============================================================================
bool display_init(void) {
    Serial.printf("[Display] Initializing MIPI DSI interface for %s...\n", DISPLAY_MODEL);

    lcd = new panel_lcd_t(LCD_RST);
    if (!lcd) {
        Serial.println("[Display] ERROR: Failed to create LCD instance!");
        return false;
    }

    lcd->begin((uint8_t)panel_variant);
    lcd->get_handle(&lcd_handles);

    Serial.printf("[Display] %s LCD initialized successfully\n", DISPLAY_MODEL);

    // LVGL buffers in PSRAM — native landscape, no rotation buffer needed.
    size_t lvgl_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t);
    buf1 = (lv_color_t *)heap_caps_malloc(lvgl_size, MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t *)heap_caps_malloc(lvgl_size, MALLOC_CAP_SPIRAM);

    if (!buf1 || !buf2) {
        Serial.println("[Display] ERROR: Failed to allocate buffers!");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        return false;
    }

    Serial.printf("[Display] LVGL buffers: %zu bytes each (landscape %dx%d)\n",
                  lvgl_size, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Serial.printf("[Display] Free PSRAM: %zu bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!disp) {
        Serial.println("[Display] ERROR: Failed to create display");
        // Release the framebuffers. The allocation-failure path above already
        // frees them; this path stranded 1.5MB (4") / 2.4MB (7") of PSRAM.
        if (buf1)       { heap_caps_free(buf1);       buf1 = NULL; }
        if (buf2)       { heap_caps_free(buf2);       buf2 = NULL; }
#if SCREEN_SIZE == 4
        if (rotate_buf) { heap_caps_free(rotate_buf); rotate_buf = NULL; }
#endif
        return false;
    }

    lv_display_set_flush_cb(disp, display_flush);
    lv_display_set_buffers(disp, buf1, buf2, lvgl_size, LV_DISPLAY_RENDER_MODE_FULL);

    Serial.println("[Display] Ready! 1024x600 landscape, no rotation (direct flush)");
    return true;
}

void display_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) {
    if (!lcd || !lcd_handles.panel) {
        lv_display_flush_ready(disp_drv);
        return;
    }

    screenshotCaptureHook(px_map, area);

    // Native landscape: push the rendered region straight to the panel.
    lcd->lcd_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, (uint16_t *)px_map);

    lv_display_flush_ready(disp_drv);
}

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
}

#endif  // SCREEN_SIZE

void display_set_brightness(uint8_t brightness_percent) {
    if (lcd) {
        // Clamp brightness to 0-100%
        if (brightness_percent > 100) brightness_percent = 100;
        lcd->example_bsp_set_lcd_backlight(brightness_percent);
    }
}
