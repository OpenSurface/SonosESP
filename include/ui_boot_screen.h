/**
 * Studio boot sequence — artboard 1a of "SonosESP Boot + Screensaver".
 *
 * Replaces the logo-and-bar splash with a staged reveal: wordmark, then a
 * header with a progress hairline and four subsystem check lines that land as
 * each subsystem actually reports, then a cross-fade into the player.
 *
 * The check lines are DRIVEN BY EVENTS, not by a timeline. That is the whole
 * point of the design note ("a slow Wi-Fi join holds the timeline at its line
 * instead of faking progress"): bootScreenCheck() is called from setup() at the
 * moment a subsystem is known-good, and carries the real value it resolved to,
 * so a stalled join visibly stalls on the Wi-Fi row.
 */
#ifndef UI_BOOT_SCREEN_H
#define UI_BOOT_SCREEN_H

#include "lvgl.h"

// Only things the boot actually determines. There WAS a fourth line, "Lyrics &
// weather", which reported the provider names — but nothing at boot contacts
// LRCLIB or Open-Meteo, so it was a constant dressed up as a result. A check
// line that always says the same thing teaches the user to ignore all of them.
typedef enum {
    BOOT_CHECK_DISPLAY = 0,   // panel driver + resolution
    BOOT_CHECK_WIFI,          // SSID + RSSI, or why not
    BOOT_CHECK_SPEAKERS,      // room that answered, or why none did
    BOOT_CHECK_COUNT
} BootCheck;

// Builds the boot screen and loads it. Shows the wordmark; the header block is
// revealed by the first bootScreenReveal() or bootScreenCheck() call.
void bootScreenCreate(void);

// Cross-fades from the wordmark to the header/progress/checks view. Safe to
// call more than once — the second call is a no-op.
//
// Call this DELIBERATELY, at the point the wordmark has been up long enough.
// bootScreenCheck() no longer triggers it: the display check lands within
// milliseconds of the screen appearing, so letting it reveal meant the wordmark
// was gone almost before it was seen. Checks that arrive before the reveal are
// held and land with it.
void bootScreenReveal(void);

// Advances the progress hairline and pumps LVGL, exactly as setup()'s old
// updateBootProgress() lambda did. Percent is clamped to 0..100.
void bootScreenProgress(int percent);

// Lands one check line with the value it resolved to. `value` may be NULL for a
// line with nothing to report. Implies bootScreenReveal().
void bootScreenCheck(BootCheck which, const char* value);

// A transient message under the checks (the boot-OTA "Waiting for WiFi ..."
// line). Pass NULL to clear it.
void bootScreenStatus(const char* msg);

// Cross-fades to `next`, loads it, and frees the boot screen. Pass the screen
// that should be visible afterwards (scr_main).
void bootScreenFinish(lv_obj_t* next);

#endif // UI_BOOT_SCREEN_H
