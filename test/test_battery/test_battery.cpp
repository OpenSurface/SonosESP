/**
 * Host tests for include/battery.h (issue #165)
 *
 *     pio test -e native
 *
 * The bodies are the real thing. ROAM_ON_BATTERY is verbatim from a Roam 2 as
 * captured in #165; the other portable states reuse its exact shape with the
 * values from the same report's table. FIVE_MAINS is verbatim from a Sonos Five
 * on a live network - the negative case the report could not test, and the one
 * that proves HTTP 200 is not a capability check.
 */

#include <unity.h>
#include <string.h>
#include "battery.h"

using namespace battery;

static const char* ROAM_ON_BATTERY =
    "<ZPSupportInfo><LocalBatteryStatus>\n"
    "<Data name=\"Health\">GREEN</Data>\n"
    "<Data name=\"Level\">91</Data>\n"
    "<Data name=\"Temperature\">NORMAL</Data>\n"
    "<Data name=\"PowerSource\">BATTERY</Data>\n"
    "</LocalBatteryStatus></ZPSupportInfo>";

static const char* ROAM_ON_USB =
    "<ZPSupportInfo><LocalBatteryStatus>\n"
    "<Data name=\"Health\">GREEN</Data>\n"
    "<Data name=\"Level\">90</Data>\n"
    "<Data name=\"Temperature\">NORMAL</Data>\n"
    "<Data name=\"PowerSource\">USB_POWER</Data>\n"
    "</LocalBatteryStatus></ZPSupportInfo>";

static const char* MOVE_ON_RING =
    "<ZPSupportInfo><LocalBatteryStatus>\n"
    "<Data name=\"Health\">GREEN</Data>\n"
    "<Data name=\"Level\">100</Data>\n"
    "<Data name=\"Temperature\">NORMAL</Data>\n"
    "<Data name=\"PowerSource\">SONOS_CHARGING_RING</Data>\n"
    "</LocalBatteryStatus></ZPSupportInfo>";

static const char* FIVE_MAINS =
    "<?xml version=\"1.0\" ?>\n"
    "<?xml-stylesheet type=\"text/xsl\" href=\"/xml/review.xsl\"?>"
    "<ZPSupportInfo></ZPSupportInfo>";

// A portable body with a chosen Level and PowerSource (nullptr = no PowerSource).
static const char* make(const char* level, const char* source) {
    static char b[512];
    snprintf(b, sizeof(b),
             "<ZPSupportInfo><LocalBatteryStatus>"
             "<Data name=\"Health\">GREEN</Data>"
             "<Data name=\"Level\">%s</Data>"
             "<Data name=\"Temperature\">NORMAL</Data>"
             "%s%s%s"
             "</LocalBatteryStatus></ZPSupportInfo>",
             level,
             source ? "<Data name=\"PowerSource\">" : "",
             source ? source : "",
             source ? "</Data>" : "");
    return b;
}

static View view(int level, bool charging = false, bool stale = false, bool present = true) {
    View v;
    v.present = present;
    v.level = level;
    v.charging = charging;
    v.stale = stale;
    return v;
}

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Parser - real responses
// ---------------------------------------------------------------------------

void test_roam_on_battery(void) {
    Status st;
    TEST_ASSERT_TRUE(parse(ROAM_ON_BATTERY, st));
    TEST_ASSERT_EQUAL_INT(91, st.level);
    TEST_ASSERT_FALSE(st.charging);
    TEST_ASSERT_EQUAL_STRING("BATTERY", st.powerSource);
    TEST_ASSERT_EQUAL_STRING("GREEN", st.health);
    TEST_ASSERT_EQUAL_STRING("NORMAL", st.temperature);
}

void test_usb_power_counts_as_charging(void) {
    Status st;
    TEST_ASSERT_TRUE(parse(ROAM_ON_USB, st));
    TEST_ASSERT_EQUAL_INT(90, st.level);
    TEST_ASSERT_TRUE(st.charging);
}

void test_charging_ring_counts_as_charging(void) {
    Status st;
    TEST_ASSERT_TRUE(parse(MOVE_ON_RING, st));
    TEST_ASSERT_EQUAL_INT(100, st.level);
    TEST_ASSERT_TRUE(st.charging);
}

// The whole reason a 200 is not enough.
void test_mains_five_has_no_battery(void) {
    Status st;
    TEST_ASSERT_FALSE(parse(FIVE_MAINS, st));
    TEST_ASSERT_FALSE(st.hasLevel);
    TEST_ASSERT_FALSE(st.charging);
}

// ---------------------------------------------------------------------------
// Parser - edges
// ---------------------------------------------------------------------------

void test_a_new_power_source_is_external(void) {
    Status st;
    TEST_ASSERT_TRUE(parse(make("50", "SOLAR_PANEL"), st));
    TEST_ASSERT_TRUE(st.charging);
}

void test_missing_power_source_is_not_charging(void) {
    Status st;
    TEST_ASSERT_TRUE(parse(make("50", nullptr), st));
    TEST_ASSERT_FALSE(st.charging);
}

void test_non_numeric_level_is_not_a_level(void) {
    Status st;
    TEST_ASSERT_FALSE(parse(make("abc", "BATTERY"), st));
    TEST_ASSERT_FALSE(parse(make("9x", "BATTERY"), st));
    TEST_ASSERT_FALSE(parse(make("", "BATTERY"), st));
}

void test_level_is_clamped_and_trimmed(void) {
    Status st;
    TEST_ASSERT_TRUE(parse(make("150", "BATTERY"), st));
    TEST_ASSERT_EQUAL_INT(100, st.level);
    TEST_ASSERT_TRUE(parse(make("-3", "BATTERY"), st));
    TEST_ASSERT_EQUAL_INT(0, st.level);
    TEST_ASSERT_TRUE(parse(make(" 42 ", "BATTERY"), st));
    TEST_ASSERT_EQUAL_INT(42, st.level);
}

void test_null_and_empty_bodies(void) {
    Status st;
    TEST_ASSERT_FALSE(parse(nullptr, st));
    TEST_ASSERT_FALSE(parse("", st));
}

// ---------------------------------------------------------------------------
// Display rules
// ---------------------------------------------------------------------------

void test_glyph_thresholds(void) {
    TEST_ASSERT_EQUAL_UINT8(GLYPH_FULL,    glyphFor(view(100)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_FULL,    glyphFor(view(67)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_MEDIUM,  glyphFor(view(66)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_MEDIUM,  glyphFor(view(34)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_LOW,     glyphFor(view(33)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_LOW,     glyphFor(view(11)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_WARNING, glyphFor(view(10)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_WARNING, glyphFor(view(0)));
}

void test_glyph_special_states(void) {
    TEST_ASSERT_EQUAL_UINT8(GLYPH_CHARGING, glyphFor(view(5, true)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_EMPTY,    glyphFor(view(80, false, false, false)));
    TEST_ASSERT_EQUAL_UINT8(GLYPH_EMPTY,    glyphFor(view(-1)));
    // Stale keeps the last level's glyph; the badge greys it instead.
    TEST_ASSERT_EQUAL_UINT8(GLYPH_FULL,     glyphFor(view(80, false, true)));
}

void test_when_it_warns(void) {
    TEST_ASSERT_TRUE(warn(view(19)));
    TEST_ASSERT_TRUE(warn(view(0)));
    TEST_ASSERT_FALSE(warn(view(20)));
    TEST_ASSERT_FALSE(warn(view(5, true)));                  // charging: being fixed
    TEST_ASSERT_FALSE(warn(view(5, false, true)));           // stale: we do not know
    TEST_ASSERT_FALSE(warn(view(5, false, false, false)));   // no battery
    TEST_ASSERT_FALSE(warn(view(-1)));                       // never read
}

// Red must start exactly where the blink does, or a battery could blink while
// still yellow, or turn red without blinking.
void test_traffic_light(void) {
    TEST_ASSERT_EQUAL_UINT8(TONE_GOOD,  toneFor(view(100)));
    TEST_ASSERT_EQUAL_UINT8(TONE_GOOD,  toneFor(view(GOOD_PCT)));
    TEST_ASSERT_EQUAL_UINT8(TONE_MID,   toneFor(view(GOOD_PCT - 1)));
    TEST_ASSERT_EQUAL_UINT8(TONE_MID,   toneFor(view(LOW_PCT)));
    TEST_ASSERT_EQUAL_UINT8(TONE_LOW,   toneFor(view(LOW_PCT - 1)));
    TEST_ASSERT_EQUAL_UINT8(TONE_LOW,   toneFor(view(0)));
    TEST_ASSERT_EQUAL_UINT8(TONE_GOOD,  toneFor(view(5, true)));          // charging
    TEST_ASSERT_EQUAL_UINT8(TONE_STALE, toneFor(view(80, false, true)));  // asleep
    TEST_ASSERT_EQUAL_UINT8(TONE_STALE, toneFor(view(-1)));               // never read
    for (int lvl = 0; lvl <= 100; lvl++) {
        TEST_ASSERT_EQUAL(warn(view(lvl)), toneFor(view(lvl)) == TONE_LOW);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_roam_on_battery);
    RUN_TEST(test_usb_power_counts_as_charging);
    RUN_TEST(test_charging_ring_counts_as_charging);
    RUN_TEST(test_mains_five_has_no_battery);
    RUN_TEST(test_a_new_power_source_is_external);
    RUN_TEST(test_missing_power_source_is_not_charging);
    RUN_TEST(test_non_numeric_level_is_not_a_level);
    RUN_TEST(test_level_is_clamped_and_trimmed);
    RUN_TEST(test_null_and_empty_bodies);
    RUN_TEST(test_glyph_thresholds);
    RUN_TEST(test_glyph_special_states);
    RUN_TEST(test_when_it_warns);
    RUN_TEST(test_traffic_light);
    return UNITY_END();
}
