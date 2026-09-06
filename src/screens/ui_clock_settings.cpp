/**
 * Clock Settings Screen — card-based dark theme.
 *
 * Sections grouped into 4 cards (Display, Photo Background, Time Zone, Weather)
 * for visual hierarchy and easier scanning vs the previous flat list. Same NVS
 * keys, same callbacks — no behavior change for existing settings.
 *
 * Adds optional manual location override (issue #74) via a 3-option method
 * dropdown inside the Weather card:
 *   - Auto-detect from IP
 *   - Predefined city  (reveals the existing city list as a sub-dropdown)
 *   - Custom coordinates  (reveals lat/lon textareas)
 * Method is encoded in the existing clock_weather_city_idx — 0 = Auto,
 * 1..CLOCK_CITY_COUNT-1 = predefined, CLOCK_LOC_CUSTOM_IDX = custom.
 *
 * Coords are written to NVS by the main thread only; fetchClockWeather() reads
 * the atomic float globals (clock_custom_lat/lon) — never touches NVS itself,
 * since clockBgTask has a PSRAM stack which would crash on the cache-disable
 * assert during a flash op.
 */

#include "ui_common.h"
#include "config.h"
#include "clock_screen.h"
#include "clock_face.h"
#include "ui_settings_card.h"  // shared card helpers: addCard, addSettingLabel, addDescLabel, addSwitch
#include "ui_fonts.h"
#include "amber.h"

// Forward declaration (defined in ui_sidebar.cpp)
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ─────────────────────────────────────────────────────────────────────────────
// Clock-specific theme tokens (form inputs + keyboard, not in the shared header
// because only this screen uses them so far)
// ─────────────────────────────────────────────────────────────────────────────
#define CLK_INPUT_BG    AMB_RAISED
#define CLK_INPUT_BORD  AMB_RAISED
#define CLK_KB_BG       AMB_PANEL
#define CLK_KB_KEY      AMB_CARD
#define CLK_KB_KEY_BORD AMB_RAISED

// Location method indices (UI-level — derived from clock_weather_city_idx)
#define LOC_METHOD_AUTO    0
#define LOC_METHOD_CITY    1
#define LOC_METHOD_CUSTOM  2

static lv_obj_t* makeDropdown(lv_obj_t* parent, const char* options,
                              uint16_t selected, bool open_up) {
    lv_obj_t* dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, selected);
    lv_obj_set_width(dd, lv_pct(100));
    lv_obj_set_style_bg_color(dd, CLK_INPUT_BG, 0);
    lv_obj_set_style_text_color(dd, AMB_TEXT, 0);
    lv_obj_set_style_text_font(dd, &font_text_14, 0);
    lv_obj_set_style_border_color(dd, CLK_INPUT_BORD, 0);
    lv_obj_set_style_radius(dd, 8, 0);
    lv_obj_set_style_pad_all(dd, SMIN(10), 0);
    if (open_up) lv_dropdown_set_dir(dd, LV_DIR_TOP);
    // The highlighted row in the OPEN list is LV_PART_SELECTED. Styling only the
    // list (below) leaves this part to LVGL's default theme, which is light — so
    // the list renders dark with a white selection bar. See ui_ota_screen.cpp.
    lv_obj_set_style_bg_color(dd, AMB_RAISED, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(dd, AMB_ACCENT,
        (lv_style_selector_t)((uint32_t)LV_PART_SELECTED | (uint32_t)LV_STATE_CHECKED));
    lv_obj_set_style_text_color(dd, AMB_TEXT, LV_PART_SELECTED);
    lv_obj_t* list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_height(list, SY(260));
        lv_obj_set_style_bg_color(list, AMB_RAISED, 0);
        lv_obj_set_style_text_color(list, AMB_TEXT, 0);
        lv_obj_set_style_text_font(list, &font_text_14, 0);
        lv_obj_set_style_border_color(list, CLK_INPUT_BORD, 0);
    }
    return dd;
}

// Slider styling now lives in addSlider() (ui_settings_card.cpp) so Display and
// Clock cannot drift apart again; this stays as a thin alias for the call sites.
static inline lv_obj_t* makeSlider(lv_obj_t* parent, int min, int max, int value) {
    return addSlider(parent, min, max, value);
}

static lv_obj_t* makeTextarea(lv_obj_t* parent, const char* initial,
                              const char* accept_chars, int max_len,
                              const char* placeholder) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    if (initial && initial[0]) lv_textarea_set_text(ta, initial);
    if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    if (accept_chars) lv_textarea_set_accepted_chars(ta, accept_chars);
    if (max_len > 0) lv_textarea_set_max_length(ta, max_len);
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_set_style_bg_color(ta, CLK_INPUT_BG, 0);
    lv_obj_set_style_text_color(ta, AMB_TEXT, 0);
    lv_obj_set_style_text_font(ta, &font_text_14, 0);
    lv_obj_set_style_border_color(ta, CLK_INPUT_BORD, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_pad_all(ta, SMIN(10), 0);
    return ta;
}

// ─────────────────────────────────────────────────────────────────────────────
// Location method UI — state shared across callbacks
// ─────────────────────────────────────────────────────────────────────────────
static lv_obj_t* city_sub_container  = nullptr;  // wraps "City" label + dropdown
static lv_obj_t* dd_city             = nullptr;
static lv_obj_t* custom_loc_card     = nullptr;
static lv_obj_t* custom_lat_ta       = nullptr;
static lv_obj_t* custom_lon_ta       = nullptr;
static lv_obj_t* custom_name_ta      = nullptr;
static lv_obj_t* custom_kb           = nullptr;
static lv_obj_t* settings_scrollable = nullptr;  // content area returned by sidebar — used for scroll-into-view
static lv_obj_t* bottom_spacer       = nullptr;  // grows when kb appears so the focused field can scroll above it

// Show/hide the sub-controls based on the selected method.
static void update_location_method_visibility(int method) {
    if (city_sub_container) {
        if (method == LOC_METHOD_CITY) lv_obj_clear_flag(city_sub_container, LV_OBJ_FLAG_HIDDEN);
        else                            lv_obj_add_flag(city_sub_container,  LV_OBJ_FLAG_HIDDEN);
    }
    if (custom_loc_card) {
        if (method == LOC_METHOD_CUSTOM) lv_obj_clear_flag(custom_loc_card, LV_OBJ_FLAG_HIDDEN);
        else                              lv_obj_add_flag(custom_loc_card,  LV_OBJ_FLAG_HIDDEN);
    }
    if (method != LOC_METHOD_CUSTOM && custom_kb) {
        lv_obj_add_flag(custom_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// Persist current textarea contents (called on DEFOCUS).
static void persist_custom_loc_from_textareas() {
    if (!custom_lat_ta || !custom_lon_ta) return;
    const char* lat_s  = lv_textarea_get_text(custom_lat_ta);
    const char* lon_s  = lv_textarea_get_text(custom_lon_ta);
    // Update atomic floats first (read lock-free by bg task)
    clock_custom_lat = (float)atof(lat_s);
    clock_custom_lon = (float)atof(lon_s);
    // Persist text form to NVS (mainAppTask = internal SRAM stack, NVS-safe)
    wifiPrefs.putString(NVS_KEY_CLOCK_WX_CUSTOM_LAT, lat_s);
    wifiPrefs.putString(NVS_KEY_CLOCK_WX_CUSTOM_LON, lon_s);
    // Optional display name — copy to fixed buffer (byte-level torn read by bg task = display
    // cosmetic only) and persist. Empty string is fine; fetchClockWeather falls back to "lat, lon".
    if (custom_name_ta) {
        const char* name_s = lv_textarea_get_text(custom_name_ta);
        strlcpy(clock_custom_name, name_s, sizeof(clock_custom_name));
        wifiPrefs.putString(NVS_KEY_CLOCK_WX_CUSTOM_NAME, name_s);
    }
    clock_wx_valid = false;
    clock_weather_needs_refetch = true;
    Serial.printf("[CLOCK] Custom location saved: %.4f, %.4f (%s)\n",
                  (double)clock_custom_lat, (double)clock_custom_lon,
                  clock_custom_name[0] ? clock_custom_name : "no name");
}

// Sum the Y coords from `obj` up the parent chain until (but not including) `ancestor`.
static int relative_y_to(lv_obj_t* obj, lv_obj_t* ancestor) {
    int y = 0;
    lv_obj_t* o = obj;
    while (o && o != ancestor) {
        y += lv_obj_get_y(o);
        o = lv_obj_get_parent(o);
    }
    return y;
}

// ── Custom numeric keypad map ──────────────────────────────────────────────
//   7  8  9  ⌫
//   4  5  6  -
//   1  2  3  .
//   [    0    ] ✕
// Buttons: digits, minus, decimal, backspace, close. No OK/validate.
static const char* num_kb_map[] = {
    "7", "8", "9", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", "-",                  "\n",
    "1", "2", "3", ".",                  "\n",
    "0", LV_SYMBOL_CLOSE, ""
};
// Width units per row (must sum to the same total per row). Row 4: "0" spans 3 cells.
// W() macro casts ints to the lv_buttonmatrix_ctrl_t enum for C++ strict typing.
#define W(n) (lv_buttonmatrix_ctrl_t)(n)
static const lv_buttonmatrix_ctrl_t num_kb_ctrl[] = {
    W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1),
    W(3), W(1)
};
#undef W

// Focus / defocus handler for the custom-coords textareas.
// Numeric textareas (lat/lon) use the compact 4x4 keypad docked bottom-right.
// The name textarea uses the built-in alpha keyboard (full-width, docked bottom-center).
static void custom_ta_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (!custom_kb) return;
    if (code == LV_EVENT_FOCUSED) {
        bool is_name = (ta == custom_name_ta);
        // Both keyboards dock to the same bottom-center position and share the same
        // dark style; they only differ in size + mode (numeric pad vs alpha layout).
        if (is_name) {
            lv_keyboard_set_mode(custom_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
            lv_obj_set_size(custom_kb, SX(760), SY(230));
        } else {
            lv_keyboard_set_mode(custom_kb, LV_KEYBOARD_MODE_USER_1);
            lv_obj_set_size(custom_kb, SX(340), SY(200));
        }
        lv_obj_align(custom_kb, LV_ALIGN_BOTTOM_MID, 0, SY(-10));

        // Grow the bottom spacer so the scrollable has enough range to actually
        // move the focused field above the keyboard. Without this, lv_obj_scroll_to_y
        // is clamped to max_scroll (which is too small when the focused field is
        // already near the bottom of content) and the field stays behind the kb.
        if (bottom_spacer) {
            lv_obj_set_height(bottom_spacer, SY(260));
            if (settings_scrollable) lv_obj_update_layout(settings_scrollable);
        }

        lv_keyboard_set_textarea(custom_kb, ta);
        lv_obj_clear_flag(custom_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(custom_kb);

        // Scroll the focused textarea above the keyboard. Both keyboards now sit
        // bottom-center with their top edge around y=250, so the same scroll target
        // works for both (lands the field ~110px from the top of the visible area).
        if (settings_scrollable) {
            int ta_y  = relative_y_to(ta, settings_scrollable);
            int target = ta_y - 110;
            if (target < 0) target = 0;
            lv_obj_scroll_to_y(settings_scrollable, target, LV_ANIM_ON);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        persist_custom_loc_from_textareas();
        lv_obj_add_flag(custom_kb, LV_OBJ_FLAG_HIDDEN);
        // Collapse the spacer so the screen doesn't show empty space at the bottom.
        if (bottom_spacer) lv_obj_set_height(bottom_spacer, 0);
    }
}

// Close-button (LV_SYMBOL_CLOSE) on the keyboard fires LV_EVENT_CANCEL.
// Save the current values and clear the textarea focus state so the NEXT tap
// on a textarea fires FOCUSED again (and re-shows the keyboard).
static void kb_close_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CANCEL && code != LV_EVENT_READY) return;
    persist_custom_loc_from_textareas();
    lv_obj_add_flag(custom_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* ta = lv_keyboard_get_textarea(custom_kb);
    if (ta) {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        lv_obj_clear_state(ta, LV_STATE_FOCUS_KEY);
    }
    lv_keyboard_set_textarea(custom_kb, NULL);
}

// Apply dark-mode styling to the keyboard.
static void style_keyboard_dark(lv_obj_t* kb) {
    // Main background — slightly larger radius so the floating panel has a card feel
    lv_obj_set_style_bg_color(kb, CLK_KB_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(kb, SET_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, SMIN(6), LV_PART_MAIN);
    lv_obj_set_style_pad_gap(kb, SMIN(4), LV_PART_MAIN);
    lv_obj_set_style_radius(kb, 12, LV_PART_MAIN);

    // Key buttons
    lv_obj_set_style_bg_color(kb, CLK_KB_KEY, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, AMB_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &font_text_16, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, CLK_KB_KEY_BORD, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);

    // Pressed-key feedback
    lv_obj_set_style_bg_color(kb, AMB_ACCENT,
        (lv_style_selector_t)((uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_PRESSED));
    lv_obj_set_style_text_color(kb, AMB_ON_ACCENT,
        (lv_style_selector_t)((uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_PRESSED));
}

// ============================================================================
// createClockSettingsScreen
// ============================================================================
void createClockSettingsScreen() {
    scr_clock_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_clock_settings, AMB_BG, 0);

    // Sidebar — Clock is index 6
    lv_obj_t* content = createSettingsSidebar(scr_clock_settings, 6);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(content, 0, 0);
    settings_scrollable = content;  // remember for scroll-into-view on keyboard focus

    // ── Screen title ─────────────────────────────────────────────────────────
    addScreenHeader(content, "Clock", nullptr);

    // ────────────────────────────────────────────────────────────────────────
    // CARD 1 — Display
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Display");

        lv_obj_t* slot_mode = addSettingRow(card, "Activate clock",
            "When the panel should fall back to a clock face", true);
        // Shorter labels. "After inactivity (nothing playing)" is 34 characters and
        // did not fit 206px, so the closed dropdown drew its text straight over
        // the chevron. The bracketed qualifier is what matters; "After
        // inactivity" was repeated on three of the four and carried nothing.
        lv_obj_t* dd_mode = makeDropdown(slot_mode,
            "Disabled\n"
            "On inactivity\n"
            "When paused\n"
            "When stopped",
            (uint16_t)clock_mode, false);
        lv_obj_set_width(dd_mode, SX(206));
        lv_obj_add_event_cb(dd_mode, [](lv_event_t* e) {
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            clock_mode = (int)lv_dropdown_get_selected(dd);
            wifiPrefs.putInt(NVS_KEY_CLOCK_MODE, clock_mode);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        static lv_obj_t* lbl_timeout_val;
        lv_obj_t* row_timeout = addSliderRow(card, "Inactivity timeout", nullptr,
                                             true, &lbl_timeout_val);
        lv_label_set_text_fmt(lbl_timeout_val, "%d min", clock_timeout_min);

        lv_obj_t* sl_timeout = makeSlider(row_timeout, 1, 60, clock_timeout_min);
        lv_obj_add_event_cb(sl_timeout, [](lv_event_t* e) {
            lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
            clock_timeout_min = lv_slider_get_value(s);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e),
                                  "%d min", clock_timeout_min);
            wifiPrefs.putInt(NVS_KEY_CLOCK_TIMEOUT, clock_timeout_min);
        }, LV_EVENT_VALUE_CHANGED, lbl_timeout_val);

        lv_obj_t* slot_12h = addSettingRow(card, "12-hour format",
                                           "Off = 24-hour clock", true);
        lv_obj_t* sw_12h = addSwitch(slot_12h, clock_12h);
        lv_obj_add_event_cb(sw_12h, [](lv_event_t* e) {
            lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
            clock_12h = lv_obj_has_state(sw, LV_STATE_CHECKED);
            wifiPrefs.putBool(NVS_KEY_CLOCK_12H, clock_12h);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        // Options and description come from the CLOCK_FACES[] registry, so a new
        // face appears here automatically (see clock_face.h).
        lv_obj_t* slot_face = addSettingRow(card, "Face", clockFaceCurrent()->desc, false);
        lv_obj_t* lbl_face_desc = settingRowDesc(slot_face);
        lv_obj_t* dd_style = makeDropdown(slot_face, clockFaceOptions(),
                                          (uint16_t)clock_style, false);
        lv_obj_set_width(dd_style, SX(206));
        lv_obj_set_user_data(dd_style, lbl_face_desc);
        lv_obj_add_event_cb(dd_style, [](lv_event_t* e) {
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            clock_style = (int)lv_dropdown_get_selected(dd);
            clockFaceClampStyle();
            wifiPrefs.putInt(NVS_KEY_CLOCK_STYLE, clock_style);
            lv_obj_t* desc = (lv_obj_t*)lv_obj_get_user_data(dd);
            if (desc) lv_label_set_text(desc, clockFaceCurrent()->desc);
            clockStyleChanged();   // swap faces on the already-built clock screen
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD 2 — Photo Background
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Photo Background");

        lv_obj_t* slot_picsum = addSettingRow(card, "Enable random photos",
            "Random photos from Flickr via loremflickr.com (requires WiFi)", true);
        lv_obj_t* sw_picsum = addSwitch(slot_picsum, clock_picsum_enabled);
        lv_obj_add_event_cb(sw_picsum, [](lv_event_t* e) {
            lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
            clock_picsum_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
            wifiPrefs.putBool(NVS_KEY_CLOCK_PICSUM, clock_picsum_enabled);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_t* slot_kw = addSettingRow(card, "Photo theme", nullptr, true);
        static char kw_opts[256];
        kw_opts[0] = '\0';
        for (int i = 0; i < CLOCK_BG_KW_COUNT; i++) {
            strncat(kw_opts, CLOCK_BG_KEYWORDS[i].label, sizeof(kw_opts) - strlen(kw_opts) - 2);
            if (i < CLOCK_BG_KW_COUNT - 1)
                strncat(kw_opts, "\n", sizeof(kw_opts) - strlen(kw_opts) - 1);
        }
        lv_obj_t* dd_kw = makeDropdown(slot_kw, kw_opts, (uint16_t)clock_bg_kw_idx, false);
        lv_obj_set_width(dd_kw, SX(206));
        lv_obj_add_event_cb(dd_kw, [](lv_event_t* e) {
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            clock_bg_kw_idx = (int)lv_dropdown_get_selected(dd);
            wifiPrefs.putInt(NVS_KEY_CLOCK_KW, clock_bg_kw_idx);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        static lv_obj_t* lbl_refresh_val;
        lv_obj_t* row_refresh = addSliderRow(card, "Photo refresh interval", nullptr,
                                             false, &lbl_refresh_val);
        lv_label_set_text_fmt(lbl_refresh_val, "%d min", clock_refresh_min);

        lv_obj_t* sl_refresh = makeSlider(row_refresh, 1, 60, clock_refresh_min);
        lv_obj_add_event_cb(sl_refresh, [](lv_event_t* e) {
            lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
            clock_refresh_min = lv_slider_get_value(s);
            lv_label_set_text_fmt((lv_obj_t*)lv_event_get_user_data(e),
                                  "%d min", clock_refresh_min);
            wifiPrefs.putInt(NVS_KEY_CLOCK_REFRESH, clock_refresh_min);
        }, LV_EVENT_VALUE_CHANGED, lbl_refresh_val);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD 3 — Time Zone
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Time Zone");

        lv_obj_t* slot_tz = addSettingRow(card, "Timezone",
                                          "Used for the clock and for sunrise/sunset", false);
        static char tz_opts[4096];
        tz_opts[0] = '\0';
        for (int i = 0; i < CLOCK_ZONES_COUNT; i++) {
            strncat(tz_opts, CLOCK_ZONES[i].name, sizeof(tz_opts) - strlen(tz_opts) - 2);
            if (i < CLOCK_ZONES_COUNT - 1) {
                strncat(tz_opts, "\n", sizeof(tz_opts) - strlen(tz_opts) - 1);
            }
        }
        lv_obj_t* dd_tz = makeDropdown(slot_tz, tz_opts, (uint16_t)clock_tz_idx, true);
        lv_obj_set_width(dd_tz, SX(240));
        lv_obj_add_event_cb(dd_tz, [](lv_event_t* e) {
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            clock_tz_idx = (int)lv_dropdown_get_selected(dd);
            wifiPrefs.putInt(NVS_KEY_CLOCK_TZ, clock_tz_idx);
            setenv("TZ", CLOCK_ZONES[clock_tz_idx].posix, 1);
            tzset();
            Serial.printf("[CLOCK] TZ set to %s (%s)\n",
                          CLOCK_ZONES[clock_tz_idx].name,
                          CLOCK_ZONES[clock_tz_idx].posix);
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // ────────────────────────────────────────────────────────────────────────
    // CARD 4 — Weather  (with method dropdown + reveal panels per #74)
    // ────────────────────────────────────────────────────────────────────────
    {
        lv_obj_t* card = addCard(content, "Weather");

        lv_obj_t* slot_wx = addSettingRow(card, "Enable widget",
            "Temperature, humidity, wind and a 6-hour forecast (Open-Meteo, no API key)", true);
        lv_obj_t* sw_weather = addSwitch(slot_wx, clock_weather_enabled);
        lv_obj_add_event_cb(sw_weather, [](lv_event_t* e) {
            lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
            clock_weather_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
            wifiPrefs.putBool(NVS_KEY_CLOCK_WEATHER_EN, clock_weather_enabled);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        // ── Location method dropdown ─────────────────────────────────────────
        lv_obj_t* slot_method = addSettingRow(card, "Location method",
            "Auto-detect uses your public IP. Pick a city or enter your own coordinates.",
            true);

        // Derive initial method from saved city_idx
        int initial_method = LOC_METHOD_AUTO;
        if (clock_weather_city_idx == CLOCK_LOC_CUSTOM_IDX) {
            initial_method = LOC_METHOD_CUSTOM;
        } else if (clock_weather_city_idx >= 1 && clock_weather_city_idx < CLOCK_CITY_COUNT) {
            initial_method = LOC_METHOD_CITY;
        }

        lv_obj_t* dd_method = makeDropdown(slot_method,
            "Auto-detect from IP\n"
            "Predefined city\n"
            "Custom coordinates",
            (uint16_t)initial_method, false);
        lv_obj_set_width(dd_method, SX(206));

        // ── City sub-container (label + dropdown), hidden unless method == CITY ──
        city_sub_container = lv_obj_create(card);
        lv_obj_set_width(city_sub_container, lv_pct(100));
        lv_obj_set_height(city_sub_container, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(city_sub_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(city_sub_container, 0, 0);
        lv_obj_set_style_pad_all(city_sub_container, 0, 0);
        lv_obj_set_style_pad_row(city_sub_container, SY(4), 0);
        lv_obj_set_flex_flow(city_sub_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(city_sub_container, LV_OBJ_FLAG_SCROLLABLE);

        addSettingLabel(city_sub_container, "City");
        // Build the city list — predefined entries only (skip CLOCK_CITIES[0] which is Auto).
        static char city_opts[2048];
        city_opts[0] = '\0';
        for (int i = 1; i < CLOCK_CITY_COUNT; i++) {
            strncat(city_opts, CLOCK_CITIES[i].label, sizeof(city_opts) - strlen(city_opts) - 2);
            if (i < CLOCK_CITY_COUNT - 1)
                strncat(city_opts, "\n", sizeof(city_opts) - strlen(city_opts) - 1);
        }
        // Map saved idx → dropdown idx (saved 1..N-1 ⇒ dropdown 0..N-2; else 0)
        uint16_t city_dd_initial = (initial_method == LOC_METHOD_CITY)
                                   ? (uint16_t)(clock_weather_city_idx - 1)
                                   : 0;
        dd_city = makeDropdown(city_sub_container, city_opts, city_dd_initial, true);
        lv_obj_add_event_cb(dd_city, [](lv_event_t* e) {
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            // Predefined city sub-dropdown is 0-indexed over [1..CLOCK_CITY_COUNT-1]
            clock_weather_city_idx = (int)lv_dropdown_get_selected(dd) + 1;
            wifiPrefs.putInt(NVS_KEY_CLOCK_WEATHER_CITY, clock_weather_city_idx);
            clock_wx_valid = false;
            clock_weather_needs_refetch = true;
        }, LV_EVENT_VALUE_CHANGED, NULL);

        // ── Custom coords sub-card, hidden unless method == CUSTOM ──────────
        custom_loc_card = lv_obj_create(card);
        lv_obj_set_width(custom_loc_card, lv_pct(100));
        lv_obj_set_height(custom_loc_card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(custom_loc_card, AMB_BG, 0);
        lv_obj_set_style_bg_opa(custom_loc_card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(custom_loc_card, 10, 0);
        lv_obj_set_style_border_color(custom_loc_card, AMB_ACCENT, 0);
        lv_obj_set_style_border_width(custom_loc_card, 1, 0);
        lv_obj_set_style_pad_all(custom_loc_card, SMIN(12), 0);
        lv_obj_set_style_pad_row(custom_loc_card, SY(6), 0);
        lv_obj_set_style_margin_top(custom_loc_card, 6, 0);
        lv_obj_set_flex_flow(custom_loc_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(custom_loc_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* sub_title = lv_label_create(custom_loc_card);
        lv_label_set_text(sub_title, "Custom coordinates");
        lv_obj_set_style_text_font(sub_title, &font_text_14, 0);
        lv_obj_set_style_text_color(sub_title, AMB_ACCENT, 0);

        addSettingLabel(custom_loc_card, "Latitude (-90 to 90)");
        // Initialize from atomic float; format with 4 decimal places
        char lat_init[16] = "";
        if (clock_custom_lat != 0.0f || clock_custom_lon != 0.0f) {
            snprintf(lat_init, sizeof(lat_init), "%.4f", (double)clock_custom_lat);
        }
        custom_lat_ta = makeTextarea(custom_loc_card,
            lat_init, "0123456789.-", 12, "e.g. 40.7128");
        lv_obj_add_event_cb(custom_lat_ta, custom_ta_event_cb, LV_EVENT_FOCUSED,   NULL);
        lv_obj_add_event_cb(custom_lat_ta, custom_ta_event_cb, LV_EVENT_DEFOCUSED, NULL);

        addSettingLabel(custom_loc_card, "Longitude (-180 to 180)");
        char lon_init[16] = "";
        if (clock_custom_lat != 0.0f || clock_custom_lon != 0.0f) {
            snprintf(lon_init, sizeof(lon_init), "%.4f", (double)clock_custom_lon);
        }
        custom_lon_ta = makeTextarea(custom_loc_card,
            lon_init, "0123456789.-", 12, "e.g. -74.0060");
        lv_obj_add_event_cb(custom_lon_ta, custom_ta_event_cb, LV_EVENT_FOCUSED,   NULL);
        lv_obj_add_event_cb(custom_lon_ta, custom_ta_event_cb, LV_EVENT_DEFOCUSED, NULL);

        addSettingLabel(custom_loc_card, "Location name (optional)");
        custom_name_ta = makeTextarea(custom_loc_card,
            clock_custom_name, nullptr, 60, "e.g. New York");
        lv_obj_add_event_cb(custom_name_ta, custom_ta_event_cb, LV_EVENT_FOCUSED,   NULL);
        lv_obj_add_event_cb(custom_name_ta, custom_ta_event_cb, LV_EVENT_DEFOCUSED, NULL);

        addDescLabel(custom_loc_card,
            "Shown on the saver screen instead of \"lat, lon\". Saved automatically on tap outside.");

        // ── Method dropdown handler (after city/custom panels exist) ────────
        lv_obj_add_event_cb(dd_method, [](lv_event_t* e) {
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            int method = (int)lv_dropdown_get_selected(dd);
            if (method == LOC_METHOD_AUTO) {
                clock_weather_city_idx = 0;
            } else if (method == LOC_METHOD_CITY) {
                // If we were on Auto or Custom, default to the first predefined city.
                if (clock_weather_city_idx == 0 || clock_weather_city_idx == CLOCK_LOC_CUSTOM_IDX) {
                    clock_weather_city_idx = 1;
                    if (dd_city) lv_dropdown_set_selected(dd_city, 0);
                }
            } else /* LOC_METHOD_CUSTOM */ {
                clock_weather_city_idx = CLOCK_LOC_CUSTOM_IDX;
            }
            wifiPrefs.putInt(NVS_KEY_CLOCK_WEATHER_CITY, clock_weather_city_idx);
            clock_wx_valid = false;
            clock_weather_needs_refetch = true;
            update_location_method_visibility(method);
        }, LV_EVENT_VALUE_CHANGED, NULL);

        // Apply initial visibility
        update_location_method_visibility(initial_method);

        // ── Temperature unit ────────────────────────────────────────────────
        lv_obj_t* slot_unit = addSettingRow(card, "Temperature unit",
                                            "On = Fahrenheit (°F), Off = Celsius (°C)", false);
        lv_obj_t* sw_fahr = addSwitch(slot_unit, clock_wx_fahrenheit);
        lv_obj_add_event_cb(sw_fahr, [](lv_event_t* e) {
            lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
            clock_wx_fahrenheit = lv_obj_has_state(sw, LV_STATE_CHECKED);
            wifiPrefs.putBool(NVS_KEY_CLOCK_WEATHER_FAHR, clock_wx_fahrenheit);
            clock_weather_needs_refetch = true;
        }, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // ─── Bottom spacer (zero height by default) ─────────────────────────────
    // Grows when a textarea is focused so lv_obj_scroll_to_y has enough range
    // to push the focused field above the keyboard. Collapses on defocus.
    bottom_spacer = lv_obj_create(content);
    lv_obj_set_size(bottom_spacer, lv_pct(100), 0);
    lv_obj_set_style_bg_opa(bottom_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_spacer, 0, 0);
    lv_obj_set_style_pad_all(bottom_spacer, 0, 0);
    lv_obj_clear_flag(bottom_spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bottom_spacer, LV_OBJ_FLAG_SCROLLABLE);

    // ─── Floating compact numeric pad (dark-styled, 4×4) ────────────────────
    // Custom map: digits + minus + decimal + backspace + close. No OK/validate.
    // Smaller than the default LV_KEYBOARD_MODE_NUMBER (which has +/-, layout
    // switchers, and a validate button that wasn't wired here).
    custom_kb = lv_keyboard_create(scr_clock_settings);
    lv_keyboard_set_map(custom_kb, LV_KEYBOARD_MODE_USER_1, num_kb_map, num_kb_ctrl);
    lv_keyboard_set_mode(custom_kb, LV_KEYBOARD_MODE_USER_1);
    lv_obj_set_size(custom_kb, SX(340), SY(200));
    lv_obj_align(custom_kb, LV_ALIGN_BOTTOM_MID, 0, SY(-10));
    lv_obj_add_flag(custom_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(custom_kb, kb_close_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(custom_kb, kb_close_event_cb, LV_EVENT_READY,  NULL);
    style_keyboard_dark(custom_kb);
}
