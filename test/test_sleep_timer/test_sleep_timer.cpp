/**
 * Host tests for include/sleep_timer.h
 *
 *     pio test -e native
 *
 * What goes wrong quietly here is the wire format: a duration the speaker
 * rejects arms nothing, and a reply we misread shows a timer that is not
 * there - or hides one that is.
 */

#include <unity.h>
#include <string.h>
#include "sleep_timer.h"

using namespace sleep_timer;

void setUp(void) {}
void tearDown(void) {}

void test_format_is_hh_mm_ss(void) {
    char b[16];
    formatDuration(b, sizeof b, 45 * 60);       TEST_ASSERT_EQUAL_STRING("00:45:00", b);
    formatDuration(b, sizeof b, 90 * 60);       TEST_ASSERT_EQUAL_STRING("01:30:00", b);
    formatDuration(b, sizeof b, 3 * 3600 + 7);  TEST_ASSERT_EQUAL_STRING("03:00:07", b);
}

// An empty duration is how ConfigureSleepTimer turns the timer off.
void test_zero_or_negative_is_off(void) {
    char b[16] = "junk";
    formatDuration(b, sizeof b, 0);
    TEST_ASSERT_EQUAL_STRING("", b);
    strcpy(b, "junk");
    formatDuration(b, sizeof b, -5);
    TEST_ASSERT_EQUAL_STRING("", b);
}

void test_format_is_capped(void) {
    char b[16];
    formatDuration(b, sizeof b, 30 * 3600);
    TEST_ASSERT_EQUAL_STRING("23:59:59", b);
}

void test_parse_both_hour_widths(void) {
    TEST_ASSERT_EQUAL_INT(44 * 60 + 12, parseRemaining("0:44:12"));
    TEST_ASSERT_EQUAL_INT(44 * 60 + 12, parseRemaining("00:44:12"));
    TEST_ASSERT_EQUAL_INT(3600 + 30 * 60, parseRemaining("1:30:00"));
}

void test_empty_means_no_timer(void) {
    TEST_ASSERT_EQUAL_INT(0, parseRemaining(""));
    TEST_ASSERT_EQUAL_INT(0, parseRemaining(nullptr));
}

void test_garbage_is_unknown_not_none(void) {
    TEST_ASSERT_EQUAL_INT(-1, parseRemaining("soon"));
    TEST_ASSERT_EQUAL_INT(-1, parseRemaining("12:34"));
    TEST_ASSERT_EQUAL_INT(-1, parseRemaining("0:61:00"));
    TEST_ASSERT_EQUAL_INT(-1, parseRemaining("0:44:12x"));
}

void test_round_trip(void) {
    char b[16];
    formatDuration(b, sizeof b, 5 * 60);
    TEST_ASSERT_EQUAL_INT(5 * 60, parseRemaining(b));
}

void test_minutes_round_up(void) {
    TEST_ASSERT_EQUAL_INT(0, minutesLeft(0));
    TEST_ASSERT_EQUAL_INT(0, minutesLeft(-1));
    TEST_ASSERT_EQUAL_INT(1, minutesLeft(1));
    TEST_ASSERT_EQUAL_INT(1, minutesLeft(60));
    TEST_ASSERT_EQUAL_INT(2, minutesLeft(61));
    TEST_ASSERT_EQUAL_INT(45, minutesLeft(45 * 60));
}

void test_extend(void) {
    TEST_ASSERT_EQUAL_INT(38 * 60, extend(23 * 60, 15 * 60));
    TEST_ASSERT_EQUAL_INT(15 * 60, extend(0, 15 * 60));
    TEST_ASSERT_EQUAL_INT(15 * 60, extend(-1, 15 * 60));   // unknown counts as none
    TEST_ASSERT_EQUAL_INT(MAX_SECONDS, extend(MAX_SECONDS, 15 * 60));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_format_is_hh_mm_ss);
    RUN_TEST(test_zero_or_negative_is_off);
    RUN_TEST(test_format_is_capped);
    RUN_TEST(test_parse_both_hour_widths);
    RUN_TEST(test_empty_means_no_timer);
    RUN_TEST(test_garbage_is_unknown_not_none);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_minutes_round_up);
    RUN_TEST(test_extend);
    return UNITY_END();
}
