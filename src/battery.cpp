/**
 * Battery level for portable speakers - the firmware half (issue #165).
 * The why, the parser and the display rules are in include/battery.h.
 *
 * Readings are kept per IP address, not per position in the device list. A
 * rescan rebuilds and reorders that list, and a reading filed by position was
 * lost - or could be shown against the wrong speaker - every time it did. A
 * stereo pair is two addresses under one device (SonosDevice::pairIP) and shows
 * the weaker of its two batteries.
 */
#include "battery.h"
#include "config.h"
#include "ui_common.h"          // sonos, network_mutex, last_network_end_ms
#include "ui_network_guard.h"   // sdioPreWait
#include "sonos_controller.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

namespace {

enum Probe : uint8_t {
    PROBE_UNKNOWN = 0,   // never answered: retried, never assumed either way
    PROBE_NONE,          // answered without a Level - mains only; re-asked hourly
    PROBE_PRESENT,       // has answered with a Level: latched for the session
};

struct Slot {
    uint32_t ip;           // 0 = free
    uint8_t  probe;
    int8_t   level;        // -1 until the first reading
    bool     charging;
    bool     stale;        // the last attempt got no reading
    uint32_t checked_ms;   // last attempt; 0 = never
    uint32_t read_ms;      // last good reading
};

// Room for every speaker the list can hold, and a stereo twin for each.
constexpr int SLOTS = MAX_SONOS_DEVICES * 2;
Slot     s_slots[SLOTS];
uint32_t s_last_request_ms = 0;

// Simulator - see batterySerialCommand().
bool s_sim          = false;
bool s_sim_demo     = false;
int  s_sim_level    = 50;
bool s_sim_charging = false;
bool s_sim_stale    = false;
int  s_sim_phase    = 0;
int  s_sim_hold     = 0;

uint32_t ipKey(const IPAddress& ip) {
    return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
           ((uint32_t)ip[2] << 8)  |  (uint32_t)ip[3];
}

Slot* findSlot(uint32_t key) {
    if (!key) return nullptr;
    for (Slot& s : s_slots) {
        if (s.ip == key) return &s;
    }
    return nullptr;
}

// Whether any current speaker, or its stereo twin, still has this address.
bool inUse(uint32_t key) {
    const int cnt = sonos.getDeviceCount();
    for (int i = 0; i < cnt; i++) {
        const SonosDevice* d = sonos.getDevice(i);
        if (d && (ipKey(d->ip) == key || ipKey(d->pairIP) == key)) return true;
    }
    return false;
}

// The slot for an address: its own, a free one, or one whose address no longer
// belongs to any speaker - which is what a rescan leaves behind. Polling task only.
Slot* claimSlot(uint32_t key) {
    if (Slot* s = findSlot(key)) return s;
    for (Slot& s : s_slots) {
        if (s.ip == 0 || !inUse(s.ip)) {
            memset(&s, 0, sizeof(s));
            s.ip = key;
            s.level = -1;
            return &s;
        }
    }
    return nullptr;   // SLOTS covers every speaker and its twin, so not reached
}

// Never asked: now. A known battery: every BATTERY_POLL_MS. Never answered:
// every BATTERY_RETRY_MS. "No battery": hourly, so one bad answer from a
// portable - mid-wake, say - cannot hide its battery until the next reboot.
bool due(const Slot& s, uint32_t now) {
    if (s.checked_ms == 0) return true;
    uint32_t every;
    switch (s.probe) {
        case PROBE_PRESENT: every = BATTERY_POLL_MS;         break;
        case PROBE_NONE:    every = BATTERY_NONE_RECHECK_MS; break;
        default:            every = BATTERY_RETRY_MS;        break;
    }
    return now - s.checked_ms >= every;
}

battery::View viewOf(const Slot* s, uint32_t now) {
    battery::View v = { false, -1, false, false };
    if (!s || s->probe != PROBE_PRESENT) return v;
    v.present  = true;
    v.level    = s->level;
    v.charging = s->charging;
    // Stale when the last attempt failed, or when nothing has succeeded for
    // BATTERY_STALE_MS. Polling pauses while the selected speaker is unreachable,
    // and an old number must not go on passing for a current one.
    v.stale = s->stale || (now - s->read_ms > BATTERY_STALE_MS);
    return v;
}

// One GET, under the same guards as sendSOAP(): the general cooldown before the
// mutex, a re-check inside it, a fresh HTTPClient, and last_network_end_ms
// stamped on the way out. Plain HTTP, so no HTTPS cooldown - see
// ui_network_guard.h for why adding one would cause the very crash it guards.
int fetch(const IPAddress& ip, battery::Status& st) {
    sdioPreWait("BATT");
    if (!xSemaphoreTake(network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS))) {
        return -100;
    }
    {
        const unsigned long elapsed = millis() - last_network_end_ms;
        if (last_network_end_ms > 0 && elapsed < SDIO_GENERAL_COOLDOWN_MS) {
            vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - elapsed));
        }
    }

    char url[64];
    snprintf(url, sizeof(url), "http://%s:1400/status/batterystatus", ip.toString().c_str());

    int code = -1;
    HTTPClient http;
    // Short on both legs: a sleeping portable accepts the connection and then
    // never answers, and SOAP polling is queued behind this mutex meanwhile.
    http.setConnectTimeout(BATTERY_HTTP_TIMEOUT_MS);
    http.setTimeout(BATTERY_HTTP_TIMEOUT_MS);
    if (http.begin(url)) {
        code = http.GET();
        if (code == HTTP_CODE_OK) {
            String body = http.getString();
            battery::parse(body.c_str(), st);
        }
        http.end();
    }

    last_network_end_ms = millis();
    xSemaphoreGive(network_mutex);
    return code;
}

}  // namespace

bool batteryPollStep() {
    // discoverDevices() runs on the UI task and rewrites the list this walks.
    if (sonos.isDiscovering()) return false;
    const uint32_t now = millis();
    if (s_last_request_ms && now - s_last_request_ms < BATTERY_PROBE_SPACING_MS) return false;
    // The queue fetch's DMA floor: a battery reading is never worth an
    // allocation the next art download will need.
    if (heap_caps_get_free_size(MALLOC_CAP_DMA) < ART_MIN_DMA_PRE_BURST) return false;

    // Pick the first address that is due, and copy it and the name NOW: the
    // request can take seconds, and a scan started meanwhile rewrites them.
    IPAddress target;
    char name[48] = "";
    Slot* pick = nullptr;
    const int cnt = sonos.getDeviceCount();
    for (int i = 0; i < cnt && !pick; i++) {
        const SonosDevice* d = sonos.getDevice(i);
        if (!d) continue;
        const IPAddress units[2] = { d->ip, d->pairIP };
        for (int u = 0; u < 2 && !pick; u++) {
            const uint32_t key = ipKey(units[u]);
            if (!key) continue;
            Slot* s = claimSlot(key);
            if (s && due(*s, now)) {
                pick = s;
                target = units[u];
                snprintf(name, sizeof(name), "%s%s", d->roomName.c_str(), u ? " (pair)" : "");
            }
        }
    }
    if (!pick) return false;
    s_last_request_ms = now;

    battery::Status st;
    const int code = fetch(target, st);
    pick->checked_ms = millis();
    if (pick->checked_ms == 0) pick->checked_ms = 1;   // 0 means "never"

    if (code == HTTP_CODE_OK && st.hasLevel) {
        pick->probe    = PROBE_PRESENT;
        pick->level    = (int8_t)st.level;
        pick->charging = st.charging;
        pick->stale    = false;
        pick->read_ms  = pick->checked_ms;
        Serial.printf("[BATT] %s: %d%% on %s (health %s, temperature %s)\n", name, st.level,
                      st.powerSource[0] ? st.powerSource : "?",
                      st.health[0] ? st.health : "?",
                      st.temperature[0] ? st.temperature : "?");
    } else if (code == HTTP_CODE_OK && pick->probe != PROBE_PRESENT) {
        pick->probe = PROBE_NONE;
        Serial.printf("[BATT] %s: no battery\n", name);
    } else if (pick->probe == PROBE_PRESENT) {
        // Asleep, most likely. Keep the capability and the last level, and mark
        // it stale so the badge shows "--" rather than a number nobody can vouch for.
        pick->stale = true;
        Serial.printf("[BATT] %s: no reading (%d) - keeping the last one as stale\n", name, code);
    } else {
        Serial.printf("[BATT] %s: no answer (%d) - will retry\n", name, code);
    }
    return true;
}

battery::View batteryViewFor(const SonosDevice* dev) {
    battery::View v = { false, -1, false, false };
    if (!dev) return v;

    // The simulator stands in for the SELECTED speaker only, so every other row
    // keeps showing what is really there.
    if (s_sim && dev == sonos.getCurrentDevice()) {
        v.present  = true;
        v.level    = s_sim_level;
        v.charging = s_sim_charging;
        v.stale    = s_sim_stale;
        return v;
    }

    const uint32_t now = millis();
    v = battery::merge(viewOf(findSlot(ipKey(dev->ip)), now),
                       viewOf(findSlot(ipKey(dev->pairIP)), now));
    // The selected speaker is polled all the time, so losing it is known at once
    // - no need to wait out BATTERY_STALE_MS before we stop vouching for it.
    if (v.present && dev == sonos.getCurrentDevice() && !dev->connected) v.stale = true;
    return v;
}

// Demo: 100% down to 0 in 5% steps a second apart - through three bars, two,
// one, the red blink under 20% and the warning glyph at 10% - then charging
// back up, then three seconds asleep, and round again. About 35 s a loop.
void batterySimTick() {
    if (!s_sim || !s_sim_demo) return;
    switch (s_sim_phase) {
        case 0:   // draining
            s_sim_charging = false;
            s_sim_stale = false;
            s_sim_level -= 5;
            if (s_sim_level <= 0) { s_sim_level = 0; s_sim_phase = 1; }
            break;
        case 1:   // charging
            s_sim_charging = true;
            s_sim_level += 10;
            if (s_sim_level >= 100) { s_sim_level = 100; s_sim_phase = 2; s_sim_hold = 3; }
            break;
        default:  // asleep
            s_sim_charging = false;
            s_sim_stale = true;
            if (--s_sim_hold <= 0) { s_sim_stale = false; s_sim_phase = 0; }
            break;
    }
}

static void printUnit(const char* label, const IPAddress& ip) {
    const uint32_t key = ipKey(ip);
    if (!key) return;
    const Slot* s = findSlot(key);
    Serial.printf("[BATT]   %-26s %-15s ", label, ip.toString().c_str());
    if (!s || s->probe == PROBE_UNKNOWN) {
        Serial.println("not read yet");
    } else if (s->probe == PROBE_NONE) {
        Serial.println("no battery");
    } else {
        const battery::View v = viewOf(s, millis());
        Serial.printf("%d%%%s%s\n", v.level, v.charging ? " on external power" : "",
                      v.stale ? " (stale)" : "");
    }
}

// Type into the serial monitor. There is no battery on a mains speaker, so this
// is how the badge, the levels and the low-battery blink get tested at all.
bool batterySerialCommand(const char* cmd) {
    if (strncmp(cmd, "bat", 3) != 0 || (cmd[3] != '\0' && cmd[3] != ' ')) return false;
    const char* arg = cmd + 3;
    while (*arg == ' ') arg++;

    if (*arg == '\0' || strcmp(arg, "help") == 0) {
        Serial.println("[BATT] bat <0-100> | bat chg | bat stale | bat demo | bat off | bat status");
        Serial.println("[BATT]   Simulates a battery on the SELECTED speaker, to test the UI.");
        return true;
    }
    if (strcmp(arg, "off") == 0) {
        s_sim = s_sim_demo = false;
        Serial.println("[BATT] Simulator off");
        return true;
    }
    if (strcmp(arg, "demo") == 0) {
        s_sim = s_sim_demo = true;
        s_sim_level = 100;
        s_sim_phase = 0;
        s_sim_charging = s_sim_stale = false;
        Serial.println("[BATT] Demo: draining, charging, asleep, repeat - 'bat off' to stop");
        return true;
    }
    if (strcmp(arg, "chg") == 0) {
        s_sim = true;
        s_sim_demo = false;
        s_sim_charging = !s_sim_charging;
        Serial.printf("[BATT] Simulated charging %s\n", s_sim_charging ? "on" : "off");
        return true;
    }
    if (strcmp(arg, "stale") == 0) {
        s_sim = true;
        s_sim_demo = false;
        s_sim_stale = !s_sim_stale;
        Serial.printf("[BATT] Simulated asleep %s\n", s_sim_stale ? "on" : "off");
        return true;
    }
    if (strcmp(arg, "status") == 0) {
        const int cnt = sonos.getDeviceCount();
        for (int i = 0; i < cnt && i < MAX_SONOS_DEVICES; i++) {
            SonosDevice* d = sonos.getDevice(i);
            if (!d) continue;
            char name[40] = "?";
            if (xSemaphoreTake(sonos.getDeviceMutex(), pdMS_TO_TICKS(50))) {
                strlcpy(name, d->roomName.c_str(), sizeof(name));
                xSemaphoreGive(sonos.getDeviceMutex());
            }
            printUnit(name, d->ip);
            char twin[48];
            snprintf(twin, sizeof(twin), "%s (pair)", name);
            printUnit(twin, d->pairIP);
        }
        Serial.printf("[BATT]   simulator %s\n", s_sim ? (s_sim_demo ? "demo" : "on") : "off");
        return true;
    }

    char* end = nullptr;
    const long v = strtol(arg, &end, 10);
    if (end != arg && *end == '\0' && v >= 0 && v <= 100) {
        s_sim = true;
        s_sim_demo = false;
        s_sim_level = (int)v;
        s_sim_stale = false;
        Serial.printf("[BATT] Simulated level %ld%%\n", v);
        return true;
    }
    Serial.printf("[BATT] Unknown: '%s' - try 'bat help'\n", arg);
    return true;
}
