#include "touch_driver.h"
#include <Wire.h>
#include <TAMC_GT911.h>

// Touch sensor is in portrait orientation (480×800)
#define TOUCH_MAP_X1 480
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 800
#define TOUCH_MAP_Y2 0

TAMC_GT911 ts = TAMC_GT911(TOUCH_GT911_SDA, TOUCH_GT911_SCL, TOUCH_GT911_INT, TOUCH_GT911_RST,
                           max(TOUCH_MAP_X1, TOUCH_MAP_X2),
                           max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

static lv_indev_t *indev = NULL;

bool touch_init(void) {
    Serial.println("[Touch] Initializing GT911...");

    Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);  // Normal orientation - LVGL handles rotation

    Serial.println("[Touch] GT911 initialized!");

    // LVGL v9 input device initialization
    indev = lv_indev_create();
    if (!indev) {
        Serial.println("[Touch] ERROR: Failed to create input device");
        return false;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);

    return true;
}

// External callback for screen wake
extern void resetScreenTimeout();

void touch_read(lv_indev_t *indev_drv, lv_indev_data_t *data) {
    static bool was_touched = false;
    ts.read();

    if (ts.isTouched) {
        // Touch sensor reports physical coordinates in portrait orientation
        // We need to apply 90° rotation to match display rotation
        // Physical panel: 480x800 portrait -> LVGL sees: 800x480 landscape
        // Display rotation: (x, y) portrait -> (y, height - 1 - x) landscape

        // Get raw touch coordinates (physical panel portrait coordinates)
        int16_t touch_x = map(ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, 479);
        int16_t touch_y = map(ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, 799);

        // Clamp raw coordinates
        if (touch_x < 0) touch_x = 0;
        if (touch_x > 479) touch_x = 479;
        if (touch_y < 0) touch_y = 0;
        if (touch_y > 799) touch_y = 799;

        // Apply 90° rotation to match display: portrait (touch_x, touch_y) -> landscape (x, y)
        // Using same rotation as display: (x, y) -> (y, height - 1 - x)
        // So touch at portrait (touch_x, touch_y) appears at landscape (touch_y, 479 - touch_x)
        data->point.x = touch_y;
        data->point.y = 479 - touch_x;
        data->state = LV_INDEV_STATE_PRESSED;

        // Reset screen timeout on EVERY touch
        if (!was_touched) {
            resetScreenTimeout();
            was_touched = true;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        was_touched = false;
    }
}
