#include "clock_face.h"
#include "clock_screen.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

// Face builders (registry entries point at these).
void buildHorizonFace(lv_obj_t* parent);
void horizonTick(const struct tm* now);

// ---------------------------------------------------------------------------
// The registry. Adding a face is one row.
//
// ORDER IS PERSISTED: clock_style is stored in NVS as an index, so existing
// entries must keep their positions. Classic=0 and StandBy=1 match the
// CLOCK_STYLE_* constants in config.h that shipped to the fleet — new faces
// go on the END.
// ---------------------------------------------------------------------------
const ClockFaceDef CLOCK_FACES[] = {
    { "Classic", "Centred time with the date below",
      true,  nullptr, nullptr },
    { "StandBy", "Oversized overlapping digits, tinted from the album art",
      true,  nullptr, nullptr },
    { "Horizon", "Centred clock over an ambient glow, with a 6-hour forecast",
      false, buildHorizonFace, horizonTick },
};

const uint8_t CLOCK_FACE_COUNT = (uint8_t)(sizeof(CLOCK_FACES) / sizeof(CLOCK_FACES[0]));

const ClockFaceDef* clockFaceCurrent(void) {
    int idx = clock_style;
    if (idx < 0 || idx >= (int)CLOCK_FACE_COUNT) idx = CLOCK_DEFAULT_STYLE;
    return &CLOCK_FACES[idx];
}

void clockFaceClampStyle(void) {
    if (clock_style < 0 || clock_style >= (int)CLOCK_FACE_COUNT)
        clock_style = CLOCK_DEFAULT_STYLE;
}

bool clockFaceUsesPhotoBg(void) {
    return clockFaceCurrent()->photo_bg;
}

const char* clockFaceOptions(void) {
    // lv_dropdown copies the string it is given, but the options are also read
    // back while the dropdown is alive, so keep it in a static buffer rather
    // than a temporary.
    static char buf[256];
    buf[0] = '\0';
    size_t used = 0;
    for (uint8_t i = 0; i < CLOCK_FACE_COUNT; i++) {
        int n = snprintf(buf + used, sizeof(buf) - used, "%s%s",
                         i ? "\n" : "", CLOCK_FACES[i].name);
        if (n < 0 || (size_t)n >= sizeof(buf) - used) break;   // truncate, don't overrun
        used += (size_t)n;
    }
    return buf;
}
