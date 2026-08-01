/**
 * General Settings Screen — card-based dark theme.
 *
 * Currently a single "Lyrics" card with the synced-lyrics toggle. Uses the
 * shared card helpers from ui_settings_card.h so this stays consistent with
 * the Clock settings screen.
 */

#include "ui_common.h"
#include "config.h"
#include "lyrics.h"
#include "ui_settings_card.h"
#include "ui_theme.h"
#include "ui_fonts.h"

// Forward declaration (defined in ui_sidebar.cpp)
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

void createGeneralScreen() {
    scr_general = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_general, lv_color_hex(0x121212), 0);

    // Sidebar — General is index 0
    lv_obj_t* content = createSettingsSidebar(scr_general, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(content, 0, 0);

    // ── Screen title ─────────────────────────────────────────────────────────
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, "General");
    lv_obj_set_style_text_font(lbl_title, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_pad_bottom(lbl_title, 12, 0);

    // ────────────────────────────────────────────────────────────────────────
    // CARD — Lyrics
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Lyrics");

        addSettingLabel(card, "Show synced lyrics");
        addDescLabel(card, "Display time-synced lyrics over the album art (LRCLIB, no API key)");

        lv_obj_t* sw_lyrics = addSwitch(card, lyrics_enabled);
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

        addSettingLabel(card, "Player appearance");

        // Description label reflects whichever theme is currently selected.
        static lv_obj_t* lbl_theme_desc;
        lbl_theme_desc = lv_label_create(card);
        lv_label_set_text(lbl_theme_desc, THEMES[active_theme].desc);
        lv_obj_set_style_text_font(lbl_theme_desc, &font_text_12, 0);
        lv_obj_set_style_text_color(lbl_theme_desc, COL_TEXT2, 0);
        lv_obj_set_width(lbl_theme_desc, lv_pct(100));
        lv_label_set_long_mode(lbl_theme_desc, LV_LABEL_LONG_WRAP);

        // Build the "A\nB\nC" option string from the registry.
        static char theme_opts[192];
        theme_opts[0] = '\0';
        for (uint8_t i = 0; i < THEME_COUNT; i++) {
            if (i) strncat(theme_opts, "\n", sizeof(theme_opts) - strlen(theme_opts) - 1);
            strncat(theme_opts, THEMES[i].name, sizeof(theme_opts) - strlen(theme_opts) - 1);
        }

        lv_obj_t* dd = lv_dropdown_create(card);
        lv_dropdown_set_options(dd, theme_opts);
        lv_dropdown_set_selected(dd, active_theme);
        lv_obj_set_width(dd, lv_pct(100));
        lv_obj_set_style_bg_color(dd, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_text_color(dd, COL_TEXT, 0);
        lv_obj_set_style_text_font(dd, &font_text_14, 0);
        lv_obj_set_style_border_color(dd, lv_color_hex(0x3A3A3A), 0);
        lv_obj_set_style_radius(dd, 8, 0);
        lv_obj_set_style_pad_all(dd, 10, 0);
        lv_obj_set_style_margin_top(dd, 4, 0);
        if (lv_obj_t* list = lv_dropdown_get_list(dd)) {
            lv_obj_set_style_bg_color(list, lv_color_hex(0x1F1F1F), 0);
            lv_obj_set_style_text_color(list, COL_TEXT, 0);
            lv_obj_set_style_text_font(list, &font_text_14, 0);
            lv_obj_set_style_border_color(list, lv_color_hex(0x3A3A3A), 0);
        }
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            lv_obj_t* d = (lv_obj_t*)lv_event_get_target(e);
            uint8_t sel = (uint8_t)lv_dropdown_get_selected(d);
            // Rebuilds the player screen; safe here — we're on the main LVGL
            // thread and the settings screen (not scr_main) is displayed.
            themeSet(sel);
            if (lbl_theme_desc) lv_label_set_text(lbl_theme_desc, THEMES[active_theme].desc);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        addDescLabel(card, "Applies immediately. Classic keeps the original blurred-art look.");
    }
}
