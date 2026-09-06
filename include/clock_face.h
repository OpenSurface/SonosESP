/**
 * Screensaver clock-face registry.
 *
 * Mirrors the player theme registry in ui_theme.h: a face is ONE row in
 * CLOCK_FACES[] in clock_face.cpp. Adding one is a single entry — the settings
 * dropdown, NVS clamping and the render path all iterate the registry, so
 * nothing else needs editing.
 *
 * Two families live here:
 *   - LEGACY faces (Classic, StandBy) are drawn by the widgets that
 *     createClockScreen() already builds, and switched by applyClockStyle().
 *     Their `build` is NULL and their index must stay stable — it is persisted
 *     in NVS on deployed devices.
 *   - NOCTURNE faces (Orbit, Monolith, Horizon) each own a builder that
 *     constructs its whole layout, so a face can be a genuinely different
 *     design rather than a recolour.
 *
 * CONTRACT FOR BUILDERS: a builder owns everything it creates and must supply
 * its own `tick`. clock_tick_cb hands control to that tick and returns; it only
 * renders anything itself for the builder-less StandBy face.
 */
#ifndef CLOCK_FACE_H
#define CLOCK_FACE_H

#include <stdint.h>
#include <time.h>
#include "lvgl.h"

// Builds the face into `parent` (scr_clock). NULL = legacy applyClockStyle path.
typedef void (*ClockFaceBuildFn)(lv_obj_t* parent);

// Per-second update. NULL = the shared clock_tick_cb handles it.
typedef void (*ClockFaceTickFn)(const struct tm* now);

// Returns the face's root container. applyClockStyle() shows/hides by walking
// scr_clock's children, so it only needs to know which child belongs to the
// active face. NULL for legacy faces.
typedef lv_obj_t* (*ClockFaceRootFn)(void);

typedef struct {
    const char*      name;        // label in the settings dropdown
    const char*      desc;        // one-line description under the dropdown
    bool             photo_bg;    // true = wants the Unsplash/loremflickr backdrop.
                                  // The Nocturne faces paint their own gradient and
                                  // must suppress the photo fetch entirely.
    bool             noc_backdrop; // true = applyClockStyle() paints the shared
                                  // Nocturne gradient (or photo scrim) onto this
                                  // face's root. False for a face that fills its
                                  // own ground, which would otherwise be painted
                                  // over on every style change. Studio is flat by
                                  // specification and sets this false.
    ClockFaceBuildFn build;
    ClockFaceTickFn  tick;
    ClockFaceRootFn  root;
} ClockFaceDef;

extern const ClockFaceDef CLOCK_FACES[];
extern const uint8_t      CLOCK_FACE_COUNT;

const ClockFaceDef* clockFaceCurrent(void);

// Newline-separated option list for lv_dropdown, built from the registry so a
// new face appears in Settings automatically. Valid until the next call.
const char* clockFaceOptions(void);

// Clamps clock_style into range. Call after loading it from NVS — the saved
// index may name a face that no longer exists after a downgrade.
void clockFaceClampStyle(void);

// True when the current face wants the downloaded photo backdrop. Gates the
// background image fetch in clockBgTask().
bool clockFaceUsesPhotoBg(void);

// Root of the active face, or NULL when a legacy face is selected.
lv_obj_t* clockFaceActiveRoot(void);

// One-time rewrite of persisted face indices after the list changed.
void clockFaceMigrate(void);

#endif // CLOCK_FACE_H
