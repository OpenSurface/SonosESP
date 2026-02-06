/**
 * UI Radio Mode Handler
 * Adapts the UI when playing radio stations vs music tracks
 *
 * Radio detection uses the isRadioStation field from SonosDevice which is set
 * based on the track URI (x-sonosapi-stream:, x-rincon-mp3radio:, etc.)
 *
 * Station name comes from GetMediaInfo's CurrentURIMetaData (radioStationName field)
 * Current song info comes from r:streamContent parsed in updateTrackInfo()
 */

#include "ui_common.h"

// Track if we're currently in radio mode
static bool is_radio_mode = false;

// Adapt UI for radio mode - hide/show appropriate controls
void setRadioMode(bool enable) {
    if (is_radio_mode == enable) return; // Already in correct mode

    is_radio_mode = enable;

    if (enable) {
        Serial.println("[RADIO UI] Switching to radio mode");

        // Hide controls not applicable to radio
        if (btn_next) lv_obj_add_flag(btn_next, LV_OBJ_FLAG_HIDDEN);
        if (btn_prev) lv_obj_add_flag(btn_prev, LV_OBJ_FLAG_HIDDEN);
        if (btn_queue) lv_obj_add_flag(btn_queue, LV_OBJ_FLAG_HIDDEN);
        if (btn_shuffle) lv_obj_add_flag(btn_shuffle, LV_OBJ_FLAG_HIDDEN);
        if (btn_repeat) lv_obj_add_flag(btn_repeat, LV_OBJ_FLAG_HIDDEN);

        // Hide time/progress controls (radio has no duration)
        if (slider_progress) lv_obj_add_flag(slider_progress, LV_OBJ_FLAG_HIDDEN);
        if (lbl_time) lv_obj_add_flag(lbl_time, LV_OBJ_FLAG_HIDDEN);
        if (lbl_time_remaining) lv_obj_add_flag(lbl_time_remaining, LV_OBJ_FLAG_HIDDEN);

        // Hide next track info (no queue in radio)
        if (img_next_album) lv_obj_add_flag(img_next_album, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_title) lv_obj_add_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_artist) lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_header) lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);

    } else {
        Serial.println("[RADIO UI] Switching to music mode");

        // Show all music controls
        if (btn_next) lv_obj_clear_flag(btn_next, LV_OBJ_FLAG_HIDDEN);
        if (btn_prev) lv_obj_clear_flag(btn_prev, LV_OBJ_FLAG_HIDDEN);
        if (btn_queue) lv_obj_clear_flag(btn_queue, LV_OBJ_FLAG_HIDDEN);
        if (btn_shuffle) lv_obj_clear_flag(btn_shuffle, LV_OBJ_FLAG_HIDDEN);
        if (btn_repeat) lv_obj_clear_flag(btn_repeat, LV_OBJ_FLAG_HIDDEN);

        // Show time/progress controls
        if (slider_progress) lv_obj_clear_flag(slider_progress, LV_OBJ_FLAG_HIDDEN);
        if (lbl_time) lv_obj_clear_flag(lbl_time, LV_OBJ_FLAG_HIDDEN);
        if (lbl_time_remaining) lv_obj_clear_flag(lbl_time_remaining, LV_OBJ_FLAG_HIDDEN);

        // Show next track info
        if (img_next_album) lv_obj_clear_flag(img_next_album, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_title) lv_obj_clear_flag(lbl_next_title, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_artist) lv_obj_clear_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_header) lv_obj_clear_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);
    }
}

// Update UI based on current track type
// Call this at the END of updateUI() to ensure radio mode takes effect
void updateRadioModeUI() {
    SonosDevice* dev = sonos.getCurrentDevice();
    if (!dev) return;

    bool isRadio = dev->isRadioStation;
    setRadioMode(isRadio);

    if (!isRadio) return;

    // For radio: Use radioStationName for title if available
    // The currentTrack field may contain current song from streamContent
    // or it may contain URL junk - we need to be smart about this

    String displayTitle = "";
    String displayArtist = "";

    // Priority 1: Use radioStationName from GetMediaInfo (the actual station name)
    if (dev->radioStationName.length() > 0) {
        displayTitle = dev->radioStationName;
    }

    // Priority 2: If currentTrack looks valid (not URL junk), use it
    // This could be the current song from streamContent parsing
    if (dev->currentTrack.length() > 0) {
        String track = dev->currentTrack;
        bool isJunk = (track.indexOf("?") > 0 ||
                      track.indexOf(".mp3") > 0 ||
                      track.indexOf(".m3u8") > 0 ||
                      track.indexOf("accessKey=") > 0 ||
                      track.indexOf("index-cmaf") >= 0 ||
                      track.indexOf("index-ts") >= 0);

        if (!isJunk) {
            // If we have a station name, use currentTrack as the "now playing" info
            if (displayTitle.length() > 0) {
                // Station name is set, currentTrack might be the song
                // We could show both but for now, prioritize station name
                // The currentArtist will show the song info
            } else {
                // No station name from GetMediaInfo, use currentTrack
                displayTitle = track;
            }
        }
    }

    // Fallback: Generic label if nothing else works
    if (displayTitle.length() == 0) {
        displayTitle = "Radio Station";
    }

    // Artist: Use currentArtist if available, otherwise "Live Radio"
    if (dev->currentArtist.length() > 0) {
        displayArtist = dev->currentArtist;
    } else {
        displayArtist = "Live Radio";
    }

    // Update UI labels
    static String last_displayed_title = "";
    static String last_displayed_artist = "";

    // Only log when actually changing
    if (displayTitle != last_displayed_title || displayArtist != last_displayed_artist) {
        Serial.printf("[RADIO UI] Updating display - Title: '%s', Artist: '%s'\n",
                     displayTitle.c_str(), displayArtist.c_str());
        Serial.printf("[RADIO UI] Source data - StationName: '%s', CurrentTrack: '%s', CurrentArtist: '%s'\n",
                     dev->radioStationName.c_str(), dev->currentTrack.c_str(), dev->currentArtist.c_str());
        last_displayed_title = displayTitle;
        last_displayed_artist = displayArtist;
    }

    if (lbl_title) {
        lv_label_set_text(lbl_title, displayTitle.c_str());
    }
    if (lbl_artist) {
        lv_label_set_text(lbl_artist, displayArtist.c_str());
    }
}
