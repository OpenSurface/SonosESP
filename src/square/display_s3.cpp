/**
 * display_s3.cpp — GUITION ESP32-4848S040 (ESP32-S3 + ST7701S, 480x480 RGB)
 *
 * Square build only (UI_SQUARE). Two buses:
 *   - a 3-wire 9-bit SPI (CS 39 / SCK 48 / MOSI 47), bit-banged, used ONCE at
 *     boot to send the ST7701S its init sequence;
 *   - the 16-bit RGB565 parallel bus that streams pixels from then on, driven by
 *     the S3's LCD_CAM peripheral through esp_lcd's RGB panel driver.
 *
 * Rendering: LVGL draws in bands into a small buffer that stays in cache
 * (LV_DISPLAY_RENDER_MODE_PARTIAL); each band is copied into the hidden one of
 * two PSRAM frame buffers. The last band of a frame swaps the scan-out buffer,
 * waits for VSYNC, then copies the frame's dirty areas into the other buffer so
 * both hold the same picture again.
 *
 * Why not LVGL's DIRECT mode on the frame buffers: blending straight into PSRAM
 * means read-modify-write traffic all over the frame, competing with the CPU
 * refilling the bounce buffers from that same PSRAM. On the bench that showed
 * as small glitches during every redraw. Rendering into a cache-sized band
 * leaves PSRAM mostly sequential row copies.
 *
 * Bounce buffers: the DMA reads two small internal-RAM buffers that the CPU
 * refills from the PSRAM frame buffer. Scanning PSRAM directly glitches as soon
 * as Wi-Fi and TLS compete for the PSRAM bus, and it also makes the CPU copy
 * cache-coherent by construction (no explicit write-back needed).
 */
#include "display_driver.h"

#if UI_SQUARE

#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include <freertos/semphr.h>
#include "src/display/lv_display_private.h"   // inv_areas: what the next frame redraws
#include "src/misc/lv_area_private.h"         // lv_area_diff

static lv_display_t*          disp     = nullptr;
static esp_lcd_panel_handle_t s_panel  = nullptr;
static SemaphoreHandle_t      s_vsync  = nullptr;
static void*                  s_fb[2]  = {nullptr, nullptr};
static const void*            s_front  = nullptr;   // the buffer being scanned out
static int                    s_back   = 1;         // index of the hidden buffer
#define MAX_DIRTY          24
static lv_area_t              s_dirty[MAX_DIRTY];
static int                    s_dirty_n = 0;
static bool                   s_dirty_all = false;
// Areas the hidden buffer still lacks (the last frame's dirty areas), copied in
// at the start of the next frame minus whatever that frame redraws anyway.
static lv_area_t              s_stale[MAX_DIRTY];
static int                    s_stale_n = 0;
static bool                   s_stale_all = false;
static bool                   s_swap_pending = false;   // a buffer switch waits for VSYNC

#define LCD_PCLK_HZ        (12 * 1000 * 1000)   // ~42 fps at 548x518 total
#define LCD_BOUNCE_LINES   10
#define LCD_BAND_LINES     32      // LVGL render band, internal RAM
#define BACKLIGHT_PWM_HZ   1000    // the backlight driver stays dark at 20 kHz (bench)
#define BACKLIGHT_PWM_BITS 10

// ── ST7701S init over 3-wire SPI ────────────────────────────────────────────
// Each 9-bit word is D/C (0 = command, 1 = parameter) then 8 data bits, MSB
// first, sampled by the panel on SCK's rising edge. The panel tolerates a slow
// clock, so a microsecond per half period is plenty and keeps timing obvious.

static inline void spi_bit(bool b) {
    gpio_set_level((gpio_num_t)LCD_SPI_MOSI, b);
    gpio_set_level((gpio_num_t)LCD_SPI_SCK, 0);
    ets_delay_us(1);
    gpio_set_level((gpio_num_t)LCD_SPI_SCK, 1);
    ets_delay_us(1);
}

static void spi_word(bool dc, uint8_t v) {
    spi_bit(dc);
    for (int i = 7; i >= 0; --i) spi_bit((v >> i) & 1);
}

static void st_cmd(uint8_t cmd, const uint8_t* data, uint8_t n) {
    gpio_set_level((gpio_num_t)LCD_SPI_CS, 0);
    spi_word(false, cmd);
    for (uint8_t i = 0; i < n; ++i) spi_word(true, data[i]);
    gpio_set_level((gpio_num_t)LCD_SPI_CS, 1);
    ets_delay_us(2);
}

struct St7701Cmd {
    uint8_t  cmd;
    uint8_t  len;
    uint8_t  data[16];
    uint16_t delay_ms;
};

// Arduino_GFX st7701_type9_init_operations, transcribed one command per line.
static const St7701Cmd kInit[] = {
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x10}, 0},
    {0xC0, 2,  {0x3B, 0x00}, 0},
    {0xC1, 2,  {0x0D, 0x02}, 0},
    {0xC2, 2,  {0x31, 0x05}, 0},
    {0xCD, 1,  {0x00}, 0},
    {0xB0, 16, {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
                0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 0},
    {0xB1, 16, {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
                0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 0},
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x11}, 0},
    {0xB0, 1,  {0x60}, 0},
    {0xB1, 1,  {0x32}, 0},
    {0xB2, 1,  {0x07}, 0},
    {0xB3, 1,  {0x80}, 0},
    {0xB5, 1,  {0x49}, 0},
    {0xB7, 1,  {0x85}, 0},
    {0xB8, 1,  {0x21}, 0},
    {0xC1, 1,  {0x78}, 0},
    {0xC2, 1,  {0x78}, 0},
    {0xE0, 3,  {0x00, 0x1B, 0x02}, 0},
    {0xE1, 11, {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 0},
    {0xE2, 12, {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 0},
    {0xE3, 4,  {0x00, 0x00, 0x11, 0x11}, 0},
    {0xE4, 2,  {0x44, 0x44}, 0},
    {0xE5, 16, {0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
                0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 0},
    {0xE6, 4,  {0x00, 0x00, 0x11, 0x11}, 0},
    {0xE7, 2,  {0x44, 0x44}, 0},
    {0xE8, 16, {0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
                0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 0},
    {0xEB, 7,  {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 0},
    {0xEC, 2,  {0x3C, 0x00}, 0},
    {0xED, 16, {0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 0},
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x13}, 0},
    {0xE5, 1,  {0xE4}, 0},
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x00}, 0},
    {0x3A, 1,  {0x60}, 0},          // 18-bit interface; RGB565 sits on the top bits
    {0x11, 0,  {0}, 120},           // sleep out
    {0x29, 0,  {0}, 20},            // display on
};

static void st7701_init(void) {
    const gpio_num_t pins[] = {(gpio_num_t)LCD_SPI_CS, (gpio_num_t)LCD_SPI_SCK,
                               (gpio_num_t)LCD_SPI_MOSI};
    for (gpio_num_t p : pins) {
        gpio_reset_pin(p);
        gpio_set_direction(p, GPIO_MODE_OUTPUT);
        gpio_set_level(p, 1);
    }
    delay(10);
    for (const St7701Cmd& c : kInit) {
        st_cmd(c.cmd, c.data, c.len);
        if (c.delay_ms) delay(c.delay_ms);
    }
}

// ── Backlight ───────────────────────────────────────────────────────────────

// Bench: re-attach the backlight PWM at another frequency.
void display_backlight_freq(uint32_t hz) {
    ledcDetach(LCD_BACKLIGHT);
    bool ok = ledcAttach(LCD_BACKLIGHT, hz, BACKLIGHT_PWM_BITS);
    Serial.printf("[DISPLAY] backlight PWM %u Hz attach=%d\n", (unsigned)hz, ok);
}

void display_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    const uint32_t max = (1u << BACKLIGHT_PWM_BITS) - 1;
    ledcWrite(LCD_BACKLIGHT, (max * pct) / 100);
}

// ── LVGL glue ───────────────────────────────────────────────────────────────

static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*,
                               void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_vsync, &woken);
    return woken == pdTRUE;
}

static inline void copyArea(uint16_t* dst, const uint16_t* src, int src_stride, const lv_area_t* a) {
    const int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; ++y)
        memcpy(dst + y * DISPLAY_WIDTH + a->x1, src + (y - a->y1) * src_stride, w * 2);
}

static inline void syncArea(uint16_t* dst, const uint16_t* src, const lv_area_t* a) {
    const int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; ++y)
        memcpy(dst + y * DISPLAY_WIDTH + a->x1, src + y * DISPLAY_WIDTH + a->x1, w * 2);
}

// Bench timing (UI_DEBUG_TAP builds): per second, how long frames spend in
// LVGL rendering, band copies, the VSYNC wait and the buffer sync.
#if UI_DEBUG_TAP
static uint32_t s_t_copy, s_t_vsync, s_t_sync, s_t_render, s_frames, s_t_start, s_t_window;
static void perfRefr(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) { s_t_start = micros(); return; }
    s_t_render += micros() - s_t_start;   // whole refresh, flushes included
    s_frames++;
    uint32_t now = millis();
    if (now - s_t_window >= 1000) {
        if (s_frames > 1)
            Serial.printf("[PERF] %u fps | per frame: refresh %u us = copy %u + vsync %u + sync %u + render %u\n",
                          (unsigned)s_frames, (unsigned)(s_t_render / s_frames),
                          (unsigned)(s_t_copy / s_frames), (unsigned)(s_t_vsync / s_frames),
                          (unsigned)(s_t_sync / s_frames),
                          (unsigned)((s_t_render - s_t_copy - s_t_vsync - s_t_sync) / s_frames));
        s_t_copy = s_t_vsync = s_t_sync = s_t_render = s_frames = 0;
        s_t_window = now;
    }
}
#define PERF_T0() uint32_t _t0 = micros()
#define PERF_ADD(acc) do { acc += micros() - _t0; _t0 = micros(); } while (0)
#else
#define PERF_T0() do {} while (0)
#define PERF_ADD(acc) do {} while (0)
#endif

// Start of a frame: copy into the hidden buffer the parts of the last frame it
// lacks, except those this frame is about to redraw.
static void syncStale(lv_event_t* e) {
    PERF_T0();
    lv_display_t* d = (lv_display_t*)lv_event_get_target(e);
    // The buffer this frame draws into is still on the glass until the switch
    // asked for by the last frame takes effect.
    if (s_swap_pending) {
        xSemaphoreTake(s_vsync, pdMS_TO_TICKS(50));
        s_swap_pending = false;
        PERF_ADD(s_t_vsync);
    }
    uint16_t* dst = (uint16_t*)s_fb[s_back];
    const uint16_t* src = (const uint16_t*)s_fb[s_back ^ 1];
    if (s_stale_all) {
        s_stale_all = false;
        s_stale[0] = {0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1};
        s_stale_n = 1;
    }
    // Work list: each stale area minus every area about to be redrawn.
    static lv_area_t work[64];
    for (int i = 0; i < s_stale_n; ++i) {
        int n = 1;
        work[0] = s_stale[i];
        for (uint32_t k = 0; k < d->inv_p && n > 0; ++k) {
            const lv_area_t& inv = d->inv_areas[k];
            int out = 0;
            static lv_area_t next[64];
            for (int j = 0; j < n; ++j) {
                lv_area_t parts[4];
                int8_t c = lv_area_diff(parts, &work[j], &inv);
                if (c < 0) {                       // no overlap: keep as is
                    if (out < 64) next[out++] = work[j];
                } else {
                    for (int p = 0; p < c && out < 64; ++p) next[out++] = parts[p];
                }
            }
            memcpy(work, next, out * sizeof(lv_area_t));
            n = out;
        }
        for (int j = 0; j < n; ++j) syncArea(dst, src, &work[j]);
    }
    s_stale_n = 0;
    PERF_ADD(s_t_sync);
}

void display_flush(lv_display_t* d, const lv_area_t* area, uint8_t* px_map) {
    PERF_T0();
    uint16_t* back = (uint16_t*)s_fb[s_back];
    copyArea(back, (const uint16_t*)px_map, area->x2 - area->x1 + 1, area);
    if (s_dirty_n < MAX_DIRTY) s_dirty[s_dirty_n++] = *area;
    else s_dirty_all = true;
    PERF_ADD(s_t_copy);

    if (lv_display_flush_is_last(d)) {
        // Show the finished frame at the next VSYNC. Not waited for here: the
        // loop gets on with its other work meanwhile, and the next frame waits
        // (syncStale) only if the switch has not happened by then.
        xSemaphoreTake(s_vsync, 0);
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, back);
        s_front = back;
        s_swap_pending = true;
        // ...and remember what the other buffer now lacks. It is brought up to
        // date when the next frame starts (syncStale), not here: by then we
        // know what that frame redraws, and a scroll redraws all of it.
        s_stale_all = s_dirty_all;
        s_stale_n = s_dirty_all ? 0 : s_dirty_n;
        if (!s_dirty_all) memcpy(s_stale, s_dirty, s_dirty_n * sizeof(lv_area_t));
        s_dirty_n = 0;
        s_dirty_all = false;
        s_back ^= 1;
    }
    lv_display_flush_ready(d);
}

bool display_init(void) {
    Serial.printf("[DISPLAY] %s — RGB %u MHz\n", DISPLAY_MODEL, LCD_PCLK_HZ / 1000000);

    // Backlight off while the panel wakes, so boot garbage never shows.
    bool bl_ok = ledcAttach(LCD_BACKLIGHT, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
    bool bl_w  = ledcWrite(LCD_BACKLIGHT, 0);
    Serial.printf("[DISPLAY] backlight PWM GPIO%d attach=%d write=%d\n", LCD_BACKLIGHT, bl_ok, bl_w);

    st7701_init();

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src                    = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz            = LCD_PCLK_HZ;
    cfg.timings.h_res              = DISPLAY_WIDTH;
    cfg.timings.v_res              = DISPLAY_HEIGHT;
    cfg.timings.hsync_pulse_width  = 8;
    cfg.timings.hsync_back_porch   = 50;
    cfg.timings.hsync_front_porch  = 10;
    cfg.timings.vsync_pulse_width  = 8;
    cfg.timings.vsync_back_porch   = 20;
    cfg.timings.vsync_front_porch  = 10;
    cfg.timings.flags.hsync_idle_low = 0;
    cfg.timings.flags.vsync_idle_low = 0;
    cfg.timings.flags.de_idle_high   = 0;
    cfg.timings.flags.pclk_active_neg = 0;
    cfg.timings.flags.pclk_idle_high  = 0;
    cfg.data_width            = 16;
    cfg.bits_per_pixel        = 16;
    cfg.num_fbs               = 2;
    cfg.bounce_buffer_size_px = DISPLAY_WIDTH * LCD_BOUNCE_LINES;
    cfg.dma_burst_size        = 64;
    cfg.hsync_gpio_num        = LCD_PIN_HSYNC;
    cfg.vsync_gpio_num        = LCD_PIN_VSYNC;
    cfg.de_gpio_num           = LCD_PIN_DE;
    cfg.pclk_gpio_num         = LCD_PIN_PCLK;
    cfg.disp_gpio_num         = -1;
    // D0..D15 = B0..B4, G0..G5, R0..R4 (RGB565, little-endian as LVGL draws it)
    const int data_pins[16] = {4, 5, 6, 7, 15, 8, 20, 3, 46, 9, 10, 11, 12, 13, 14, 0};
    for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = data_pins[i];
    cfg.flags.fb_in_psram = 1;

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) {
        Serial.printf("[DISPLAY] esp_lcd_new_rgb_panel failed: %s\n", esp_err_to_name(err));
        return false;
    }

    s_vsync = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = on_vsync;
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, nullptr);

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &s_fb[0], &s_fb[1]) != ESP_OK) {
        Serial.println("[DISPLAY] could not get the frame buffers");
        return false;
    }
    const size_t fb_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    memset(s_fb[0], 0, fb_bytes);
    memset(s_fb[1], 0, fb_bytes);

    disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!disp) return false;
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, display_flush);
#if UI_DEBUG_TAP
    lv_display_add_event_cb(disp, perfRefr, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(disp, perfRefr, LV_EVENT_REFR_READY, nullptr);
#endif
    // After the bench timer's REFR_START, so the sync counts inside the frame.
    lv_display_add_event_cb(disp, syncStale, LV_EVENT_REFR_START, nullptr);
    const size_t band_bytes = DISPLAY_WIDTH * LCD_BAND_LINES * sizeof(uint16_t);
    // Internal RAM (30KB): blending into it and then reading it for the copy
    // into the PSRAM frame buffer is ~2x faster than from a PSRAM band. Falls
    // back to PSRAM if internal RAM is short.
    void* band = heap_caps_aligned_alloc(64, band_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!band) band = heap_caps_aligned_alloc(64, band_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!band) { Serial.println("[DISPLAY] no memory for the render band"); return false; }
    lv_display_set_buffers(disp, band, nullptr, band_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    s_front = s_fb[0];
    s_back  = 1;

    Serial.printf("[DISPLAY] ready: 2 x %u KB frame buffers in PSRAM, %u-line band, "
                  "%u-line bounce buffers\n", (unsigned)(fb_bytes / 1024), LCD_BAND_LINES,
                  LCD_BOUNCE_LINES);
    return true;
}

// Bench screenshots: the frame on the glass, not the one LVGL is drawing.
const void* display_front_buffer(void) { return s_front ? s_front : s_fb[0]; }

void display_deinit(void) {
    if (s_panel) esp_lcd_panel_del(s_panel);
    s_panel = nullptr;
}

#endif  // UI_SQUARE
