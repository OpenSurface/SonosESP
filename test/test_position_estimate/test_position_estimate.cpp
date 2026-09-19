/**
 * Host tests for include/position_estimate.h
 *
 *     pio test -e native
 *
 * The bar used to move in one-second steps because the speaker reports whole
 * seconds on an irregular poll. The estimator's job is to fill the gaps without
 * ever telling a lie that lasts - so what is tested here is both halves: that
 * it runs smoothly between readings, and that it still obeys the speaker when
 * the speaker says something genuinely new.
 *
 * Time is a parameter throughout, so a whole track plays in microseconds and
 * the millis() rollover is just another number.
 */

#include <unity.h>
#include "position_estimate.h"

using namespace position_estimate;

void setUp(void) {}
void tearDown(void) {}

// A reading as the UI sees it: the speaker's whole second, stamped with the
// millis() at which the polling task wrote it.
static void feed(Estimator& e, int pos_sec, uint32_t read_ms, uint32_t now_ms,
                 int duration_sec = 240, bool playing = true) {
    e.tick(pos_sec, duration_sec, playing, read_ms, now_ms);
}

void test_unknown_until_first_reading(void) {
    Estimator e;
    TEST_ASSERT_FALSE(e.hasPosition());
    TEST_ASSERT_EQUAL_INT32(-1, e.positionMs(1000));

    // Playback state alone is not a position.
    e.tick(0, 240, true, 0, 1000);
    TEST_ASSERT_EQUAL_INT32(-1, e.positionMs(1000));
}

// "135" means the truth is in [135.000, 136.000), so the estimate aims for the
// middle. Anchoring on the number itself sits half a second behind for ever.
void test_first_reading_anchors_mid_window(void) {
    Estimator e;
    feed(e, 135, 5000, 5000);
    TEST_ASSERT_EQUAL_INT32(135500, e.positionMs(5000));
}

void test_interpolates_between_readings(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);
    TEST_ASSERT_EQUAL_INT32(10500, e.positionMs(1000));
    TEST_ASSERT_EQUAL_INT32(10600, e.positionMs(1100));
    TEST_ASSERT_EQUAL_INT32(11000, e.positionMs(1500));
    TEST_ASSERT_EQUAL_INT32(12500, e.positionMs(3000));
}

// The UI calls tick() ten times a second; the poll answers three times a
// second. Re-offering the same reading must not re-anchor, or the estimate
// would be dragged back to the same second over and over - the staircase.
void test_repeat_of_same_reading_does_not_re_anchor(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);
    for (uint32_t t = 1100; t <= 1900; t += 100) feed(e, 10, 1000, t);
    TEST_ASSERT_EQUAL_INT32(11400, e.positionMs(1900));
}

// The whole point, stated as an invariant: across a realistic poll train the
// position never goes backwards and never stalls for a whole second.
void test_position_is_monotonic_and_never_stalls(void) {
    Estimator e;
    uint32_t now = 0;
    int32_t  last = -1;
    uint32_t last_move = 0;

    // 40 seconds of playback. The UI ticks every 100ms; readings arrive every
    // 300ms, with the holes the polling task really leaves - 1.2s every twelfth
    // reading, which is an album art download.
    uint32_t next_read    = 0;
    uint32_t last_read_ms = 0;
    int      read_no      = 0;
    for (now = 0; now < 40000; now += 100) {
        if (now >= next_read) {
            read_no++;
            last_read_ms = now + 1;          // whatever millis() the poll stamped
            next_read    = now + ((read_no % 12 == 0) ? 1200 : 300);
        }
        // The speaker's own clock, truncated to the second, offset so its second
        // boundaries do not line up with our polls. Between readings the UI
        // re-offers the SAME reading, which must not re-anchor anything.
        feed(e, (int)((now + 400) / 1000), last_read_ms, now);

        const int32_t p = e.positionMs(now);
        TEST_ASSERT_TRUE_MESSAGE(p >= last, "position went backwards");
        if (p != last) { last = p; last_move = now; }
        TEST_ASSERT_TRUE_MESSAGE(now - last_move < 1000, "position stalled for a second");
    }
}

// A seek in the Sonos app. Nothing gradual about it - follow at once.
void test_snaps_on_a_jump(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);
    feed(e, 95, 1300, 1300);
    TEST_ASSERT_EQUAL_INT32(95500, e.positionMs(1300));
}

void test_snaps_backwards_too(void) {
    Estimator e;
    feed(e, 100, 1000, 1000);
    feed(e, 12, 1300, 1300);
    TEST_ASSERT_EQUAL_INT32(12500, e.positionMs(1300));
}

// Drift is corrected, but never in one visible move.
void test_eases_a_small_error(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);          // estimate 10500 at t=1000

    // A second on, the estimate reads 11500 and the speaker still says 10 -
    // centre 10500, so the estimate is 1s fast. Too far to ignore, not far
    // enough to be a seek, so a quarter of the error comes off: 11500 - 250.
    feed(e, 10, 2000, 2000);
    TEST_ASSERT_EQUAL_INT32(11250, e.positionMs(2000));

    // And keeps converging on the readings that follow rather than jumping.
    feed(e, 11, 2300, 2300);
    const int32_t p = e.positionMs(2300);
    TEST_ASSERT_TRUE(p > 11250 && p < 11800);
}

// Inside the hysteresis band the anchor is left completely alone.
void test_hysteresis_leaves_a_consistent_estimate_alone(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);
    // At t=1400 the estimate is 10900; the speaker says 10, centre 10500,
    // err = -400, inside HYSTERESIS_MS. Nothing moves.
    feed(e, 10, 1400, 1400);
    TEST_ASSERT_EQUAL_INT32(10900, e.positionMs(1400));
    TEST_ASSERT_EQUAL_INT32(11000, e.positionMs(1500));
}

void test_frozen_while_paused(void) {
    Estimator e;
    feed(e, 30, 1000, 1000);
    e.tick(30, 240, false, 1000, 1200);                 // paused
    TEST_ASSERT_EQUAL_INT32(30700, e.positionMs(1200));
    TEST_ASSERT_EQUAL_INT32(30700, e.positionMs(9000)); // still there
}

void test_resumes_from_where_it_stopped(void) {
    Estimator e;
    feed(e, 30, 1000, 1000);
    e.tick(30, 240, false, 1000, 1200);      // pause at 30700
    e.tick(30, 240, true,  1000, 9000);      // resume much later
    TEST_ASSERT_EQUAL_INT32(30700, e.positionMs(9000));
    TEST_ASSERT_EQUAL_INT32(31200, e.positionMs(9500));
}

// Ten seconds without news and the clock stops. Wrong-and-moving is worse than
// stale-and-still.
void test_stops_when_readings_dry_up(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);
    TEST_ASSERT_EQUAL_INT32(19500, e.positionMs(10000));   // 9s later, still trusted
    TEST_ASSERT_EQUAL_INT32(20500, e.positionMs(11000));   // exactly at the limit
    TEST_ASSERT_EQUAL_INT32(20500, e.positionMs(15000));   // frozen
    TEST_ASSERT_EQUAL_INT32(20500, e.positionMs(60000));

    // And it picks straight back up when the speaker answers again.
    feed(e, 59, 60000, 60000);
    TEST_ASSERT_EQUAL_INT32(59500, e.positionMs(60000));
    TEST_ASSERT_EQUAL_INT32(60000, e.positionMs(60500));
}

void test_never_runs_past_the_end(void) {
    Estimator e;
    feed(e, 118, 1000, 1000, 120);
    TEST_ASSERT_EQUAL_INT32(120000, e.positionMs(6000));
}

void test_no_duration_still_tracks(void) {
    Estimator e;
    feed(e, 10, 1000, 1000, 0);        // a stream: no duration to clamp against
    TEST_ASSERT_EQUAL_INT32(15500, e.positionMs(6000));
}

// The panel's own seek shows immediately, and the reply already in flight -
// which describes the track BEFORE the seek - must not drag the bar back.
void test_local_seek_is_optimistic_and_ignores_the_stale_reply(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);

    e.onSeek(90, 1100);
    TEST_ASSERT_EQUAL_INT32(90000, e.positionMs(1100));

    feed(e, 11, 1300, 1300);                            // in flight before the seek
    TEST_ASSERT_EQUAL_INT32(90200, e.positionMs(1300)); // ignored

    // Once the settle window passes the speaker is back in charge - and by then
    // it has caught up with us (seeked to 90.0 at t=1100, so 91.6 at t=2700,
    // which it reports as "91"). Agreeing, it changes nothing: no lurch at the
    // end of the hold.
    feed(e, 91, 2700, 2700);
    TEST_ASSERT_EQUAL_INT32(91600, e.positionMs(2700));
}

// If the seek never took, the speaker wins as soon as the hold expires.
void test_a_failed_seek_is_corrected(void) {
    Estimator e;
    feed(e, 10, 1000, 1000);
    e.onSeek(90, 1100);
    feed(e, 12, 2700, 2700);
    TEST_ASSERT_EQUAL_INT32(12500, e.positionMs(2700));
}

void test_reset_forgets_the_track(void) {
    Estimator e;
    feed(e, 100, 1000, 1000);
    e.reset();
    TEST_ASSERT_FALSE(e.hasPosition());
    TEST_ASSERT_EQUAL_INT32(-1, e.positionMs(1000));

    // The same read_ms must be consumable again - a new track can be read at
    // the very millisecond the old one was.
    feed(e, 0, 1000, 1000);
    TEST_ASSERT_EQUAL_INT32(500, e.positionMs(1000));
}

// millis() wraps after 49 days. The panel is meant to run for months.
void test_survives_the_millis_rollover(void) {
    const uint32_t before = 0xFFFFFF00u;
    Estimator e;
    feed(e, 10, before, before);
    TEST_ASSERT_EQUAL_INT32(10500, e.positionMs(before));

    const uint32_t after = before + 500;        // wrapped
    TEST_ASSERT_EQUAL_INT32(11000, e.positionMs(after));

    feed(e, 11, after, after);
    TEST_ASSERT_EQUAL_INT32(11000, e.positionMs(after));   // consistent, left alone
    TEST_ASSERT_EQUAL_INT32(11200, e.positionMs(after + 200));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_until_first_reading);
    RUN_TEST(test_first_reading_anchors_mid_window);
    RUN_TEST(test_interpolates_between_readings);
    RUN_TEST(test_repeat_of_same_reading_does_not_re_anchor);
    RUN_TEST(test_position_is_monotonic_and_never_stalls);
    RUN_TEST(test_snaps_on_a_jump);
    RUN_TEST(test_snaps_backwards_too);
    RUN_TEST(test_eases_a_small_error);
    RUN_TEST(test_hysteresis_leaves_a_consistent_estimate_alone);
    RUN_TEST(test_frozen_while_paused);
    RUN_TEST(test_resumes_from_where_it_stopped);
    RUN_TEST(test_stops_when_readings_dry_up);
    RUN_TEST(test_never_runs_past_the_end);
    RUN_TEST(test_no_duration_still_tracks);
    RUN_TEST(test_local_seek_is_optimistic_and_ignores_the_stale_reply);
    RUN_TEST(test_a_failed_seek_is_corrected);
    RUN_TEST(test_reset_forgets_the_track);
    RUN_TEST(test_survives_the_millis_rollover);
    return UNITY_END();
}
