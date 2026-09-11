#pragma once
/**
 * Battery level for portable speakers (Move, Roam) - issue #165.
 *
 * The data is `GET http://<ip>:1400/status/batterystatus`, plain HTTP, no SOAP:
 *
 *     <ZPSupportInfo><LocalBatteryStatus>
 *     <Data name="Health">GREEN</Data>
 *     <Data name="Level">91</Data>
 *     <Data name="Temperature">NORMAL</Data>
 *     <Data name="PowerSource">BATTERY</Data>
 *     </LocalBatteryStatus></ZPSupportInfo>
 *
 * It is what SoCo's get_battery_info() reads, and what Home Assistant polls
 * (every 15 min) when UPnP events are not flowing. We never subscribe to
 * events, so polling is the only route here.
 *
 * Two facts decide the design, both from real hardware:
 *   - HTTP 200 is NOT a capability test. A mains-only Sonos Five answers 200
 *     with an empty <ZPSupportInfo></ZPSupportInfo>. A parsable Level is the
 *     capability, so there is no model table to maintain.
 *   - A sleeping portable accepts TCP and then never answers. A timeout means
 *     "stale", never "no battery", or the icon vanishes exactly while idle.
 *
 * Everything in the namespace is free of Arduino, FreeRTOS and LVGL on purpose,
 * so `pio test -e native` covers it (test/test_battery).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace battery {

struct Status {
    bool hasLevel;          // <Data name="Level"> present and numeric - the capability
    int  level;             // 0-100
    bool charging;          // on external power: PowerSource present and not "BATTERY"
    char powerSource[24];   // BATTERY, USB_POWER, SONOS_CHARGING_RING, or "" if absent
    char health[16];        // GREEN, ...  informational
    char temperature[16];   // NORMAL, ... informational
};

// The text of <Data name="KEY">...</Data>, trimmed, into out. False when absent.
inline bool dataField(const char* xml, const char* key, char* out, size_t n) {
    if (!out || n == 0) return false;
    out[0] = '\0';
    if (!xml) return false;
    char pat[48];
    snprintf(pat, sizeof(pat), "<Data name=\"%s\">", key);
    const char* p = strstr(xml, pat);
    if (!p) return false;
    p += strlen(pat);
    const char* e = strstr(p, "</Data>");
    if (!e) return false;
    while (p < e && isspace((unsigned char)*p)) p++;
    while (e > p && isspace((unsigned char)e[-1])) e--;
    size_t len = (size_t)(e - p);
    if (len >= n) len = n - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

// Returns hasLevel: whether this response makes the speaker a battery speaker.
inline bool parse(const char* xml, Status& st) {
    memset(&st, 0, sizeof(st));
    char lvl[8];
    if (dataField(xml, "Level", lvl, sizeof(lvl)) && lvl[0]) {
        char* end = nullptr;
        const long v = strtol(lvl, &end, 10);
        if (end && end != lvl && *end == '\0') {   // "abc" and "9x" are not levels
            st.hasLevel = true;
            st.level = v < 0 ? 0 : (v > 100 ? 100 : (int)v);
        }
    }
    dataField(xml, "PowerSource", st.powerSource, sizeof(st.powerSource));
    dataField(xml, "Health", st.health, sizeof(st.health));
    dataField(xml, "Temperature", st.temperature, sizeof(st.temperature));
    // Anything that is not BATTERY is external power. Derived this way round,
    // never by listing the three known sources, so one Sonos adds later does
    // not read as "on battery".
    st.charging = st.powerSource[0] && strcmp(st.powerSource, "BATTERY") != 0;
    return st.hasLevel;
}

// ── Display rules ───────────────────────────────────────────────────────────

// What a badge shows: a stored reading plus its freshness, or the simulator.
struct View {
    bool present;    // this speaker has a battery
    int  level;      // 0-100, or -1 before the first reading
    bool charging;   // on external power
    bool stale;      // the last refresh got no answer (asleep); level is the last known
};

constexpr int LOW_PCT      = 20;   // below this: gold, blinking
constexpr int CRITICAL_PCT = 10;   // at or below: the warning glyph

enum Glyph : uint8_t {
    GLYPH_FULL, GLYPH_MEDIUM, GLYPH_LOW, GLYPH_WARNING, GLYPH_CHARGING, GLYPH_EMPTY,
};

// Three bars at 67+, two at 34+, one above CRITICAL, then the "!" battery.
// Coarse on purpose: at 16px a bar is about a pixel wide, and the exact number
// is printed beside it anyway. A stale reading keeps its glyph - the badge dims
// it instead - since the last level is still the best information there is.
inline Glyph glyphFor(const View& v) {
    if (!v.present || v.level < 0) return GLYPH_EMPTY;
    if (v.charging)                return GLYPH_CHARGING;
    if (v.level <= CRITICAL_PCT)   return GLYPH_WARNING;
    if (v.level < 34)              return GLYPH_LOW;
    if (v.level < 67)              return GLYPH_MEDIUM;
    return GLYPH_FULL;
}

// Worth the user's attention: low, on battery, and the reading is current. Not
// while charging - it is already being fixed - and not when stale, since then
// we do not actually know.
inline bool warn(const View& v) {
    return v.present && !v.stale && !v.charging && v.level >= 0 && v.level < LOW_PCT;
}

}  // namespace battery

// ── Firmware API (src/battery.cpp) ──────────────────────────────────────────
// Declarations only, so the native test build never needs the definitions.

struct SonosDevice;

// From pollingTask, once per cycle after the mid-cycle guard. Makes at most one
// small request, and only when a speaker is due. True if it used the network.
bool batteryPollStep();

// What the UI should show for a speaker, simulator included.
battery::View batteryViewFor(const SonosDevice* dev);

// Advances the simulator's demo. Called once a second from the badge timer.
void batterySimTick();

// Serial "bat ..." commands - the simulator. True if cmd was one of them.
bool batterySerialCommand(const char* cmd);
