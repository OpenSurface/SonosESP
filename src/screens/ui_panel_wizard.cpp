/**
 * Panel confirmation wizard (7" only).
 *
 * GUITION ship different LCD panels under the same product code, and nothing in
 * firmware can tell them apart — same JD9165 controller, same 1024x600, same
 * chip ID. Reading the panel ID over DSI is not available either
 * (esp_lcd_panel_io_rx_param() hangs on ESP32-P4, espressif/esp-idf#15358).
 *
 * So the USER'S EYES are the detector, the same way a desktop OS confirms a
 * resolution change:
 *
 *   wrong panel  -> screen is blank/striped -> nobody taps -> we advance
 *   right panel  -> prompt is legible       -> they tap    -> we remember
 *
 * We never have to know which panel is fitted. This runs once, on first boot
 * (or when re-triggered from Settings), and costs nothing afterwards.
 *
 * Deliberately NOT solved with a second firmware build: that would double the
 * release artifacts, force the web installer to ask a question the user cannot
 * answer, and make OTA pick between images forever. One binary, one OTA path,
 * and a new panel revision is one row in JD9165_PANELS[].
 */

#include "ui_common.h"
#include "ui_fonts.h"

#if SCREEN_SIZE == 7
#include "../../lib/jd9165_lcd/jd9165_panels.h"

static bool     wiz_confirmed = false;
static lv_obj_t* wiz_screen   = nullptr;

static void wiz_confirm_cb(lv_event_t*) {
    wiz_confirmed = true;
}

// Returns true if the user confirmed the picture within the timeout.
static bool wizardAsk(uint8_t variant) {
    wiz_confirmed = false;
    wiz_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(wiz_screen, lv_color_hex(0x101010), 0);
    lv_obj_clear_flag(wiz_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Full-width reference bars. A wrong panel corrupts part of every scanline,
    // and the reported failures rendered the LEFT portion correctly - so a
    // centred button alone could sit in the readable half and get tapped by
    // mistake. These bars span edge to edge: if any of them is broken, striped
    // or missing, the picture is wrong and the user is told to wait.
    static const uint32_t bar_col[6] = {
        0xFFFFFF, 0xFF3B30, 0x34C759, 0x0A84FF, 0xFFD60A, 0xFFFFFF
    };
    for (int i = 0; i < 6; i++) {
        lv_obj_t* bar = lv_obj_create(wiz_screen);
        lv_obj_set_size(bar, SX(800 / 6), SY(26));
        lv_obj_set_pos(bar, SX((800 / 6) * i), SY(16));
        lv_obj_set_style_bg_color(bar, lv_color_hex(bar_col[i]), 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t* title = lv_label_create(wiz_screen);
    lv_label_set_text(title, "Can you read this?");
    lv_obj_set_style_text_font(title, &font_text_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SY(62));

    lv_obj_t* sub = lv_label_create(wiz_screen);
    lv_label_set_text(sub, "Tap the button to keep this display setting.\n"
                           "If the screen looks wrong, just wait.");
    lv_obj_set_style_text_font(sub, &font_text_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, SY(118));

    // Big target: this must be hittable, but not so easy to hit by accident
    // that a garbled screen gets confirmed by a stray touch.
    lv_obj_t* btn = lv_btn_create(wiz_screen);
    lv_obj_set_size(btn, SX(300), SY(90));
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, SY(20));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, wiz_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* blbl = lv_label_create(btn);
    lv_label_set_text(blbl, "Looks good");
    lv_obj_set_style_text_font(blbl, &font_text_24, 0);
    lv_obj_center(blbl);

    lv_obj_t* foot = lv_label_create(wiz_screen);
    lv_obj_set_style_text_font(foot, &font_text_14, 0);
    lv_obj_set_style_text_color(foot, lv_color_hex(0x777777), 0);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -SY(40));

    lv_screen_load(wiz_screen);

    const uint32_t start = millis();
    while (!wiz_confirmed) {
        uint32_t elapsed = millis() - start;
        if (elapsed >= PANEL_WIZARD_TIMEOUT_MS) break;
        lv_label_set_text_fmt(foot, "Display option %u of %u  -  trying the next one in %lus",
                              (unsigned)(variant + 1), (unsigned)JD9165_PANEL_COUNT,
                              (unsigned long)((PANEL_WIZARD_TIMEOUT_MS - elapsed) / 1000) + 1);
        lv_tick_inc(20);
        lv_timer_handler();
        // No esp_task_wdt_reset() here: loopTask is not subscribed to the task
        // watchdog during setup(), so it only logs "task not found" hundreds of
        // times. delay() yields to the idle task, which is what the WDT needs.
        delay(20);
    }
    return wiz_confirmed;
}

void runPanelWizard() {
    // Already settled — the overwhelmingly common path, costs one NVS read.
    if (wifiPrefs.getBool(NVS_KEY_PANEL_OK, false)) return;
    if (JD9165_PANEL_COUNT < 2) {                 // nothing to choose between
        wifiPrefs.putBool(NVS_KEY_PANEL_OK, true);
        return;
    }

    Serial.printf("[PANEL] Wizard: trying variant %d (%s)\n",
                  panel_variant, JD9165_PANELS[jd9165PanelClamp(panel_variant)].name);

    if (wizardAsk((uint8_t)panel_variant)) {
        wifiPrefs.putInt(NVS_KEY_PANEL_VAR, panel_variant);
        wifiPrefs.putBool(NVS_KEY_PANEL_OK, true);
        Serial.println("[PANEL] Confirmed by user");
        return;
    }

    // No confirmation: advance and reboot so the next init sequence is applied.
    int next = panel_variant + 1;
    if (next >= (int)JD9165_PANEL_COUNT) {
        // Cycled through everything with no answer — most likely nobody is
        // watching. Settle on the default rather than boot-looping forever;
        // Settings > Display can re-run this.
        Serial.println("[PANEL] No confirmation after a full cycle - settling on default");
        wifiPrefs.putInt(NVS_KEY_PANEL_VAR, PANEL_VARIANT_DEFAULT);
        wifiPrefs.putBool(NVS_KEY_PANEL_OK, true);
        return;
    }

    Serial.printf("[PANEL] No confirmation - trying variant %d next\n", next);
    wifiPrefs.putInt(NVS_KEY_PANEL_VAR, next);
    display_set_brightness(0);          // don't flash garbage on the way down
    delay(150);
    ESP.restart();
}

#else   // 4" ST7701 has only ever shipped with one panel
void runPanelWizard() {}
#endif
