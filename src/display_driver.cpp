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
#include <esp_lcd_mipi_dsi.h>   // esp_lcd_dpi_panel_get_frame_buffer() - partial render
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
static lv_color_t *rotate_buf = NULL;  // Rotation buffer (full-frame mode only)
#if DISPLAY_PARTIAL_RENDER
// The DPI panel's own frame buffer - the memory the DSI controller scans out.
// Partial render rotates each changed area straight into it, so there is no
// intermediate portrait copy and no second full-frame transfer.
static uint16_t *panel_fb = NULL;
static size_t    panel_fb_bytes = 0;
#endif

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
// Per-frame cost, printed every 5s while something is animating.
//
// A frame is one LVGL refresh (LV_EVENT_REFR_START -> LV_EVENT_REFR_READY)
// that actually flushed pixels. Its time splits into:
//   push    - getting pixels onto the panel: the rotate (PPA or CPU) plus the
//             hand-off to the DPI frame buffer, summed over the frame's flushes
//   render  - everything else in the refresh: layout, LVGL software drawing
// and px/frame says how much of the screen each frame moved. Full-frame mode
// always moves 384,000; partial render moves only what changed - which is the
// number that decides how smooth scrolling and animations can be.
static uint32_t pt_push_us = 0, pt_px = 0;
static int64_t  pt_refr_t0 = 0;
static bool     pt_flushed = false;
static const char *pt_path = "";
static uint32_t pt_frames = 0, pt_frame_sum = 0, pt_frame_worst = 0,
                pt_push_sum = 0, pt_px_sum = 0, pt_window_ms = 0;

static inline void perfFlush(uint32_t push_us, uint32_t px, const char *path) {
    pt_push_us += push_us;
    pt_px += px;
    pt_flushed = true;
    pt_path = path;
}

static void perfRefrEvent(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        pt_refr_t0 = esp_timer_get_time();
        pt_push_us = pt_px = 0;
        pt_flushed = false;
        return;
    }
    if (pt_window_ms == 0) pt_window_ms = millis();
    if (pt_flushed) {
        const uint32_t t = (uint32_t)(esp_timer_get_time() - pt_refr_t0);
        pt_frames++;
        pt_frame_sum += t;
        if (t > pt_frame_worst) pt_frame_worst = t;
        pt_push_sum += pt_push_us;
        pt_px_sum += pt_px;
    }
    const uint32_t elapsed = millis() - pt_window_ms;
    if (elapsed < 5000) return;
    if (pt_frames) {
        const float frame = pt_frame_sum / 1000.0f / pt_frames;
        const float push  = pt_push_sum  / 1000.0f / pt_frames;
        const uint32_t px = pt_px_sum / pt_frames;
        Serial.printf("[PERF/frame] path=%s | %u frames/%ums = %.1f fps | frame avg %.1fms worst %.1fms"
                      " | render %.1fms | push %.1fms | %u px/frame (%.0f%% of screen)\n",
                      pt_path, pt_frames, elapsed, pt_frames * 1000.0f / elapsed, frame,
                      pt_frame_worst / 1000.0f, frame - push, push, px,
                      px * 100.0f / (DISPLAY_WIDTH * DISPLAY_HEIGHT));
    }
    pt_frames = pt_frame_sum = pt_frame_worst = pt_push_sum = pt_px_sum = 0;
    pt_window_ms = millis();
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

#if DISPLAY_PARTIAL_RENDER
// Write back (C2M) the cache lines covering [p, p+len). The region need not be
// cache-line aligned: it is widened to whole lines, and writing back a
// neighbour's clean or dirty line is harmless.
static void cacheWriteback(const void *p, size_t len) {
    const uintptr_t a = (uintptr_t)p & ~(uintptr_t)(cache_line_size - 1);
    const uintptr_t b = ALIGN_UP((uintptr_t)p + len, cache_line_size);
    esp_cache_msync((void *)a, b - a, ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

// Rotates one changed area of the landscape LVGL buffer into its place in the
// portrait panel frame buffer. Same mapping as the full-frame path - 90deg
// clockwise, landscape (x, y) -> portrait (H-1-y, x) - applied to a block:
// rows y1..y2 / columns x1..x2 land at portrait columns H-1-y2..H-1-y1 and
// rows x1..x2.
//
// UI_PORTRAIT: LVGL already draws at the panel's 480x800, so the same call is a
// straight block copy - identical offsets, no rotation. It still goes through
// a separate LVGL buffer rather than letting LVGL draw into the scanned-out
// frame buffer: the DSI controller reads that memory continuously, and a half-
// drawn area (background painted, text not yet) would reach the glass.
static bool rotate_area_to_panel(const uint16_t *src, const lv_area_t *a, bool *used_ppa) {
    const int w = lv_area_get_width(a);
    const int h = lv_area_get_height(a);

    // LVGL drew these rows through the CPU cache; the PPA reads PSRAM directly.
    cacheWriteback(src + (size_t)a->y1 * DISPLAY_WIDTH, (size_t)h * DISPLAY_WIDTH * sizeof(uint16_t));

#if UI_PORTRAIT
#if USE_PPA_ACCELERATION
    if (ppa_handle) {
        ppa_srm_oper_config_t op = {};
        op.in.buffer = (void *)src;
        op.in.pic_w = DISPLAY_WIDTH;
        op.in.pic_h = DISPLAY_HEIGHT;
        op.in.block_w = w;
        op.in.block_h = h;
        op.in.block_offset_x = a->x1;
        op.in.block_offset_y = a->y1;
        op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        op.out.buffer = panel_fb;
        op.out.buffer_size = panel_fb_bytes;
        op.out.pic_w = PANEL_WIDTH;
        op.out.pic_h = PANEL_HEIGHT;
        op.out.block_offset_x = a->x1;
        op.out.block_offset_y = a->y1;
        op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        op.scale_x = 1.0;
        op.scale_y = 1.0;
        op.mode = PPA_TRANS_MODE_BLOCKING;
        if (ppa_do_scale_rotate_mirror(ppa_handle, &op) == ESP_OK) {
            *used_ppa = true;
            return true;
        }
        static bool warned = false;
        if (!warned) {
            warned = true;
            Serial.printf("[Display] PPA area copy failed (%d,%d %dx%d) - CPU fallback\n",
                          (int)a->x1, (int)a->y1, w, h);
        }
    }
#endif
    for (int y = a->y1; y <= a->y2; y++) {
        memcpy(panel_fb + (size_t)y * PANEL_WIDTH + a->x1,
               src + (size_t)y * DISPLAY_WIDTH + a->x1, (size_t)w * sizeof(uint16_t));
    }
    cacheWriteback(panel_fb + (size_t)a->y1 * PANEL_WIDTH, (size_t)h * PANEL_WIDTH * sizeof(uint16_t));
    *used_ppa = false;
    return true;
#else

#if USE_PPA_ACCELERATION
    if (ppa_handle) {
        ppa_srm_oper_config_t op = {};   // zeroed: see rotate_image_90_ppa()
        op.in.buffer = (void *)src;
        op.in.pic_w = DISPLAY_WIDTH;
        op.in.pic_h = DISPLAY_HEIGHT;
        op.in.block_w = w;
        op.in.block_h = h;
        op.in.block_offset_x = a->x1;
        op.in.block_offset_y = a->y1;
        op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        op.out.buffer = panel_fb;
        op.out.buffer_size = panel_fb_bytes;
        op.out.pic_w = PANEL_WIDTH;
        op.out.pic_h = PANEL_HEIGHT;
        op.out.block_offset_x = DISPLAY_HEIGHT - 1 - a->y2;
        op.out.block_offset_y = a->x1;
        op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        op.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;   // 270 CCW == 90 CW, as full-frame
        op.scale_x = 1.0;
        op.scale_y = 1.0;
        op.mode = PPA_TRANS_MODE_BLOCKING;
        if (ppa_do_scale_rotate_mirror(ppa_handle, &op) == ESP_OK) {
            *used_ppa = true;
            return true;
        }
        static bool warned = false;
        if (!warned) {
            warned = true;
            Serial.printf("[Display] PPA area rotate failed (%d,%d %dx%d) - CPU fallback\n",
                          (int)a->x1, (int)a->y1, w, h);
        }
    }
#endif
    // CPU fallback: same mapping, then push the touched panel rows out of the
    // cache so the DSI controller (a DMA reader) sees them.
    for (int y = a->y1; y <= a->y2; y++) {
        const uint16_t *s = src + (size_t)y * DISPLAY_WIDTH;
        const int col = DISPLAY_HEIGHT - 1 - y;
        for (int x = a->x1; x <= a->x2; x++) panel_fb[(size_t)x * PANEL_WIDTH + col] = s[x];
    }
    cacheWriteback(panel_fb + (size_t)a->x1 * PANEL_WIDTH, (size_t)w * PANEL_WIDTH * sizeof(uint16_t));
    *used_ppa = false;
    return true;
#endif  // UI_PORTRAIT
}
#endif

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
#if DISPLAY_PARTIAL_RENDER
    // One landscape buffer that LVGL keeps whole (DIRECT mode), and the panel's
    // own frame buffer as the destination. No second LVGL buffer and no
    // portrait staging copy: 1.5MB of PSRAM less, and nothing to keep in sync.
    if (cache_line_size == 0) cache_line_size = 64;
    if (esp_lcd_dpi_panel_get_frame_buffer(lcd_handles.panel, 1, (void **)&panel_fb) != ESP_OK || !panel_fb) {
        Serial.println("[Display] ERROR: no DPI frame buffer for partial render");
        return false;
    }
    panel_fb_bytes = ALIGN_UP((size_t)PANEL_WIDTH * PANEL_HEIGHT * sizeof(uint16_t), cache_line_size);
    // Nothing on the CPU side may later evict a stale line over what the PPA
    // writes, so flush and drop whatever the driver left cached at init.
    esp_cache_msync(panel_fb, panel_fb_bytes, ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    buf1 = (lv_color_t *)heap_caps_aligned_alloc(cache_line_size, ALIGN_UP(fb_bytes, cache_line_size),
                                                 MALLOC_CAP_SPIRAM);
    if (!buf1) {
        Serial.println("[Display] ERROR: Failed to allocate the LVGL buffer!");
        return false;
    }
#elif USE_PPA_ACCELERATION
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

#if !DISPLAY_PARTIAL_RENDER
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
#else
    Serial.printf("[Display] Partial render: 1 LVGL buffer (%u bytes), %s changed areas into the panel FB\n",
                  (unsigned)fb_bytes, UI_PORTRAIT ? "copying" : "rotating");
#endif
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
#if DISPLAY_PARTIAL_RENDER
    // DIRECT: LVGL redraws only invalidated areas, in place in a screen-sized
    // buffer, so the buffer always holds the whole current frame (which is what
    // the screenshot hook copies). Single-buffered on purpose - the flush is
    // synchronous, and a second buffer would only add LVGL's area sync copies.
    lv_display_set_buffers(disp, buf1, NULL, fb_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    lv_display_set_buffers(disp, buf1, buf2, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);
#endif
#if DISPLAY_PERF_TRACE
    lv_display_add_event_cb(disp, perfRefrEvent, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, perfRefrEvent, LV_EVENT_REFR_READY, NULL);
#endif

    // DON'T use lv_display_set_rotation - we do rotation manually in flush callback

#if UI_PORTRAIT
    Serial.println("[Display] Ready! 480x800 portrait, no rotation");
#else
    Serial.println("[Display] Ready! 800x480 landscape with manual 90° rotation to portrait panel");
#endif
    return true;
}

#if DISPLAY_PARTIAL_RENDER
void display_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) {
    if (!lcd || !panel_fb) {
        lv_display_flush_ready(disp_drv);
        return;
    }
#if DISPLAY_PERF_TRACE
    const int64_t t0 = esp_timer_get_time();
#endif
    // In DIRECT mode px_map is the start of the whole buffer, not the area.
    bool used_ppa = false;
    rotate_area_to_panel((const uint16_t *)px_map, area, &used_ppa);
#if DISPLAY_PERF_TRACE
    perfFlush((uint32_t)(esp_timer_get_time() - t0), (uint32_t)lv_area_get_size(area),
              used_ppa ? "partial/PPA" : "partial/CPU");
#endif

    // The buffer holds the complete frame once the last area of a refresh is
    // drawn, so a pending screenshot takes all of it.
    if (lv_display_flush_is_last(disp_drv)) {
        const lv_area_t full = {0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1};
        screenshotCaptureHook(px_map, &full);
    }
    lv_display_flush_ready(disp_drv);
}
#else
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
    perfFlush((uint32_t)(esp_timer_get_time() - t_rot0), DISPLAY_WIDTH * DISPLAY_HEIGHT,
#if USE_PPA_ACCELERATION
              ppa_handle ? "full/PPA" : "full/SW(ppa-init-failed)"
#else
              "full/SW(compiled-out)"
#endif
              );
#endif

    lv_display_flush_ready(disp_drv);
}
#endif  // DISPLAY_PARTIAL_RENDER

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
