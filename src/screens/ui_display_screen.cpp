/**
 * Display Settings Screen — card-based dark theme.
 *
 * Was the odd one out: bare labels and full-bleed sliders straight on the screen
 * background while General and Clock used the grouped cards from
 * ui_settings_card.h. It now uses the same helpers, so the three settings screens
 * share one layout language — card, title, accent underline, controls inset by the
 * card's padding instead of running edge to edge.
 */

#include "ui_common.h"
#include "ui_fonts.h"
#include "ui_settings_card.h"
#include "ui_theme.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// Display Settings Screen
// ============================================================================
void createDisplaySettingsScreen() {
    scr_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_display, COL_SCREEN, 0);

    // Create sidebar and get content area (Display is index 4)
    lv_obj_t* content = createSettingsSidebar(scr_display, 4);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(content, 0, 0);   // cards carry their own margin_bottom

    // ── Screen title ─────────────────────────────────────────────────────────
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, "Display");
    lv_obj_set_style_text_font(lbl_title, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_pad_bottom(lbl_title, SY(12), 0);

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Brightness
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Brightness");

        // Screen brightness
        addSettingLabel(card, "Screen brightness");

        static lv_obj_t* lbl_brightness_val;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", brightness_level);
        lbl_brightness_val = addValueLabel(card, buf);

        lv_obj_t* slider_brightness = addSlider(card, 10, 100, brightness_level);
        lv_obj_add_event_cb(slider_brightness, [](lv_event_t* e) {
            lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
            int val = lv_slider_get_value(slider);
            setBrightness(val);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", val);
        }, LV_EVENT_VALUE_CHANGED, lbl_brightness_val);

        // Dimmed brightness
        addSettingLabel(card, "Dimmed brightness");
        addDescLabel(card, "Level the screen drops to once the auto-dim timer expires");

        static lv_obj_t* lbl_dimmed_brightness_val;
        snprintf(buf, sizeof(buf), "%d%%", brightness_dimmed);
        lbl_dimmed_brightness_val = addValueLabel(card, buf);

        lv_obj_t* slider_dimmed_brightness = addSlider(card, 5, 50, brightness_dimmed);
        lv_obj_add_event_cb(slider_dimmed_brightness, [](lv_event_t* e) {
            lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
            brightness_dimmed = lv_slider_get_value(slider);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", brightness_dimmed);
            wifiPrefs.putInt(NVS_KEY_BRIGHTNESS_DIM, brightness_dimmed);
            if (screen_dimmed) setBrightness(brightness_dimmed);
        }, LV_EVENT_VALUE_CHANGED, lbl_dimmed_brightness_val);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Auto-dim
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Auto-dim");

        addSettingLabel(card, "Auto-dim after");
        addDescLabel(card, "Idle time before the screen dims. 0 disables dimming.");

        static lv_obj_t* lbl_dim_timeout_val;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d sec", autodim_timeout);
        lbl_dim_timeout_val = addValueLabel(card, buf);

        lv_obj_t* slider_dim_timeout = addSlider(card, 0, 300, autodim_timeout);
        lv_obj_add_event_cb(slider_dim_timeout, [](lv_event_t* e) {
            lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
            autodim_timeout = lv_slider_get_value(slider);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d sec", autodim_timeout);
            wifiPrefs.putInt("autodim_sec", autodim_timeout);
        }, LV_EVENT_VALUE_CHANGED, lbl_dim_timeout_val);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Player background  (issue #49)
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Player background");

        addSettingLabel(card, "Blurred album art");
        addDescLabel(card,
                     "Fills the screen behind the player with a blurred copy of "
                     "the artwork. Used by the Classic theme; the others paint "
                     "their own backdrop.");

        lv_obj_t* sw_blur = addSwitch(card, blur_bg_enabled);
        lv_obj_add_event_cb(sw_blur, [](lv_event_t* e) {
            lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
            blur_bg_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
            wifiPrefs.putBool(NVS_KEY_BLUR_BG, blur_bg_enabled);

            if (!blur_bg_enabled) {
                // Hide immediately rather than waiting for the next track — the
                // backdrop is already on screen behind this settings page.
                if (img_blur_bg) lv_obj_add_flag(img_blur_bg, LV_OBJ_FLAG_HIDDEN);
            } else if (art_mutex && xSemaphoreTake(art_mutex, pdMS_TO_TICKS(50))) {
                // Re-publish the artwork we already hold so it comes straight back.
                // Gated on blur_bg_valid, not on the buffer pointer: blur_bg_buf is
                // allocated once and never freed, so a pointer check would republish
                // the previous track's blur (or uninitialised PSRAM).
                if (blur_bg_valid) blur_bg_ready = true;
                xSemaphoreGive(art_mutex);
            }
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }

// No panel-type control here on purpose. If the picture is good you can read
// this screen, so there is nothing to change; if it is bad you cannot reach it
// anyway. Offering the choice only creates a way to strand yourself, because a
// manual pick also marks the panel confirmed and stops the boot wizard from
// rescuing you. Detection is the wizard's job (see ui_panel_wizard.cpp).
}
