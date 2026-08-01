#include "clock_face.h"
#include "clock_screen.h"
#include "config.h"
#include "ui_common.h"   // wifiPrefs
#include <string.h>
#include <stdio.h>

// Face builders (registry entries point at these).
void buildHorizonFace(lv_obj_t* parent);
void horizonTick(const struct tm* now);
void buildOrbitFace(lv_obj_t* parent);
void orbitTick(const struct tm* now);
void buildMonolithFace(lv_obj_t* parent);
void monolithTick(const struct tm* now);
lv_obj_t* horizonRoot(void);
lv_obj_t* orbitRoot(void);
lv_obj_t* monolithRoot(void);

// ---------------------------------------------------------------------------
// The registry. Adding a face is one row.
//
// ORDER IS PERSISTED: clock_style is stored in NVS as an index, so existing
// entries must keep their positions. Classic=0 and StandBy=1 match the
// CLOCK_STYLE_* constants in config.h that shipped to the fleet — new faces
// go on the END.
// ---------------------------------------------------------------------------
const ClockFaceDef CLOCK_FACES[] = {
    { "StandBy",  "Oversized overlapping digits, tinted from the album art",
      true,  nullptr,            nullptr,       nullptr },
    { "Orbit",    "Light clock with a live sun-path arc and temperature curve",
      false, buildOrbitFace,     orbitTick,     orbitRoot },
    { "Monolith", "Hours stacked over minutes, with a details column",
      false, buildMonolithFace,  monolithTick,  monolithRoot },
    { "Horizon",  "Centred clock over an ambient glow, with a 6-hour forecast",
      false, buildHorizonFace,   horizonTick,   horizonRoot },
};

const uint8_t CLOCK_FACE_COUNT = (uint8_t)(sizeof(CLOCK_FACES) / sizeof(CLOCK_FACES[0]));

const ClockFaceDef* clockFaceCurrent(void) {
    int idx = clock_style;
    if (idx < 0 || idx >= (int)CLOCK_FACE_COUNT) idx = CLOCK_DEFAULT_STYLE;
    return &CLOCK_FACES[idx];
}

// Bump when the meaning of a persisted index changes.
#define CLOCK_FACE_SCHEMA 1

// v0 (<=1.10) numbering was Classic=0, StandBy=1. Classic has been removed, so
// every saved index has to be rewritten or users land on the wrong face — a
// device set to Classic would silently become Orbit. Runs once; the marker key
// stops it re-running and clobbering a later deliberate choice.
void clockFaceMigrate(void) {
    if (wifiPrefs.getInt(NVS_KEY_CLOCK_FACE_VER, 0) >= CLOCK_FACE_SCHEMA) return;

    int old_idx = wifiPrefs.getInt(NVS_KEY_CLOCK_STYLE, -1);
    if (old_idx >= 0) {
        int mapped;
        switch (old_idx) {
            case 0:  mapped = CLOCK_STYLE_STANDBY;  break;  // Classic (removed) -> StandBy
            case 1:  mapped = CLOCK_STYLE_STANDBY;  break;  // StandBy keeps its face
            case 2:  mapped = CLOCK_STYLE_HORIZON;  break;  // pre-release Horizon
            default: mapped = CLOCK_DEFAULT_STYLE;  break;
        }
        clock_style = mapped;
        wifiPrefs.putInt(NVS_KEY_CLOCK_STYLE, mapped);
        Serial.printf("[CLOCK] Face index migrated: %d -> %d (%s)\n",
                      old_idx, mapped, CLOCK_FACES[mapped].name);
    }
    wifiPrefs.putInt(NVS_KEY_CLOCK_FACE_VER, CLOCK_FACE_SCHEMA);
}

void clockFaceClampStyle(void) {
    if (clock_style < 0 || clock_style >= (int)CLOCK_FACE_COUNT)
        clock_style = CLOCK_DEFAULT_STYLE;
}

lv_obj_t* clockFaceActiveRoot(void) {
    const ClockFaceDef* f = clockFaceCurrent();
    return f->root ? f->root() : nullptr;
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
