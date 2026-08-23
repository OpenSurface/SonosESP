#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <Arduino.h>
#include "lvgl.h"

// ---------------------------------------------------------------------------
// Serial frame grab — for documentation screenshots.
// ---------------------------------------------------------------------------
// Type "screenshot" into the serial monitor and the next completed frame is
// base64'd out of the port. tools/screenshot.py drives it and writes a PNG.
//
// The frame already exists: LVGL renders with LV_DISPLAY_RENDER_MODE_FULL, so
// display_flush() is handed one contiguous RGB565 buffer of the whole screen.
// Nothing is re-rendered; we copy bytes that are already there.
//
// Cost when idle is one bool test per flush. The frame is copied ONLY after a
// request arrives, and the copy buffer is freed again once the dump completes,
// so the steady-state cost of having this compiled in is nil. Copying every
// frame unconditionally would be ~768KB of memcpy at frame rate for a feature
// used a handful of times; keeping a bare pointer to px_map instead would be
// worse, because the draw buffers are double-buffered and the frame you kept a
// pointer to is being overwritten while you dump it.
//
// The dump blocks the calling task for a second or two, so the UI freezes for
// its duration. That is fine for a deliberate debug command and is why nothing
// calls it automatically.

// Called from display_flush() on every frame. Returns immediately unless a
// screenshot has been requested. `area` is used to confirm the flush really is
// a full frame before anything is copied.
void screenshotCaptureHook(const uint8_t* px_map, const lv_area_t* area);

// Called from the main task loop. Reads the serial port for the "screenshot"
// command and performs the dump once a frame has been captured.
void screenshotPoll(void);

#endif // SCREENSHOT_H
