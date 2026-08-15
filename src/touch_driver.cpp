#include "touch_driver.h"
#include <Wire.h>
#include <TAMC_GT911.h>

// External callback for screen wake
extern void resetScreenTimeout();

// ============================================================================
// Why this driver samples on its own task
// ============================================================================
// touch_read() used to call ts.read() directly, so the panel was only sampled
// when LVGL got round to asking — and LVGL only asks from inside
// lv_timer_handler(), on mainAppTask, in the same loop that renders. A tap that
// began and ended between two of those calls was never seen at all.
//
// That is invisible while frames are quick and obvious when they are not, which
// is exactly the pattern reported: the Immersive theme (animated full-screen
// lyric stage over a saturated backdrop) needed repeated taps on its transport
// buttons, while the lighter themes were fine. Enlarging the buttons changed
// nothing, because the touch was being dropped rather than mis-aimed.
//
// So the panel is now polled by a small task on core 0, independent of whatever
// core 1 is drawing, and a press is LATCHED. If a whole tap happens between two
// LVGL polls, the next poll still reports one press, and the one after that
// reports the release — LVGL sees a complete click either way.
// ============================================================================

#define TOUCH_SAMPLE_MS     10      // panel poll interval
#define TOUCH_TASK_STACK    3072
#define TOUCH_TASK_PRIORITY 4       // above Sonos polling (3); sleeps between samples
#define TOUCH_TASK_CORE     0       // mainAppTask renders on core 1

#if SCREEN_SIZE != 7
// ============================================================================
// 4" GT911 — portrait sensor (480×800) mapped to landscape (800×480) via a
// manual 90° rotation that matches the ST7701 display rotation.
// ============================================================================
#define TOUCH_MAP_X1 480
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 800
#define TOUCH_MAP_Y2 0

TAMC_GT911 ts = TAMC_GT911(TOUCH_GT911_SDA, TOUCH_GT911_SCL, TOUCH_GT911_INT, TOUCH_GT911_RST,
                           max(TOUCH_MAP_X1, TOUCH_MAP_X2),
                           max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

static void touch_begin_hw(void) {
    Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);  // Normal orientation - LVGL handles rotation
}

// Reads the panel once. Returns true and fills x/y (LVGL landscape coords) when
// a finger is down.
static bool touch_sample(int32_t* x, int32_t* y) {
    ts.read();
    if (!ts.isTouched) return false;

    // Touch sensor reports physical coordinates in portrait orientation.
    // Physical panel: 480x800 portrait -> LVGL sees: 800x480 landscape.
    int16_t touch_x = map(ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, 479);
    int16_t touch_y = map(ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, 799);

    if (touch_x < 0) touch_x = 0;
    if (touch_x > 479) touch_x = 479;
    if (touch_y < 0) touch_y = 0;
    if (touch_y > 799) touch_y = 799;

    // 90° rotation to match the display: portrait (x, y) -> landscape (y, 479 - x)
    *x = touch_y;
    *y = 479 - touch_x;
    return true;
}

#else  // SCREEN_SIZE == 7
// ============================================================================
// 7" GT911 — native landscape sensor (1024×600). No rotation: raw coordinates
// map directly to LVGL's landscape framebuffer.
// ============================================================================
TAMC_GT911 ts = TAMC_GT911(TOUCH_GT911_SDA, TOUCH_GT911_SCL, TOUCH_GT911_INT, TOUCH_GT911_RST,
                           TOUCH_PANEL_WIDTH, TOUCH_PANEL_HEIGHT);

static void touch_begin_hw(void) {
    Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
    ts.begin();
    ts.setRotation(ROTATION_INVERTED);
}

static bool touch_sample(int32_t* x, int32_t* y) {
    ts.read();
    if (!ts.isTouched || ts.touches <= 0) return false;

    int16_t rx = map(ts.points[0].x, 0, TOUCH_PANEL_WIDTH - 1, 0, DISPLAY_WIDTH - 1);
    int16_t ry = map(ts.points[0].y, 0, TOUCH_PANEL_HEIGHT - 1, 0, DISPLAY_HEIGHT - 1);
    *x = constrain(rx, 0, DISPLAY_WIDTH - 1);
    *y = constrain(ry, 0, DISPLAY_HEIGHT - 1);
    return true;
}

#endif  // SCREEN_SIZE

// ── Shared state: written only by the sampler, read only by touch_read ──────
// Single writer and single reader of word-sized values, so no lock is needed.
// t_pending is cleared by the reader, which is the only race that matters: the
// worst case is a duplicated press, which LVGL collapses, rather than a lost one.
static volatile int32_t t_x = 0, t_y = 0;
static volatile bool    t_down    = false;   // finger is down right now
static volatile bool    t_pending = false;   // a press happened since LVGL last looked

static lv_indev_t* indev = NULL;

static void touchSamplerTask(void*) {
    for (;;) {
        int32_t x, y;
        if (touch_sample(&x, &y)) {
            t_x = x;
            t_y = y;
            if (!t_down) {
                t_down = true;
                resetScreenTimeout();   // wake the screen on the leading edge only
            }
            t_pending = true;
        } else {
            t_down = false;
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_SAMPLE_MS));
    }
}

void touch_read(lv_indev_t *indev_drv, lv_indev_data_t *data) {
    // t_pending covers the case this whole driver exists for: the tap started
    // AND finished since the last poll. Report it as a press now; with the finger
    // already up, the next poll reports the release and the click completes.
    if (t_down || t_pending) {
        data->point.x = t_x;
        data->point.y = t_y;
        data->state   = LV_INDEV_STATE_PRESSED;
        t_pending = false;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool touch_init(void) {
    Serial.printf("[Touch] Initializing GT911 for %s...\n", DISPLAY_MODEL);

    touch_begin_hw();
    Serial.println("[Touch] GT911 initialized!");

    indev = lv_indev_create();
    if (!indev) {
        Serial.println("[Touch] ERROR: Failed to create input device");
        return false;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);

    BaseType_t ok = xTaskCreatePinnedToCore(touchSamplerTask, "Touch", TOUCH_TASK_STACK, NULL,
                                            TOUCH_TASK_PRIORITY, NULL, TOUCH_TASK_CORE);
    if (ok != pdPASS) {
        // Not fatal: without the sampler nothing ever sets t_down, so say so
        // loudly rather than leaving a dead touchscreen looking like a hang.
        Serial.println("[Touch] ERROR: sampler task failed to start — touch will not work");
        return false;
    }
    Serial.printf("[Touch] Sampler task on core %d, every %dms\n",
                  TOUCH_TASK_CORE, TOUCH_SAMPLE_MS);
    return true;
}
