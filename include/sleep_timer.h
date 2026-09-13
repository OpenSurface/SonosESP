#pragma once
/**
 * Sleep timer (issue #173) - the arithmetic.
 *
 * The countdown itself lives in the speaker. AVTransport on the group
 * coordinator holds it:
 *
 *     ConfigureSleepTimer   <NewSleepTimerDuration>00:45:00</...>   arm
 *     ConfigureSleepTimer   <NewSleepTimerDuration></...>           turn off
 *     GetRemainingSleepTimerDuration
 *         -> <RemainingSleepTimerDuration>0:44:12</...>             empty = none
 *
 * So the panel never keeps its own count. It reads what the speaker holds,
 * which also picks up a timer set by voice or in the Sonos app and survives
 * the panel restarting; between reads it only extrapolates for display.
 *
 * Free of Arduino, FreeRTOS and LVGL on purpose, so `pio test -e native`
 * covers it (test/test_sleep_timer).
 */
#include <stddef.h>
#include <stdio.h>

namespace sleep_timer {

// Longest duration the panel will send. The speaker takes HH:MM:SS, and
// nothing on the panel offers more than 90 min plus a few "+15"s, so this is
// only a guard on the format.
constexpr int MAX_SECONDS = 24 * 3600 - 1;

// NewSleepTimerDuration for ConfigureSleepTimer: "HH:MM:SS", or "" to turn the
// timer off. secs <= 0 means off; anything past MAX_SECONDS is capped.
inline void formatDuration(char* out, size_t n, int secs) {
    if (!out || n == 0) return;
    if (secs <= 0) { out[0] = '\0'; return; }
    if (secs > MAX_SECONDS) secs = MAX_SECONDS;
    snprintf(out, n, "%02d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60);
}

// RemainingSleepTimerDuration -> seconds. An empty element is how the speaker
// says "no timer", so it is 0. Both "H:MM:SS" and "HH:MM:SS" occur. Anything
// else is -1: a garbled reply must read as "unknown", never as "no timer".
inline int parseRemaining(const char* s) {
    if (!s || !*s) return 0;
    int h = 0, m = 0, sec = 0, used = 0;
    if (sscanf(s, "%d:%d:%d%n", &h, &m, &sec, &used) != 3 || s[used] != '\0') return -1;
    if (h < 0 || m < 0 || m > 59 || sec < 0 || sec > 59) return -1;
    return h * 3600 + m * 60 + sec;
}

// What the strip shows: whole minutes rounded UP, so it reads "1 min" until
// the music actually stops instead of "0 min" for its last minute.
inline int minutesLeft(int secs) { return secs <= 0 ? 0 : (secs + 59) / 60; }

// "+15 min" on a running timer (or from nothing), capped like everything else.
inline int extend(int secsLeft, int addSecs) {
    long t = (long)(secsLeft > 0 ? secsLeft : 0) + (addSecs > 0 ? addSecs : 0);
    return t > MAX_SECONDS ? MAX_SECONDS : (int)t;
}

} // namespace sleep_timer
