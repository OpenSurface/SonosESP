#pragma once
/**
 * Reboot log - why the panel restarted, kept across restarts.
 *
 * The panel used to throw that away. esp_reset_reason() only describes the boot
 * you are in; a deliberate restart reports as a bare "SW" whichever of nine call
 * sites fired it; and nothing reached flash. A panel restarting four times a day
 * could not say why unless a serial console happened to be attached.
 *
 * Two halves:
 *   - A breadcrumb in RTC memory (src/reboot_log.cpp). It survives a software
 *     restart, a panic and a watchdog reset - everything but losing power - and
 *     costs no flash writes, so it is refreshed every second without the
 *     cache-off display glitch a flash write causes on these boards.
 *   - An 8-entry history in NVS, written once per boot from that breadcrumb and
 *     shown under Settings > General > Device.
 *
 * Everything in the namespace is free of Arduino, FreeRTOS and LVGL on purpose,
 * so `pio test -e native` covers it (test/test_reboot_log).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace reboot_log {

// esp_reset_reason_t, copied so this header stays framework-free.
// src/reboot_log.cpp static_asserts every value against the real enum.
enum : uint8_t {
    RST_UNKNOWN = 0, RST_POWERON = 1, RST_EXT = 2, RST_SW = 3, RST_PANIC = 4,
    RST_INT_WDT = 5, RST_TASK_WDT = 6, RST_WDT = 7, RST_DEEPSLEEP = 8,
    RST_BROWNOUT = 9, RST_SDIO = 10, RST_USB = 11, RST_JTAG = 12,
    RST_EFUSE = 13, RST_PWR_GLITCH = 14, RST_CPU_LOCKUP = 15,
};

// Which deliberate restart fired. Stored in NVS: append only, never renumber.
enum Cause : uint8_t {
    CAUSE_NONE = 0,       // not recorded - a crash, a power cut, or an untagged path
    CAUSE_USER,           // Settings > General > Device > Restart
    CAUSE_OTA,            // an update restarting to install, or done installing
    CAUSE_OTA_FAILED,     // boot-time update gave up and restarted to recover
    CAUSE_PANEL_WIZARD,   // 7" wizard trying the next LCD variant
    CAUSE_DMA_RECONNECT,  // DMA low again after the Wi-Fi reconnect recovery
    CAUSE_WIFI_TIMEOUT,   // Wi-Fi did not come back during that recovery
    CAUSE_DMA_WIFI_STOP,  // DMA still low even with Wi-Fi stopped
};

constexpr uint32_t UPTIME_UNKNOWN  = 0xFFFFFFFFu;
constexpr uint32_t EPOCH_UNKNOWN   = 0;
// Before NTP syncs, time() is 1970 plus uptime. Anything earlier than this
// (September 2020) is an unsynced clock, not a date.
constexpr uint32_t EPOCH_VALID_MIN = 1600000000u;

struct Entry {
    uint32_t epoch;      // wall clock when that run went down, or EPOCH_UNKNOWN
    uint32_t uptime_s;   // how long that run lasted, or UPTIME_UNKNOWN
    uint8_t  reason;     // RST_* of the boot that followed it
    uint8_t  cause;      // Cause - meaningful only when reason is RST_SW
    uint16_t reserved;
};

constexpr int     CAPACITY = 8;
constexpr uint8_t VERSION  = 1;

struct Log {
    uint8_t version;
    uint8_t count;       // valid entries, 0..CAPACITY
    uint8_t head;        // the slot the NEXT entry goes into
    uint8_t reserved;
    Entry   e[CAPACITY];
};

inline void clear(Log& log) {
    memset(&log, 0, sizeof(log));
    log.version = VERSION;
}

// A blob read from NVS is trusted only if it is our version and its indices are
// in range. Anything else - first boot, a future format, flash corruption -
// starts a fresh log rather than indexing past the array.
inline bool valid(const Log& log) {
    return log.version == VERSION && log.count <= CAPACITY && log.head < CAPACITY;
}

inline void push(Log& log, const Entry& en) {
    log.e[log.head] = en;
    log.head = (uint8_t)((log.head + 1) % CAPACITY);
    if (log.count < CAPACITY) log.count++;
}

// i = 0 is the newest. nullptr past the end.
inline const Entry* at(const Log& log, int i) {
    if (i < 0 || i >= log.count) return nullptr;
    return &log.e[(log.head - 1 - i + 2 * CAPACITY) % CAPACITY];
}

// ── Breadcrumb ──────────────────────────────────────────────────────────────
// Every field is 32-bit so the struct has no padding for the checksum to miss.
struct Crumb {
    uint32_t magic;
    uint32_t uptime_s;
    uint32_t epoch;
    uint32_t cause;
    uint32_t check;
};

constexpr uint32_t CRUMB_MAGIC = 0x52424C31u;   // "RBL1"

// FNV-1a over the payload. Not security - just enough that the random contents
// of RTC memory after a power-on do not pass for a real breadcrumb.
inline uint32_t crumbCheck(const Crumb& c) {
    const uint32_t w[4] = { c.magic, c.uptime_s, c.epoch, c.cause };
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4; i++) {
        for (int b = 0; b < 4; b++) {
            h ^= (w[i] >> (8 * b)) & 0xFFu;
            h *= 16777619u;
        }
    }
    return h;
}

inline void crumbSeal(Crumb& c)        { c.magic = CRUMB_MAGIC; c.check = crumbCheck(c); }
inline bool crumbValid(const Crumb& c) { return c.magic == CRUMB_MAGIC && c.check == crumbCheck(c); }

// The history entry for the run that just ended, from this boot's reset reason
// and whatever the breadcrumb held.
inline Entry fromBoot(uint8_t reason, const Crumb& c) {
    Entry en = {};
    en.reason   = reason;
    en.uptime_s = UPTIME_UNKNOWN;
    en.epoch    = EPOCH_UNKNOWN;
    en.cause    = CAUSE_NONE;

    // Power-on means RTC memory was not retained, so even a crumb that happens
    // to pass the check is leftover noise.
    if (reason == RST_POWERON || !crumbValid(c)) return en;

    en.uptime_s = c.uptime_s;
    en.epoch    = c.epoch >= EPOCH_VALID_MIN ? c.epoch : EPOCH_UNKNOWN;
    // A cause is noted just before esp_restart(). If the chip panicked or hit a
    // watchdog on the way down instead, the reset reason is the truth.
    if (reason == RST_SW) en.cause = (uint8_t)c.cause;
    return en;
}

// ── Presentation ────────────────────────────────────────────────────────────
// ASCII only: these reach the screen through the text fonts.
inline const char* label(const Entry& en) {
    if (en.reason == RST_SW) {
        switch (en.cause) {
            case CAUSE_USER:          return "You restarted it";
            case CAUSE_OTA:           return "Firmware update";
            case CAUSE_OTA_FAILED:    return "Update failed, recovered";
            case CAUSE_PANEL_WIZARD:  return "Screen setup";
            case CAUSE_DMA_RECONNECT: return "Low memory after reconnect";
            case CAUSE_WIFI_TIMEOUT:  return "Wi-Fi did not reconnect";
            case CAUSE_DMA_WIFI_STOP: return "Low memory (self-restart)";
            default:                  return "Restarted";
        }
    }
    switch (en.reason) {
        case RST_POWERON:    return "Powered on";
        case RST_EXT:        return "Reset button";
        case RST_PANIC:
        case RST_CPU_LOCKUP: return "Crashed";
        case RST_INT_WDT:
        case RST_TASK_WDT:
        case RST_WDT:        return "Froze (watchdog)";
        case RST_BROWNOUT:
        case RST_PWR_GLITCH: return "Power dipped";
        case RST_USB:
        case RST_JTAG:       return "USB connected";
        default:             return "Other reset";
    }
}

// Worth reporting: the panel went down without anyone asking it to.
inline bool unexpected(const Entry& en) {
    switch (en.reason) {
        case RST_PANIC: case RST_CPU_LOCKUP:
        case RST_INT_WDT: case RST_TASK_WDT: case RST_WDT:
        case RST_BROWNOUT: case RST_PWR_GLITCH:
            return true;
        case RST_SW:
            return en.cause == CAUSE_DMA_RECONNECT || en.cause == CAUSE_WIFI_TIMEOUT ||
                   en.cause == CAUSE_DMA_WIFI_STOP || en.cause == CAUSE_OTA_FAILED;
        default:
            return false;
    }
}

// "42s", "17m", "5h 12m", "3d 4h" - short enough for a right-hand column.
inline void formatUptime(char* buf, size_t n, uint32_t s) {
    if (s == UPTIME_UNKNOWN) { snprintf(buf, n, "?"); return; }
    const unsigned long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d)      snprintf(buf, n, "%lud %luh", d, h);
    else if (h) snprintf(buf, n, "%luh %lum", h, m);
    else if (m) snprintf(buf, n, "%lum", m);
    else        snprintf(buf, n, "%lus", (unsigned long)s);
}

}  // namespace reboot_log

// ── Firmware API (src/reboot_log.cpp) ───────────────────────────────────────
// Declarations only, so the native test build never needs the definitions.

// Once, early in setup(): turn the breadcrumb into a history entry, save it, and
// start a fresh breadcrumb for this run.
void rebootLogBoot();

// From mainAppTask every loop. Refreshes the breadcrumb at most once a second,
// in RTC memory only - never flash.
void rebootLogTick();

// Once, a few seconds after boot, from mainAppTask: the full history. Kept out
// of the boot burst on purpose - see issue #164 and src/reboot_log.cpp.
void rebootLogReport();

// Immediately before a deliberate restart: which one it was.
void rebootNoteCause(reboot_log::Cause cause);

// The history, for Settings > General > Device.
const reboot_log::Log& rebootLogHistory();
