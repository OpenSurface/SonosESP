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
#include "display_driver.h"   // display_set_brightness()
#include "reboot_log.h"
#include <time.h>

// Forward declaration (defined in ui_sidebar.cpp)
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);
extern bool clock_12h;   // defined with the clock settings; declared here like the sidebar

// ── Recent restarts (reboot_log.h) ──────────────────────────────────────────
// One row per history slot, built once and filled on every visit. Filled late on
// purpose: this screen is created during boot, before NTP has synced and before
// the time zone is applied, so dates formatted at creation would be in UTC.
static lv_obj_t* s_rb_what[reboot_log::CAPACITY];
static lv_obj_t* s_rb_when[reboot_log::CAPACITY];
static lv_obj_t* s_rb_empty;

static void formatRebootWhen(char* buf, size_t n, uint32_t epoch) {
    if (epoch == reboot_log::EPOCH_UNKNOWN) { snprintf(buf, n, "Time unknown"); return; }
    const time_t t = (time_t)epoch;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, clock_12h ? "%b %d %I:%M %p" : "%b %d %H:%M", &tm);
}

static void refreshRebootList() {
    const reboot_log::Log& log = rebootLogHistory();
    for (int i = 0; i < reboot_log::CAPACITY; i++) {
        if (!s_rb_what[i]) return;
        lv_obj_t* row = lv_obj_get_parent(s_rb_what[i]);
        const reboot_log::Entry* e = reboot_log::at(log, i);
        if (!e) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(s_rb_what[i], reboot_log::label(*e));
        // Gold for the ones worth reporting: crashes, freezes, power dips and the
        // low-memory self-restarts. Amber has no red, and gold is already how this
        // UI says "look here".
        lv_obj_set_style_text_color(s_rb_what[i],
                                    reboot_log::unexpected(*e) ? AMB_ACCENT : AMB_TEXT2, 0);

        char when[32], up[16], line[56];
        formatRebootWhen(when, sizeof(when), e->epoch);
        if (e->uptime_s == reboot_log::UPTIME_UNKNOWN) {
            snprintf(line, sizeof(line), "%s", when);
        } else {
            reboot_log::formatUptime(up, sizeof(up), e->uptime_s);
            snprintf(line, sizeof(line), "%s, up %s", when, up);
        }
        lv_label_set_text(s_rb_when[i], line);
    }
    if (s_rb_empty) {
        if (log.count) lv_obj_add_flag(s_rb_empty, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_remove_flag(s_rb_empty, LV_OBJ_FLAG_HIDDEN);
    }
}

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

    // ────────────────────────────────────────────────────────────────────────
    // CARD - Device  (issue #159)
    //
    // Asked for so a wedged panel does not mean reaching behind the furniture
    // for the USB cable. It lives here rather than on Update because General is
    // the page people land on, and someone whose panel is misbehaving does not
    // go looking for "make it work again" under firmware updates.
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Device");

        lv_obj_t* slot = addSettingRow(card, "Restart",
                                       "Restarts the panel. Your Wi-Fi and speaker "
                                       "settings are kept.",
                                       true);

        // Arm-then-confirm, the same shape the queue's Clear button uses, rather
        // than a modal: one stray tap should not drop the music.
        lv_obj_t* btn = lv_button_create(slot);
        lv_obj_set_size(btn, SX(132), SY(40));
        lv_obj_set_style_radius(btn, SMIN(20), 0);
        lv_obj_set_style_bg_color(btn, AMB_RAISED, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, AMB_BORDER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "Restart");
        lv_obj_set_style_text_font(lbl, &font_text_14, 0);
        lv_obj_set_style_text_color(lbl, AMB_TEXT2, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            static uint32_t armed_ms = 0;
            lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
            lv_obj_t* l = lv_obj_get_child(b, 0);
            const bool armed = armed_ms && (millis() - armed_ms) < RESTART_ARM_MS;

            if (!armed) {
                armed_ms = millis();
                if (l) {
                    lv_label_set_text(l, "Tap to confirm");
                    lv_obj_set_style_text_color(l, AMB_ACCENT, 0);
                }
                lv_obj_set_style_border_color(b, AMB_ACCENT_DIM, 0);
                lv_obj_set_style_bg_color(b, AMB_ACCENT_WASH, 0);
                return;
            }

            armed_ms = 0;
            if (l) lv_label_set_text(l, "Restarting...");
            lv_refr_now(NULL);          // paint it before the screen goes dark

            // Quiesce the way the OTA path does before its own restart. The chip
            // reset itself does not need this, but a task caught mid-SDIO leaves
            // the C6 in a state the next boot has to recover from - which is the
            // failure the SDIO defence layers exist to avoid.
            Serial.println("[MAIN] Restart requested from Settings");
            rebootNoteCause(reboot_log::CAUSE_USER);
            sonos.suspendTasks();

            // Backlight off before the reset, not after. Between esp_restart()
            // and the boot screen's first paint the LCD controller sits in its
            // power-on state, which on these panels is a flat blue field - so a
            // restart flashed blue for a couple of seconds. The panel wizard and
            // the OTA path already do exactly this ("don't flash garbage on the
            // way down"); the Restart button simply had not copied them.
            display_set_brightness(0);
            vTaskDelay(pdMS_TO_TICKS(150));
            ESP.restart();
        }, LV_EVENT_CLICKED, NULL);

        // ── Recent restarts ─────────────────────────────────────────────────
        // Why the panel last went down, newest first. Before this it kept no
        // record at all: a panel restarting four times a day could not say why
        // unless a serial console happened to be attached at the time.
        addSettingRow(card, "Recent restarts",
                      "Newest first. The gold ones are worth reporting.", false);

        lv_obj_t* list = lv_obj_create(card);
        lv_obj_remove_style_all(list);
        lv_obj_set_width(list, lv_pct(100));
        lv_obj_set_height(list, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list, SY(6), 0);
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(list, LV_OBJ_FLAG_CLICKABLE);

        const int32_t line_h = lv_font_get_line_height(&font_text_14);
        for (int i = 0; i < reboot_log::CAPACITY; i++) {
            lv_obj_t* row = lv_obj_create(list);
            lv_obj_remove_style_all(row);
            lv_obj_set_width(row, lv_pct(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, SX(12), 0);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

            // DOT needs a bounded height as well as a width: given only a width,
            // an LVGL 9.5 label wraps and grows instead of truncating.
            lv_obj_t* what = lv_label_create(row);
            lv_obj_set_style_text_font(what, &font_text_14, 0);
            lv_obj_set_style_text_color(what, AMB_TEXT2, 0);
            lv_obj_set_flex_grow(what, 1);
            lv_obj_set_height(what, line_h);
            lv_label_set_long_mode(what, LV_LABEL_LONG_DOT);
            lv_label_set_text(what, "");

            lv_obj_t* when = lv_label_create(row);
            lv_obj_set_style_text_font(when, &font_text_12, 0);
            lv_obj_set_style_text_color(when, AMB_TEXT3, 0);
            lv_label_set_text(when, "");

            s_rb_what[i] = what;
            s_rb_when[i] = when;
        }
        s_rb_empty = addDescLabel(list, "Nothing recorded yet.");
    }

    // Refilled on every visit - see refreshRebootList().
    lv_obj_add_event_cb(scr_general, [](lv_event_t*) { refreshRebootList(); },
                        LV_EVENT_SCREEN_LOAD_START, NULL);
    refreshRebootList();
}
