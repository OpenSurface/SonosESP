#pragma once
/**
 * Smooth playback position - the progress bar and the synced lyrics.
 *
 * The speaker is asked where it is with GetPositionInfo, and answers
 *
 *     <RelTime>0:02:15</RelTime>
 *
 * which is the whole truth about why the bar used to move in steps:
 *
 *   1. RelTime is truncated to the SECOND. Two readings 300ms apart report the
 *      same number, then one jumps by a full second.
 *   2. The readings are irregular. The polling task runs on a 300ms base tick,
 *      but it skips whole cycles while album art downloads, during the
 *      post-download SDIO cooldown, for a second after a track change and
 *      through a 500 storm - so gaps of one to several seconds are normal, by
 *      design, and that design is not negotiable (see the SDIO defence).
 *   3. updateUI() only ran when a Sonos event arrived, and at most every 200ms.
 *
 * So the panel keeps its own clock between readings. An anchor (a position and
 * the millis() it was true at) plus elapsed time gives a position at any
 * instant, at millisecond resolution, on whatever cadence the UI likes.
 *
 * The speaker stays the authority. Every reading is compared against the
 * estimate and the estimate is pulled back into line - but only when it is
 * provably wrong, because re-anchoring on each reading would reintroduce
 * exactly the one-second staircase this exists to remove:
 *
 *   - within HYSTERESIS_MS of the reading   leave it alone, keep running
 *   - out by less than SNAP_MS              ease it in over the next few
 *                                           readings, so nothing visibly jumps
 *   - out by SNAP_MS or more                snap: this is a seek from the Sonos
 *                                           app, a track change, or a stall,
 *                                           and pretending otherwise is a lie
 *                                           that lasts several seconds
 *
 * A reading of "135" means the true position is somewhere in [135, 136), so the
 * estimate aims for the MIDDLE of that window. Anchoring on the reported number
 * itself would sit half a second behind the music for ever.
 *
 * Free of Arduino, FreeRTOS and LVGL on purpose, so `pio test -e native` covers
 * it (test/test_position_estimate).
 */
#include <stdint.h>

namespace position_estimate {

// RelTime is whole seconds, so one reading covers a window this wide.
constexpr int32_t READING_SPAN_MS = 1000;

// How far the estimate may sit from the middle of that window before it is
// corrected at all. It has to exceed half the window (500ms) or every reading
// would re-anchor and the staircase comes straight back; the rest is slack for
// the SOAP round trip, which happens between the speaker sampling its position
// and us stamping the reply.
constexpr int32_t HYSTERESIS_MS = 750;

// At or past this, the position did not drift - it MOVED. Someone seeked in the
// app, the track changed, or playback stalled.
constexpr int32_t SNAP_MS = 1500;

// A correction smaller than SNAP_MS is spread over several readings instead of
// being applied at once: at the ~300ms poll cadence a quarter each time is gone
// inside about a second, and no single step is big enough to see.
constexpr int32_t EASE_DIVISOR = 4;

// Stop the clock when the readings dry up for this long. Something is wrong -
// polling held off far longer than its own guards allow, or the speaker has
// stopped answering - and a bar that keeps sliding on a ten-second-old guess is
// worse than one that waits, because it is confidently in the wrong place.
constexpr uint32_t CONFIDENCE_MS = 10000;

// After the panel's own seek, ignore readings for this long. The reply to a
// GetPositionInfo already in flight describes where the track was BEFORE the
// seek; consuming it would snap the bar back, then forward again a moment
// later.
constexpr uint32_t SEEK_SETTLE_MS = 1500;

class Estimator {
public:
    // Forget everything. For a track change, a room change, a disconnect -
    // anywhere the next reading describes different music.
    void reset() { *this = Estimator(); }

    // Feed the latest device state. Safe to call as often as the UI likes:
    // a reading is consumed once, identified by read_ms (the millis() at which
    // relTimeSeconds was written), so repeat calls between polls only
    // interpolate. read_ms == 0 means nothing has ever been read.
    void tick(int pos_sec, int duration_sec, bool playing,
              uint32_t read_ms, uint32_t now_ms) {
        duration_ms_ = duration_sec > 0 ? (int32_t)duration_sec * 1000 : 0;

        // Play/pause is read on its own cadence and must take effect at once:
        // the bar stops when the speaker says PAUSED, not at the next reading.
        // Re-anchoring on the current estimate is what freezes it in place -
        // and, on resume, what restarts the clock from where it stopped rather
        // than from where the last reading left it.
        if (playing != playing_) {
            if (has_anchor_) {
                anchor_ms_ = positionMs(now_ms);
                anchor_at_ = now_ms;
            }
            playing_ = playing;
        }

        if (read_ms == 0 || read_ms == last_read_ms_) return;   // nothing new

        // Read it before the estimate moves on: positionMs() depends on
        // fresh_at_, which this reading is about to update.
        const int32_t est = has_anchor_ ? positionMs(now_ms) : 0;

        last_read_ms_ = read_ms;

        // Our own seek is still settling - this reading predates it.
        if (has_anchor_ && (int32_t)(now_ms - seek_hold_until_) < 0) return;

        fresh_at_ = now_ms;

        // The middle of the window the reading describes: [pos, pos + 1s).
        const int32_t centre = (int32_t)pos_sec * 1000 + READING_SPAN_MS / 2;

        if (!has_anchor_) {
            has_anchor_ = true;
            anchor_ms_  = centre;
            anchor_at_  = now_ms;
            return;
        }

        const int32_t err = centre - est;
        if (err > -HYSTERESIS_MS && err < HYSTERESIS_MS) return;   // close enough

        anchor_ms_ = (err >= SNAP_MS || err <= -SNAP_MS)
                       ? centre                       // it moved; go there
                       : est + err / EASE_DIVISOR;    // it drifted; walk it in
        anchor_at_ = now_ms;
    }

    // The panel seeked. Show the destination immediately rather than waiting
    // out the command queue, the SOAP call and the next poll - roughly a second
    // in which the bar would otherwise spring back under the finger that just
    // moved it.
    void onSeek(int pos_sec, uint32_t now_ms) {
        has_anchor_      = true;
        anchor_ms_       = (int32_t)pos_sec * 1000;
        anchor_at_       = now_ms;
        fresh_at_        = now_ms;   // our own word counts as news, for now
        seek_hold_until_ = now_ms + SEEK_SETTLE_MS;
    }

    // Position in milliseconds, or -1 when nothing has been read yet.
    int32_t positionMs(uint32_t now_ms) const {
        if (!has_anchor_) return -1;

        int32_t p = anchor_ms_;
        if (playing_) {
            uint32_t clock_now = now_ms;
            if ((uint32_t)(now_ms - fresh_at_) > CONFIDENCE_MS) {
                clock_now = fresh_at_ + CONFIDENCE_MS;      // out of confidence
            }
            const int32_t elapsed = (int32_t)(clock_now - anchor_at_);
            if (elapsed > 0) p += elapsed;
        }

        if (p < 0) p = 0;
        if (duration_ms_ > 0 && p > duration_ms_) p = duration_ms_;
        return p;
    }

    bool hasPosition() const { return has_anchor_; }

private:
    bool     has_anchor_      = false;
    int32_t  anchor_ms_       = 0;   // estimated position at anchor_at_
    uint32_t anchor_at_       = 0;
    uint32_t last_read_ms_    = 0;   // read_ms of the reading already consumed
    uint32_t fresh_at_        = 0;   // when we last had news, for CONFIDENCE_MS
    uint32_t seek_hold_until_ = 0;
    bool     playing_         = false;
    int32_t  duration_ms_     = 0;
};

} // namespace position_estimate
