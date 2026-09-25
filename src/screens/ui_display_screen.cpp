/**
 * Display Settings Screen — card-based dark theme.
 *
 * Brightness, auto-dim and the blurred-artwork backdrop. Uses the shared card
 * and row helpers from ui_settings_card.h: sliders carry their label and accent
 * value on one line with the track beneath (addSliderRow), and the backdrop
 * toggle sits to the right of its label (addSettingRow).
 */

#include "ui_common.h"
#include "ui_fonts.h"
#include "amber.h"
#include "ui_settings_card.h"
#include "ui_theme.h"
#include "display_driver.h"   // preview a night level without persisting it
#include "clock_screen.h"     // clock_12h - the time chips follow it

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ── Sliders persist on release, not on every step ───────────────────────────
// LVGL fires VALUE_CHANGED from update_knob_pos() on every pixel or two of drag,
// and Preferences::putInt() is nvs_set_i32() + nvs_commit() with no coalescing —
// one 32-byte NVS entry appended per step, the previous one marked erased. A full
// drag of "Auto-dim after" (range 0-300) therefore wrote ~300 entries in about a
// second, into a partition with roughly 500 usable slots, forcing a page
// compaction mid-gesture. Each compaction erases a 4KB sector under
// spi_flash_disable_interrupts_caches_and_other_cpu(): PSRAM cache off and the
// other core parked for tens of milliseconds. That is the same mechanism as the
// blue strobe during an OTA, and it was firing every time anyone touched a
// settings slider — visible as a stutter, and needless wear besides.
//
// The live preview and the value label stay on VALUE_CHANGED so dragging still
// responds immediately. Only the write moves to LV_EVENT_RELEASED, which fires
// once when the finger lifts.
struct SliderPersist { const char* key; const int* value; };

static void persistSliderCb(lv_event_t* e) {
    const SliderPersist* p = (const SliderPersist*)lv_event_get_user_data(e);
    wifiPrefs.putInt(p->key, *p->value);
}

// ── Night card helpers (issue #172) ─────────────────────────────────────────
// "21:00", or "9:00 PM" when the clock is set to 12-hour.
static void nightTimeText(char* out, size_t n, int minutes) {
    const int h = (minutes / 60) % 24, m = minutes % 60;
    if (!clock_12h) { snprintf(out, n, "%02d:%02d", h, m); return; }
    const int h12 = (h % 12) == 0 ? 12 : h % 12;
    snprintf(out, n, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
}

// 0 is not "0%": the backlight is off, and saying so is the point of allowing it.
static const char* nightLevelText(int level) {
    static char buf[8];
    if (level <= 0) snprintf(buf, sizeof(buf), "Off");
    else            snprintf(buf, sizeof(buf), "%d%%", level);
    return buf;
}

// The two ends of the window, as chips that step half an hour a tap and wrap at
// midnight. A roller each would be more precise than "about bedtime" needs, and
// this stays readable at arm's length in a dark room.
struct NightChip { int* minutes; const char* key; lv_obj_t* lbl; };
static NightChip s_night_from;
static NightChip s_night_to;

static void nightChipClicked(lv_event_t* e) {
    NightChip* c = (NightChip*)lv_event_get_user_data(e);
    *c->minutes = (*c->minutes + NIGHT_STEP_MIN) % (24 * 60);
    wifiPrefs.putInt(c->key, *c->minutes);
    char txt[12];
    nightTimeText(txt, sizeof(txt), *c->minutes);
    lv_label_set_text(c->lbl, txt);
}

static lv_obj_t* nightChip(lv_obj_t* parent, NightChip* state) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, LV_SIZE_CONTENT, SY(34));
    lv_obj_set_style_radius(b, SMIN(17), 0);
    lv_obj_set_style_bg_color(b, AMB_RAISED, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, AMB_BORDER, 0);
    lv_obj_set_style_border_color(b, AMB_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, SX(14), 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(b, nightChipClicked, LV_EVENT_CLICKED, state);

    char txt[12];
    nightTimeText(txt, sizeof(txt), *state->minutes);
    state->lbl = lv_label_create(b);
    lv_obj_set_style_text_font(state->lbl, &font_text_16, 0);
    lv_obj_set_style_text_color(state->lbl, AMB_TEXT, 0);
    lv_label_set_text(state->lbl, txt);
    return b;
}

// ============================================================================
// Display Settings Screen
// ============================================================================
void createDisplaySettingsScreen() {
    scr_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_display, AMB_BG, 0);

    // Create sidebar and get content area (Display is index 4)
    lv_obj_t* content = createSettingsSidebar(scr_display, 4);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(content, 0, 0);   // cards carry their own margin_bottom

    // ── Screen title ─────────────────────────────────────────────────────────
    addScreenHeader(content, "Display", nullptr);

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Brightness
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Brightness");

        // Screen brightness
        static lv_obj_t* lbl_brightness_val;
        lv_obj_t* row = addSliderRow(card, "Screen brightness", nullptr,
                                     true, &lbl_brightness_val);
        lv_label_set_text_fmt(lbl_brightness_val, "%d%%", brightness_level);

        lv_obj_t* slider_brightness = addSlider(row, 10, 100, brightness_level);
        lv_obj_add_event_cb(slider_brightness, [](lv_event_t* e) {
            lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
            int val = lv_slider_get_value(slider);
            // setBrightness() would persist on every step; drive the backlight
            // directly and let the RELEASED handler below do the writing.
            brightness_level = constrain(val, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
            display_set_brightness(brightness_level);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", val);
        }, LV_EVENT_VALUE_CHANGED, lbl_brightness_val);
        static SliderPersist p_brightness = { NVS_KEY_BRIGHTNESS, &brightness_level };
        lv_obj_add_event_cb(slider_brightness, persistSliderCb,
                            LV_EVENT_RELEASED, &p_brightness);

        // Dimmed brightness
        static lv_obj_t* lbl_dimmed_brightness_val;
        lv_obj_t* row_dim = addSliderRow(card, "Dimmed brightness",
                                         "Level the screen drops to once the auto-dim timer expires",
                                         false, &lbl_dimmed_brightness_val);
        lv_label_set_text_fmt(lbl_dimmed_brightness_val, "%d%%", brightness_dimmed);

        // Down to 1% on the 4" (issue #172); the 7" backlight cannot go below 5%.
        lv_obj_t* slider_dimmed_brightness = addSlider(row_dim, BRIGHTNESS_DIM_MIN, 50,
                                                       brightness_dimmed);
        lv_obj_add_event_cb(slider_dimmed_brightness, [](lv_event_t* e) {
            lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
            brightness_dimmed = lv_slider_get_value(slider);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", brightness_dimmed);
            // display_set_brightness(), NOT setBrightness(): the latter assigns
            // brightness_level and persists NVS_KEY_BRIGHTNESS, so previewing the
            // DIMMED level here would permanently overwrite the user's main
            // brightness with it. Same reasoning as the night slider below.
            if (screen_dimmed) display_set_brightness(brightness_dimmed);
        }, LV_EVENT_VALUE_CHANGED, lbl_dimmed_brightness_val);
        static SliderPersist p_dimmed = { NVS_KEY_BRIGHTNESS_DIM, &brightness_dimmed };
        lv_obj_add_event_cb(slider_dimmed_brightness, persistSliderCb,
                            LV_EVENT_RELEASED, &p_dimmed);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Night  (issue #172)
    // ------------------------------------------------------------------------
    // A bedside panel should not light a dark room. Between these hours the
    // screen rests at the night level instead of the dimmed one, and a touch
    // wakes it part way rather than to full brightness. Morning needs no action:
    // the window ends and everything behaves as before.
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Night");

        lv_obj_t* slot = addSettingRow(card, "Night hours",
                                       "A quieter screen between these times", false);
        lv_obj_t* sw_night = addSwitch(slot, night_enabled);
        lv_obj_add_event_cb(sw_night, [](lv_event_t* e) {
            lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
            night_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
            wifiPrefs.putBool(NVS_KEY_NIGHT_ON, night_enabled);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        // From / to, as two chips that step half an hour a tap. A roller for
        // each would be more precise than anyone needs for "about bedtime", and
        // this stays readable at arm's length in the dark.
        lv_obj_t* times = addSettingRow(card, "From / to",
                                        "Tap a time to move it half an hour", false);
        lv_obj_set_style_pad_column(times, SX(8), 0);
        s_night_from = { &night_from_min, NVS_KEY_NIGHT_FROM, nullptr };
        s_night_to   = { &night_to_min,   NVS_KEY_NIGHT_TO,   nullptr };
        nightChip(times, &s_night_from);
        addValueLabel(times, "→");
        nightChip(times, &s_night_to);

        static lv_obj_t* lbl_night_val;
        lv_obj_t* row_night = addSliderRow(card, "Night brightness",
                                           "Where the screen rests at night. 0 turns it off.",
                                           false, &lbl_night_val);
        lv_label_set_text(lbl_night_val, nightLevelText(night_level));
        lv_obj_t* sl_night = addSlider(row_night, 0, 20, night_level);
        lv_obj_add_event_cb(sl_night, [](lv_event_t* e) {
            lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
            night_level = lv_slider_get_value(s);
            lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), nightLevelText(night_level));
            // Preview it while dragging, but only if the screen is already
            // resting: overriding a wake would fight the user's own touch.
            // display_set_brightness() drives the backlight without touching
            // brightness_level or NVS, which is exactly what a preview wants.
            if (screen_dimmed) display_set_brightness(night_level);
        }, LV_EVENT_VALUE_CHANGED, lbl_night_val);
        static SliderPersist p_night = { NVS_KEY_NIGHT_LEVEL, &night_level };
        lv_obj_add_event_cb(sl_night, persistSliderCb, LV_EVENT_RELEASED, &p_night);

        static lv_obj_t* lbl_touch_val;
        lv_obj_t* row_touch = addSliderRow(card, "Touch at night wakes to",
                                           "The clock stays up. Tap again for the player.",
                                           false, &lbl_touch_val);
        lv_label_set_text_fmt(lbl_touch_val, "%d%%", night_touch_level);
        lv_obj_t* sl_touch = addSlider(row_touch, NIGHT_TOUCH_MIN, NIGHT_TOUCH_MAX,
                                       night_touch_level);
        lv_obj_add_event_cb(sl_touch, [](lv_event_t* e) {
            lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
            night_touch_level = lv_slider_get_value(s);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d%%", night_touch_level);
        }, LV_EVENT_VALUE_CHANGED, lbl_touch_val);
        static SliderPersist p_touch = { NVS_KEY_NIGHT_TOUCH, &night_touch_level };
        lv_obj_add_event_cb(sl_touch, persistSliderCb, LV_EVENT_RELEASED, &p_touch);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Auto-dim
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Auto-dim");

        static lv_obj_t* lbl_dim_timeout_val;
        lv_obj_t* row = addSliderRow(card, "Auto-dim after",
                                     "Idle time before the screen dims. 0 disables dimming.",
                                     false, &lbl_dim_timeout_val);
        lv_label_set_text_fmt(lbl_dim_timeout_val, "%d sec", autodim_timeout);

        lv_obj_t* slider_dim_timeout = addSlider(row, 0, 300, autodim_timeout);
        lv_obj_add_event_cb(slider_dim_timeout, [](lv_event_t* e) {
            lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
            autodim_timeout = lv_slider_get_value(slider);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e), "%d sec", autodim_timeout);
        }, LV_EVENT_VALUE_CHANGED, lbl_dim_timeout_val);
        // NVS_KEY_AUTODIM, not the bare "autodim_sec" this used to write: a key
        // spelled by hand is one rename away from silently losing the setting,
        // which is exactly how "brightness_dimmed" was lost once already.
        static SliderPersist p_autodim = { NVS_KEY_AUTODIM, &autodim_timeout };
        lv_obj_add_event_cb(slider_dim_timeout, persistSliderCb,
                            LV_EVENT_RELEASED, &p_autodim);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Player background  (issue #49)
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Player background");

        lv_obj_t* slot = addSettingRow(card, "Blurred album art",
                                       "Classic theme only - the others paint their own backdrop",
                                       false);
        lv_obj_t* sw_blur = addSwitch(slot, blur_bg_enabled);
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
