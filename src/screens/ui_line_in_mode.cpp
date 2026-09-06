/**
 * UI Line-In Mode Handler
 * Dedicated UI for Sonos line-in (x-rincon-stream: URI).
 *
 * Left panel shows a large pulsing waveform icon with "LIVE AUDIO" label.
 * Right panel shows "Line In" title, source name, play/pause, and volume only.
 * All music-specific controls (progress, queue, shuffle, repeat, next-track) hidden.
 */

#include "ui_common.h"
#include "ui_theme.h"
#include "ui_icons.h"

static bool is_line_in_mode = false;

// Animation callback — fades the waveform icon opacity for a "breathing" live effect
static void _linein_anim_cb(void* obj, int32_t val) {
    lv_obj_set_style_text_opa((lv_obj_t*)obj, (lv_opa_t)val, 0);
}

void setLineInMode(bool enable) {
    if (is_line_in_mode == enable) return;
    is_line_in_mode = enable;

    if (enable) {
        Serial.println("[LINEIN UI] Switching to line-in mode");

        // ── Left panel: swap album art for waveform icon ─────────────────────
        if (img_blur_bg)     lv_obj_add_flag(img_blur_bg,     LV_OBJ_FLAG_HIDDEN);
        if (img_album)       lv_obj_add_flag(img_album,       LV_OBJ_FLAG_HIDDEN);
        if (art_placeholder) lv_obj_add_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);

        if (lbl_linein_icon) {
            lv_obj_clear_flag(lbl_linein_icon, LV_OBJ_FLAG_HIDDEN);
            // Breathing pulse: opacity 70 % → 100 % → 70 %, 1.2 s per half-cycle
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, lbl_linein_icon);
            lv_anim_set_exec_cb(&a, _linein_anim_cb);
            lv_anim_set_values(&a, LV_OPA_70, LV_OPA_COVER);
            lv_anim_set_duration(&a, 1200);
            lv_anim_set_playback_duration(&a, 1200);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        }
        if (lbl_linein_subtitle) lv_obj_clear_flag(lbl_linein_subtitle, LV_OBJ_FLAG_HIDDEN);

        // ── Right panel: hide all music-specific controls ────────────────────
        if (btn_prev)     lv_obj_add_flag(btn_prev,     LV_OBJ_FLAG_HIDDEN);
        if (btn_next)     lv_obj_add_flag(btn_next,     LV_OBJ_FLAG_HIDDEN);
        if (btn_queue)    lv_obj_add_flag(btn_queue,    LV_OBJ_FLAG_HIDDEN);
        if (btn_shuffle)  lv_obj_add_flag(btn_shuffle,  LV_OBJ_FLAG_HIDDEN);
        if (btn_repeat)   lv_obj_add_flag(btn_repeat,   LV_OBJ_FLAG_HIDDEN);

        if (slider_progress)   lv_obj_add_flag(slider_progress,   LV_OBJ_FLAG_HIDDEN);
        if (lbl_time)          lv_obj_add_flag(lbl_time,          LV_OBJ_FLAG_HIDDEN);
        if (lbl_time_remaining)lv_obj_add_flag(lbl_time_remaining, LV_OBJ_FLAG_HIDDEN);

        if (img_next_album)  lv_obj_add_flag(img_next_album,  LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_title)  lv_obj_add_flag(lbl_next_title,  LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_artist) lv_obj_add_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_header) lv_obj_add_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);

        if (lbl_album) lv_obj_add_flag(lbl_album, LV_OBJ_FLAG_HIDDEN);

        // Title: large, prominent, static
        if (lbl_title) {
            lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_CLIP);
            lv_label_set_text(lbl_title, "Line In");
        }
        // Artist: single-line, show source device name (filled by updateLineInUI)
        if (lbl_artist) {
            lv_label_set_long_mode(lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_height(lbl_artist, SY(20));
        }

    } else {
        Serial.println("[LINEIN UI] Leaving line-in mode");

        // ── Restore left panel ───────────────────────────────────────────────
        if (lbl_linein_icon) {
            lv_anim_delete(lbl_linein_icon, _linein_anim_cb);
            lv_obj_set_style_text_opa(lbl_linein_icon, LV_OPA_COVER, 0);
            lv_obj_add_flag(lbl_linein_icon,     LV_OBJ_FLAG_HIDDEN);
        }
        if (lbl_linein_subtitle) lv_obj_add_flag(lbl_linein_subtitle, LV_OBJ_FLAG_HIDDEN);

        if (img_album)       lv_obj_clear_flag(img_album,       LV_OBJ_FLAG_HIDDEN);
        if (art_placeholder) lv_obj_clear_flag(art_placeholder, LV_OBJ_FLAG_HIDDEN);
        if (img_blur_bg)     lv_obj_clear_flag(img_blur_bg,     LV_OBJ_FLAG_HIDDEN);

        // ── Restore right panel ──────────────────────────────────────────────
        if (btn_prev)     lv_obj_clear_flag(btn_prev,     LV_OBJ_FLAG_HIDDEN);
        if (btn_next)     lv_obj_clear_flag(btn_next,     LV_OBJ_FLAG_HIDDEN);
        if (btn_queue)    lv_obj_clear_flag(btn_queue,    LV_OBJ_FLAG_HIDDEN);
        if (btn_shuffle)  lv_obj_clear_flag(btn_shuffle,  LV_OBJ_FLAG_HIDDEN);
        if (btn_repeat)   lv_obj_clear_flag(btn_repeat,   LV_OBJ_FLAG_HIDDEN);

        if (slider_progress)   lv_obj_clear_flag(slider_progress,    LV_OBJ_FLAG_HIDDEN);
        if (lbl_time)          lv_obj_clear_flag(lbl_time,           LV_OBJ_FLAG_HIDDEN);
        if (lbl_time_remaining)lv_obj_clear_flag(lbl_time_remaining, LV_OBJ_FLAG_HIDDEN);

        if (img_next_album)  lv_obj_clear_flag(img_next_album,  LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_title)  lv_obj_clear_flag(lbl_next_title,  LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_artist) lv_obj_clear_flag(lbl_next_artist, LV_OBJ_FLAG_HIDDEN);
        if (lbl_next_header) lv_obj_clear_flag(lbl_next_header, LV_OBJ_FLAG_HIDDEN);

        if (lbl_album) lv_obj_clear_flag(lbl_album, LV_OBJ_FLAG_HIDDEN);

        // Restore the ACTIVE THEME's long mode, not a hardcoded one. Amber gives
        // the title a two-line truncating box; forcing scroll here left it
        // side-scrolling for the rest of the session after a single line-in play,
        // recoverable only by switching theme.
        const lv_label_long_mode_t lm = themeTitleLongMode();
        if (lbl_title)  lv_label_set_long_mode(lbl_title,  lm);
        if (lbl_artist) {
            lv_label_set_long_mode(lbl_artist, lm);
            lv_obj_set_height(lbl_artist, SY(20));
        }
    }
}

void updateLineInUI() {
    SonosDevice* dev = sonos.getCurrentDevice();
    if (!dev) return;

    if (!dev->isLineIn) {
        setLineInMode(false);
        return;
    }

    setLineInMode(true);

    // Subtitle: use track title if Sonos provides a device name, otherwise "Analog Input"
    String sourceName = dev->currentTrack.length() > 0 ? dev->currentTrack : "Analog Input";

    static String last_source;
    if (sourceName != last_source) {
        Serial.printf("[LINEIN UI] Source: '%s'\n", sourceName.c_str());
        last_source = sourceName;
        if (lbl_artist) lv_label_set_text(lbl_artist, sourceName.c_str());
    }
}
