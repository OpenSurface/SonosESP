/**
 * General Settings Screen — card-based dark theme.
 *
 * Lyrics and player-theme selection. Uses the shared card and row helpers from
 * ui_settings_card.h, so the control for each setting sits to the RIGHT of its
 * label rather than underneath it — see the row comment in that header.
 */

#include "ui_common.h"
#include "config.h"
#include "lyrics.h"
#include "ui_settings_card.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "amber.h"

// Forward declaration (defined in ui_sidebar.cpp)
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

void createGeneralScreen() {
    scr_general = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_general, AMB_BG, 0);

    // Sidebar — General is index 0
    lv_obj_t* content = createSettingsSidebar(scr_general, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(content, 0, 0);

    // ── Screen title ─────────────────────────────────────────────────────────
    addScreenHeader(content, "General", nullptr);

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Lyrics
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Lyrics");

        lv_obj_t* slot = addSettingRow(card, "Show synced lyrics",
                                       "Time-synced from LRCLIB. No API key needed.",
                                       false);
        lv_obj_t* sw_lyrics = addSwitch(slot, lyrics_enabled);
        lv_obj_add_event_cb(sw_lyrics, [](lv_event_t* e) {
            lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
            lyrics_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
            wifiPrefs.putBool("lyrics", lyrics_enabled);
            setLyricsVisible(lyrics_enabled && lyrics_ready);
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Theme  (issue #87)
    // Options and descriptions come straight from the THEMES[] registry, so
    // adding a theme in ui_theme.cpp appears here automatically.
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Theme");

        // The row's description IS the selected theme's description, so it is
        // retargeted from the dropdown's callback rather than being a separate
        // label under the control.
        lv_obj_t* slot = addSettingRow(card, "Player appearance",
                                       THEMES[active_theme].desc, false);
        static lv_obj_t* lbl_theme_desc;
        lbl_theme_desc = settingRowDesc(slot);

        // Build the option string from the registry.
        static char theme_opts[192];
        theme_opts[0] = '\0';
        for (uint8_t i = 0; i < THEME_COUNT; i++) {
            if (i) strncat(theme_opts, "\n", sizeof(theme_opts) - strlen(theme_opts) - 1);
            strncat(theme_opts, THEMES[i].name, sizeof(theme_opts) - strlen(theme_opts) - 1);
        }

        lv_obj_t* dd = lv_dropdown_create(slot);
        lv_dropdown_set_options(dd, theme_opts);
        lv_dropdown_set_selected(dd, active_theme);
        // Explicit width: the control slot sizes to its content, so a percentage
        // here would resolve against nothing.
        lv_obj_set_width(dd, SX(200));
        lv_obj_set_style_bg_color(dd, AMB_CARD, 0);
        lv_obj_set_style_text_color(dd, AMB_TEXT, 0);
        lv_obj_set_style_text_font(dd, &font_text_14, 0);
        lv_obj_set_style_border_color(dd, AMB_RAISED, 0);
        lv_obj_set_style_radius(dd, 8, 0);
        lv_obj_set_style_pad_all(dd, SMIN(10), 0);
        // Highlighted row in the OPEN list. Styling only the list leaves this to
        // LVGL's default (light) theme — dark list, white selection bar.
        lv_obj_set_style_bg_color(dd, AMB_RAISED, LV_PART_SELECTED);
        lv_obj_set_style_bg_color(dd, AMB_ACCENT,
            (lv_style_selector_t)((uint32_t)LV_PART_SELECTED | (uint32_t)LV_STATE_CHECKED));
        lv_obj_set_style_text_color(dd, AMB_TEXT, LV_PART_SELECTED);
        if (lv_obj_t* list = lv_dropdown_get_list(dd)) {
            lv_obj_set_style_bg_color(list, AMB_RAISED, 0);
            lv_obj_set_style_text_color(list, AMB_TEXT, 0);
            lv_obj_set_style_text_font(list, &font_text_14, 0);
            lv_obj_set_style_border_color(list, AMB_RAISED, 0);
        }
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            lv_obj_t* d = (lv_obj_t*)lv_event_get_target(e);
            uint8_t sel = (uint8_t)lv_dropdown_get_selected(d);
            // Rebuilds the player screen; safe here — we're on the main LVGL
            // thread and the settings screen (not scr_main) is displayed.
            themeSet(sel);
            if (lbl_theme_desc) lv_label_set_text(lbl_theme_desc, THEMES[active_theme].desc);
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }
}
