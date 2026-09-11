/**
 * Reboot log - the firmware half. The why, and all the logic worth testing,
 * are in include/reboot_log.h.
 */
#include "reboot_log.h"
#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <time.h>

using namespace reboot_log;

// The header copies these to stay framework-free. If a framework bump ever
// renumbers esp_reset_reason_t, this fails the build rather than quietly
// labelling every crash as something else.
static_assert((int)ESP_RST_UNKNOWN    == RST_UNKNOWN,    "esp_reset_reason_t moved");
static_assert((int)ESP_RST_POWERON    == RST_POWERON,    "esp_reset_reason_t moved");
static_assert((int)ESP_RST_EXT        == RST_EXT,        "esp_reset_reason_t moved");
static_assert((int)ESP_RST_SW         == RST_SW,         "esp_reset_reason_t moved");
static_assert((int)ESP_RST_PANIC      == RST_PANIC,      "esp_reset_reason_t moved");
static_assert((int)ESP_RST_INT_WDT    == RST_INT_WDT,    "esp_reset_reason_t moved");
static_assert((int)ESP_RST_TASK_WDT   == RST_TASK_WDT,   "esp_reset_reason_t moved");
static_assert((int)ESP_RST_WDT        == RST_WDT,        "esp_reset_reason_t moved");
static_assert((int)ESP_RST_DEEPSLEEP  == RST_DEEPSLEEP,  "esp_reset_reason_t moved");
static_assert((int)ESP_RST_BROWNOUT   == RST_BROWNOUT,   "esp_reset_reason_t moved");
static_assert((int)ESP_RST_SDIO       == RST_SDIO,       "esp_reset_reason_t moved");
static_assert((int)ESP_RST_USB        == RST_USB,        "esp_reset_reason_t moved");
static_assert((int)ESP_RST_JTAG       == RST_JTAG,       "esp_reset_reason_t moved");
static_assert((int)ESP_RST_EFUSE      == RST_EFUSE,      "esp_reset_reason_t moved");
static_assert((int)ESP_RST_PWR_GLITCH == RST_PWR_GLITCH, "esp_reset_reason_t moved");
static_assert((int)ESP_RST_CPU_LOCKUP == RST_CPU_LOCKUP, "esp_reset_reason_t moved");

// The stored layout. Changing it orphans every panel's saved history, which
// valid() then discards - so bump VERSION with it, deliberately.
static_assert(sizeof(Entry) == 12,  "reboot_log::Entry layout changed - bump VERSION");
static_assert(sizeof(Log)   == 100, "reboot_log::Log layout changed - bump VERSION");

// RTC_NOINIT: kept through esp_restart(), a panic and a watchdog reset; only
// losing power clears it. It lives in .rtc_noinit (sections.ld), which the
// bootloader leaves alone - the one place a crash can leave a note for the next
// boot without writing flash.
static RTC_NOINIT_ATTR Crumb s_crumb;

static Log          s_log;
static uint32_t     s_last_tick_ms = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// Everything that can block happens before the critical section - time() takes
// newlib's lock - and only the RTC stores are inside it. rebootNoteCause() can
// run on the other core from the once-a-second tick, and a torn write would
// fail the checksum and lose exactly the restart it was meant to record.
static void crumbWrite(bool set_cause, uint32_t cause) {
    const uint32_t up  = (uint32_t)(esp_timer_get_time() / 1000000LL);
    const time_t   now = time(nullptr);
    const uint32_t ep  = now >= (time_t)EPOCH_VALID_MIN ? (uint32_t)now : EPOCH_UNKNOWN;

    taskENTER_CRITICAL(&s_mux);
    if (set_cause) s_crumb.cause = cause;
    s_crumb.uptime_s = up;
    s_crumb.epoch    = ep;
    crumbSeal(s_crumb);
    taskEXIT_CRITICAL(&s_mux);
}

// UTC on purpose: the serial report can run before the time zone is applied,
// and a log line is more useful unambiguous than local.
static void formatUtc(char* buf, size_t n, uint32_t epoch) {
    if (epoch == EPOCH_UNKNOWN) { snprintf(buf, n, "time unknown"); return; }
    const time_t t = (time_t)epoch;
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M", &tm);
}

void rebootLogBoot() {
    const uint8_t reason = (uint8_t)esp_reset_reason();
    const Entry   ended  = fromBoot(reason, s_crumb);

    // Start this run's breadcrumb before anything else, so a crash later in
    // setup() is recorded against this boot rather than the one before it.
    crumbWrite(true, CAUSE_NONE);

    // Its own handle, not wifiPrefs: this runs before wifiPrefs is opened, and
    // it has to, or an early-boot crash loop would never be recorded.
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        const size_t got = prefs.isKey(NVS_KEY_REBOOT_LOG)
                         ? prefs.getBytes(NVS_KEY_REBOOT_LOG, &s_log, sizeof(s_log)) : 0;
        if (got != sizeof(s_log) || !valid(s_log)) clear(s_log);
        push(s_log, ended);
        prefs.putBytes(NVS_KEY_REBOOT_LOG, &s_log, sizeof(s_log));
        prefs.end();
    } else {
        clear(s_log);
        push(s_log, ended);
    }

    // One line here; the full history waits for rebootLogReport().
    char up[16];
    formatUptime(up, sizeof(up), ended.uptime_s);
    Serial.printf("[REBOOT] Previous run ended: %s (reset %u, cause %u) after %s\n",
                  label(ended), (unsigned)ended.reason, (unsigned)ended.cause, up);
}

// Printed a few seconds after boot rather than inside it (issue #164). Attaching
// a serial console resets the chip over USB, and boot output written into the
// CDC link while it was still settling store-faulted hw_cdc_isr_handler - one
// panel died part way through printing the coredump banner. By the time this
// runs the host has reconnected and the boot output has drained.
void rebootLogReport() {
    char up[16];
    Serial.printf("[REBOOT] Last %d, newest first (UTC):\n", (int)s_log.count);
    for (int i = 0; i < s_log.count; i++) {
        const Entry* e = at(s_log, i);
        char when[24];
        formatUtc(when, sizeof(when), e->epoch);
        formatUptime(up, sizeof(up), e->uptime_s);
        Serial.printf("[REBOOT]   %-16s  %-27s  up %-7s%s\n", when, label(*e), up,
                      unexpected(*e) ? "  <- unexpected" : "");
    }
}

void rebootLogTick() {
    const uint32_t now = millis();
    if (now - s_last_tick_ms < 1000) return;
    s_last_tick_ms = now;
    crumbWrite(false, 0);
}

void rebootNoteCause(Cause cause) {
    crumbWrite(true, cause);
    Entry e = {};
    e.reason = RST_SW;
    e.cause  = cause;
    Serial.printf("[REBOOT] Deliberate restart: %s\n", label(e));
}

const Log& rebootLogHistory() { return s_log; }
