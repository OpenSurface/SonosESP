/**
 * Host tests for include/reboot_log.h
 *
 *     pio test -e native
 *
 * The history lives in NVS and is read back on every boot, so what matters is
 * what goes wrong quietly: ring order once it wraps, a blob that is not ours,
 * a breadcrumb that is really power-on noise, and a restart cause surviving
 * onto a boot that was actually a crash.
 */

#include <unity.h>
#include <string.h>
#include "reboot_log.h"

using namespace reboot_log;

static Entry mk(uint8_t reason, uint8_t cause, uint32_t up, uint32_t epoch) {
    Entry e = {};
    e.reason = reason;
    e.cause = cause;
    e.uptime_s = up;
    e.epoch = epoch;
    return e;
}

static Crumb sealed(uint32_t up, uint32_t epoch, uint32_t cause) {
    Crumb c = {};
    c.uptime_s = up;
    c.epoch = epoch;
    c.cause = cause;
    crumbSeal(c);
    return c;
}

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Layout - this is what sits in NVS across firmware updates
// ---------------------------------------------------------------------------

void test_layout_is_stable(void) {
    TEST_ASSERT_EQUAL_UINT32(12, sizeof(Entry));
    TEST_ASSERT_EQUAL_UINT32(100, sizeof(Log));
    TEST_ASSERT_EQUAL_UINT32(20, sizeof(Crumb));
}

// ---------------------------------------------------------------------------
// Ring
// ---------------------------------------------------------------------------

void test_empty_log_has_nothing(void) {
    Log log;
    clear(log);
    TEST_ASSERT_TRUE(valid(log));
    TEST_ASSERT_EQUAL_INT(0, log.count);
    TEST_ASSERT_NULL(at(log, 0));
}

void test_newest_first(void) {
    Log log;
    clear(log);
    push(log, mk(RST_POWERON, 0, 1, 0));
    push(log, mk(RST_SW, CAUSE_USER, 2, 0));
    push(log, mk(RST_PANIC, 0, 3, 0));
    TEST_ASSERT_EQUAL_INT(3, log.count);
    TEST_ASSERT_EQUAL_UINT32(3, at(log, 0)->uptime_s);
    TEST_ASSERT_EQUAL_UINT32(2, at(log, 1)->uptime_s);
    TEST_ASSERT_EQUAL_UINT32(1, at(log, 2)->uptime_s);
    TEST_ASSERT_NULL(at(log, 3));
    TEST_ASSERT_NULL(at(log, -1));
}

void test_wraps_and_keeps_the_newest_eight(void) {
    Log log;
    clear(log);
    for (uint32_t i = 1; i <= 11; i++) push(log, mk(RST_SW, 0, i, 0));
    TEST_ASSERT_EQUAL_INT(CAPACITY, log.count);
    for (int i = 0; i < CAPACITY; i++) {
        TEST_ASSERT_EQUAL_UINT32(11 - i, at(log, i)->uptime_s);
    }
    TEST_ASSERT_NULL(at(log, CAPACITY));
}

void test_foreign_blob_is_rejected(void) {
    Log log;
    clear(log);
    log.count = CAPACITY + 1;
    TEST_ASSERT_FALSE(valid(log));

    clear(log);
    log.head = CAPACITY;
    TEST_ASSERT_FALSE(valid(log));

    clear(log);
    log.version = VERSION + 1;
    TEST_ASSERT_FALSE(valid(log));

    Log zero;                                   // a key that was never written
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_FALSE(valid(zero));
}

// ---------------------------------------------------------------------------
// Breadcrumb
// ---------------------------------------------------------------------------

void test_sealed_crumb_is_valid(void) {
    TEST_ASSERT_TRUE(crumbValid(sealed(3600, 1757600000u, CAUSE_USER)));
}

void test_any_changed_field_fails_the_check(void) {
    const Crumb c = sealed(3600, 1757600000u, CAUSE_USER);
    Crumb d;
    d = c; d.uptime_s++;          TEST_ASSERT_FALSE(crumbValid(d));
    d = c; d.epoch ^= 1;          TEST_ASSERT_FALSE(crumbValid(d));
    d = c; d.cause = CAUSE_OTA;   TEST_ASSERT_FALSE(crumbValid(d));
    d = c; d.magic ^= 0x80;       TEST_ASSERT_FALSE(crumbValid(d));
}

void test_blank_rtc_memory_is_not_a_crumb(void) {
    Crumb c;
    memset(&c, 0, sizeof(c));
    TEST_ASSERT_FALSE(crumbValid(c));
    memset(&c, 0xFF, sizeof(c));
    TEST_ASSERT_FALSE(crumbValid(c));
}

// ---------------------------------------------------------------------------
// fromBoot - turning the breadcrumb into a history entry
// ---------------------------------------------------------------------------

void test_software_restart_keeps_cause_and_uptime(void) {
    Entry e = fromBoot(RST_SW, sealed(18720, 1757600000u, CAUSE_DMA_WIFI_STOP));
    TEST_ASSERT_EQUAL_UINT8(CAUSE_DMA_WIFI_STOP, e.cause);
    TEST_ASSERT_EQUAL_UINT32(18720, e.uptime_s);
    TEST_ASSERT_EQUAL_UINT32(1757600000u, e.epoch);
    TEST_ASSERT_TRUE(unexpected(e));
}

// A cause is noted just before esp_restart(). If the chip panicked on the way
// down instead, the reset reason is the truth and the cause must not survive.
void test_crash_after_noting_a_cause_reports_the_crash(void) {
    Entry e = fromBoot(RST_PANIC, sealed(40, 1757600000u, CAUSE_USER));
    TEST_ASSERT_EQUAL_UINT8(CAUSE_NONE, e.cause);
    TEST_ASSERT_EQUAL_UINT32(40, e.uptime_s);
    TEST_ASSERT_EQUAL_STRING("Crashed", label(e));
}

// Power-on means RTC memory was not retained. A crumb that happens to pass the
// check must still not be reported as the last run's uptime.
void test_power_on_ignores_the_crumb(void) {
    Entry e = fromBoot(RST_POWERON, sealed(999, 1757600000u, CAUSE_USER));
    TEST_ASSERT_EQUAL_UINT32(UPTIME_UNKNOWN, e.uptime_s);
    TEST_ASSERT_EQUAL_UINT32(EPOCH_UNKNOWN, e.epoch);
    TEST_ASSERT_EQUAL_UINT8(CAUSE_NONE, e.cause);
}

void test_invalid_crumb_gives_unknowns(void) {
    Crumb junk;
    memset(&junk, 0xA5, sizeof(junk));
    Entry e = fromBoot(RST_SW, junk);
    TEST_ASSERT_EQUAL_UINT32(UPTIME_UNKNOWN, e.uptime_s);
    TEST_ASSERT_EQUAL_UINT8(CAUSE_NONE, e.cause);
    TEST_ASSERT_EQUAL_STRING("Restarted", label(e));
}

// Before NTP syncs, time() is 1970 plus uptime. That is not a date.
void test_unsynced_clock_is_not_a_date(void) {
    Entry e = fromBoot(RST_TASK_WDT, sealed(120, 120, CAUSE_NONE));
    TEST_ASSERT_EQUAL_UINT32(EPOCH_UNKNOWN, e.epoch);
    TEST_ASSERT_EQUAL_UINT32(120, e.uptime_s);
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

void test_what_counts_as_unexpected(void) {
    TEST_ASSERT_TRUE(unexpected(mk(RST_PANIC, 0, 0, 0)));
    TEST_ASSERT_TRUE(unexpected(mk(RST_TASK_WDT, 0, 0, 0)));
    TEST_ASSERT_TRUE(unexpected(mk(RST_BROWNOUT, 0, 0, 0)));
    TEST_ASSERT_TRUE(unexpected(mk(RST_SW, CAUSE_DMA_RECONNECT, 0, 0)));
    TEST_ASSERT_TRUE(unexpected(mk(RST_SW, CAUSE_WIFI_TIMEOUT, 0, 0)));
    TEST_ASSERT_TRUE(unexpected(mk(RST_SW, CAUSE_OTA_FAILED, 0, 0)));
    TEST_ASSERT_FALSE(unexpected(mk(RST_SW, CAUSE_USER, 0, 0)));
    TEST_ASSERT_FALSE(unexpected(mk(RST_SW, CAUSE_OTA, 0, 0)));
    TEST_ASSERT_FALSE(unexpected(mk(RST_POWERON, 0, 0, 0)));
    TEST_ASSERT_FALSE(unexpected(mk(RST_USB, 0, 0, 0)));
}

void test_labels(void) {
    TEST_ASSERT_EQUAL_STRING("You restarted it", label(mk(RST_SW, CAUSE_USER, 0, 0)));
    TEST_ASSERT_EQUAL_STRING("Froze (watchdog)", label(mk(RST_INT_WDT, 0, 0, 0)));
    TEST_ASSERT_EQUAL_STRING("Power dipped", label(mk(RST_PWR_GLITCH, 0, 0, 0)));
    TEST_ASSERT_EQUAL_STRING("USB connected", label(mk(RST_USB, 0, 0, 0)));
    TEST_ASSERT_EQUAL_STRING("Other reset", label(mk(RST_EFUSE, 0, 0, 0)));
}

void test_format_uptime(void) {
    char b[16];
    formatUptime(b, sizeof b, 42);                   TEST_ASSERT_EQUAL_STRING("42s", b);
    formatUptime(b, sizeof b, 17 * 60 + 5);          TEST_ASSERT_EQUAL_STRING("17m", b);
    formatUptime(b, sizeof b, 5 * 3600 + 12 * 60);   TEST_ASSERT_EQUAL_STRING("5h 12m", b);
    formatUptime(b, sizeof b, 3 * 86400 + 4 * 3600); TEST_ASSERT_EQUAL_STRING("3d 4h", b);
    formatUptime(b, sizeof b, UPTIME_UNKNOWN);       TEST_ASSERT_EQUAL_STRING("?", b);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_layout_is_stable);
    RUN_TEST(test_empty_log_has_nothing);
    RUN_TEST(test_newest_first);
    RUN_TEST(test_wraps_and_keeps_the_newest_eight);
    RUN_TEST(test_foreign_blob_is_rejected);
    RUN_TEST(test_sealed_crumb_is_valid);
    RUN_TEST(test_any_changed_field_fails_the_check);
    RUN_TEST(test_blank_rtc_memory_is_not_a_crumb);
    RUN_TEST(test_software_restart_keeps_cause_and_uptime);
    RUN_TEST(test_crash_after_noting_a_cause_reports_the_crash);
    RUN_TEST(test_power_on_ignores_the_crumb);
    RUN_TEST(test_invalid_crumb_gives_unknowns);
    RUN_TEST(test_unsynced_clock_is_not_a_date);
    RUN_TEST(test_what_counts_as_unexpected);
    RUN_TEST(test_labels);
    RUN_TEST(test_format_uptime);
    return UNITY_END();
}
