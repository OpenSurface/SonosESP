/**
 * UI TV Audio Mode Handler
 * Dedicated UI for Sonos soundbar TV audio (x-sonos-htastream: URI).
 *
 * Left panel shows a large pulsing television icon with "TV AUDIO" label.
 * Right panel shows "TV Audio" title, play/pause, and volume only.
 * All music-specific controls (progress, queue, shuffle, repeat, next-track) hidden.
 */

#include "ui_common.h"
#include "ui_theme.h"
#include "ui_icons.h"

static bool is_tv_audio_mode = false;

static void _tv_anim_cb(void* obj, int32_t val) {
    lv_obj_set_style_text_opa((lv_obj_t*)obj, (lv_opa_t)val, 0);
}

// Forget the latched state after themeSet() rebuilds the player widgets.
// See the fuller note at radioModeForget().
void tvAudioModeForget(void) { is_tv_audio_mode = false; }

void setTvAudioMode(bool enable) {
    if (is_tv_audio_mode == enable) return;
    is_tv_audio_mode = enable;

    if (enable) {
        Serial.println("[TV UI] Switching to TV audio mode");

        // ── Left panel: swap album art for TV icon ───────────────────────────
        if (img_blur_bg)     lv_obj_set_hidden(img_blur_bg, true);
        if (img_album)       lv_obj_set_hidden(img_album, true);
        if (art_placeholder) lv_obj_set_hidden(art_placeholder, true);

        if (lbl_tv_icon) {
            lv_obj_set_hidden(lbl_tv_icon, false);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, lbl_tv_icon);
            lv_anim_set_exec_cb(&a, _tv_anim_cb);
            lv_anim_set_values(&a, LV_OPA_70, LV_OPA_COVER);
            lv_anim_set_duration(&a, 1200);
            lv_anim_set_playback_duration(&a, 1200);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        }
        if (lbl_tv_subtitle) lv_obj_set_hidden(lbl_tv_subtitle, false);

        // ── Right panel: hide all music-specific controls ────────────────────
        if (btn_prev)     lv_obj_set_hidden(btn_prev, true);
        if (btn_next)     lv_obj_set_hidden(btn_next, true);
        if (btn_queue)    lv_obj_set_hidden(btn_queue, true);
        if (btn_shuffle)  lv_obj_set_hidden(btn_shuffle, true);
        if (btn_repeat)   lv_obj_set_hidden(btn_repeat, true);

        if (slider_progress)    lv_obj_set_hidden(slider_progress, true);
        if (lbl_time)           lv_obj_set_hidden(lbl_time, true);
        if (lbl_time_remaining) lv_obj_set_hidden(lbl_time_remaining, true);

        if (img_next_album)  lv_obj_set_hidden(img_next_album, true);
        if (lbl_next_title)  lv_obj_set_hidden(lbl_next_title, true);
        if (lbl_next_artist) lv_obj_set_hidden(lbl_next_artist, true);
        if (lbl_next_header) lv_obj_set_hidden(lbl_next_header, true);

        if (lbl_album) lv_obj_set_hidden(lbl_album, true);

        if (lbl_title) {
            lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_CLIP);
            lv_label_set_text(lbl_title, "TV Audio");
        }
        // Long mode only - the box belongs to the theme builder, and the exit
        // path asks it to restore everything (issue #177).
        if (lbl_artist) {
            lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_label_set_text(lbl_artist, "");
        }

    } else {
        Serial.println("[TV UI] Leaving TV audio mode");

        // ── Restore left panel ───────────────────────────────────────────────
        if (lbl_tv_icon) {
            lv_anim_delete(lbl_tv_icon, _tv_anim_cb);
            lv_obj_set_style_text_opa(lbl_tv_icon, LV_OPA_COVER, 0);
            lv_obj_set_hidden(lbl_tv_icon, true);
        }
        if (lbl_tv_subtitle) lv_obj_set_hidden(lbl_tv_subtitle, true);

        if (img_album)       lv_obj_set_hidden(img_album, false);
        if (art_placeholder) lv_obj_set_hidden(art_placeholder, false);
        if (img_blur_bg)     lv_obj_set_hidden(img_blur_bg, false);

        // ── Restore right panel ──────────────────────────────────────────────
        if (btn_prev)     lv_obj_set_hidden(btn_prev, false);
        if (btn_next)     lv_obj_set_hidden(btn_next, false);
        if (btn_queue)    lv_obj_set_hidden(btn_queue, false);
        if (btn_shuffle)  lv_obj_set_hidden(btn_shuffle, false);
        if (btn_repeat)   lv_obj_set_hidden(btn_repeat, false);

        if (slider_progress)    lv_obj_set_hidden(slider_progress, false);
        if (lbl_time)           lv_obj_set_hidden(lbl_time, false);
        if (lbl_time_remaining) lv_obj_set_hidden(lbl_time_remaining, false);

        if (img_next_album)  lv_obj_set_hidden(img_next_album, false);
        if (lbl_next_title)  lv_obj_set_hidden(lbl_next_title, false);
        if (lbl_next_artist) lv_obj_set_hidden(lbl_next_artist, false);
        if (lbl_next_header) lv_obj_set_hidden(lbl_next_header, false);

        if (lbl_album) lv_obj_set_hidden(lbl_album, false);

        // Hand the labels back to the theme that built them: position, size and
        // long mode together. Restoring only the title's long mode left the
        // artist carrying the TITLE's mode and Classic's height, on every theme
        // (issue #177).
        themeRestoreTrackLabels();
    }
}

void updateTvAudioUI() {
    SonosDevice* dev = sonos.getCurrentDevice();
    if (!dev) return;

    if (!dev->isTvAudio) {
        setTvAudioMode(false);
        return;
    }

    setTvAudioMode(true);
}
