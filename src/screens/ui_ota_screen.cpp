/**
 * OTA (Over-The-Air) Update Screen
 * Firmware update management with GitHub integration.
 *
 * Laid out from the "SonosESP Amber" design canvas: a version card pairing
 * INSTALLED with AVAILABLE, the channel as a labelled row, then the two actions.
 * Structure only — the palette is the project's existing COL_* tokens.
 *
 * ── Fits the content area exactly ───────────────────────────────────────────
 * The usable box is SETTINGS_INNER_H (376) tall. The previous layout ran to 400+
 * and the content area does not scroll, so its footer was clipped. Every y below
 * is checked against that:
 *
 *   header        0 .. 40
 *   version card 52 .. 152
 *   channel row 162 .. 214
 *   status      224 .. 248
 *   progress    252 .. 268   (hidden until an install runs)
 *   buttons     280 .. 330
 *   footnote    340 .. 364
 */

#include "ui_common.h"
#include "ui_settings_card.h"   // addScreenHeader() - shared title row
#include "ui_fonts.h"
#include "amber_icons.h"
#include "amber.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ── Grid ────────────────────────────────────────────────────────────────────
#define OTA_CARD_Y     52
#define OTA_CARD_H     100
#define OTA_CHAN_Y     162
#define OTA_STATUS_Y   224
#define OTA_PROG_Y     252
#define OTA_BTN_Y      280
#define OTA_BTN_H      50
#define OTA_FOOT_Y     340

// ============================================================================
// OTA Update Screen
// ============================================================================
void createOTAScreen() {
    scr_ota = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_ota, AMB_BG, 0);

    // Create sidebar and get content area (Update is index 7 — Clock added at 6)
    lv_obj_t* content = createSettingsSidebar(scr_ota, 7);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    addScreenHeader(content, "Update", nullptr);

    // ── Version card: installed on the left, available on the right ─────────
    lv_obj_t* card_version = lv_obj_create(content);
    lv_obj_set_size(card_version, lv_pct(100), SY(OTA_CARD_H));
    lv_obj_set_pos(card_version, 0, SY(OTA_CARD_Y));
    lv_obj_set_style_bg_color(card_version, AMB_CARD, 0);
    lv_obj_set_style_radius(card_version, 12, 0);
    lv_obj_set_style_border_width(card_version, 1, 0);
    lv_obj_set_style_border_color(card_version, AMB_BORDER, 0);
    lv_obj_set_style_pad_all(card_version, SMIN(16), 0);
    lv_obj_clear_flag(card_version, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card_version, LV_OBJ_FLAG_CLICKABLE);

    // Tracked captions over the two version numbers, so which is which reads at
    // a glance instead of from a "Current:" / "Latest:" prefix.
    lv_obj_t* cap_installed = lv_label_create(card_version);
    lv_label_set_text(cap_installed, "INSTALLED");
    lv_obj_set_style_text_font(cap_installed, &font_text_12, 0);
    lv_obj_set_style_text_color(cap_installed, AMB_TEXT3, 0);
    lv_obj_set_style_text_letter_space(cap_installed, 2, 0);
    lv_obj_align(cap_installed, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_current_version = lv_label_create(card_version);
    lv_label_set_text(lbl_current_version, "v" FIRMWARE_VERSION);
    lv_obj_set_style_text_font(lbl_current_version, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_current_version, AMB_TEXT, 0);
    lv_obj_align(lbl_current_version, LV_ALIGN_TOP_LEFT, 0, SY(22));

    lv_obj_t* cap_available = lv_label_create(card_version);
    lv_label_set_text(cap_available, "AVAILABLE");
    lv_obj_set_style_text_font(cap_available, &font_text_12, 0);
    lv_obj_set_style_text_color(cap_available, AMB_TEXT3, 0);
    lv_obj_set_style_text_letter_space(cap_available, 2, 0);
    lv_obj_align(cap_available, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Accent-coloured: this is the number the user is deciding about.
    lbl_latest_version = lv_label_create(card_version);
    // NOT "Checking..." — nothing checks until the button is tapped, so that
    // string sat there from boot and read as a check that never finished.
    lv_label_set_text(lbl_latest_version, "--");
    lv_obj_set_style_text_font(lbl_latest_version, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_latest_version, AMB_ACCENT, 0);
    lv_obj_set_width(lbl_latest_version, SX(240));
    lv_label_set_long_mode(lbl_latest_version, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl_latest_version, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_latest_version, LV_ALIGN_TOP_RIGHT, 0, SY(22));

    // The warning belongs with the versions, not stranded at the foot of the
    // screen where it read as boilerplate.
    lv_obj_t* lbl_warn = lv_label_create(card_version);
    lv_label_set_text(lbl_warn, MDI_ALERT "  Keep the panel powered during the update.");
    lv_obj_set_style_text_color(lbl_warn, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_warn, &font_icon_16, 0);
    lv_obj_set_width(lbl_warn, lv_pct(100));
    lv_label_set_long_mode(lbl_warn, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_warn, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // ── Release channel, as a labelled row ──────────────────────────────────
    lv_obj_t* card_channel = lv_obj_create(content);
    lv_obj_set_size(card_channel, lv_pct(100), SY(52));
    lv_obj_set_pos(card_channel, 0, SY(OTA_CHAN_Y));
    lv_obj_set_style_bg_opa(card_channel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(card_channel, 0, 0);
    lv_obj_set_style_border_width(card_channel, 1, 0);
    lv_obj_set_style_border_side(card_channel, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(card_channel, AMB_CARD, 0);
    lv_obj_set_style_pad_all(card_channel, 0, 0);
    lv_obj_clear_flag(card_channel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card_channel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl_channel = lv_label_create(card_channel);
    lv_label_set_text(lbl_channel, "Channel");
    lv_obj_set_style_text_font(lbl_channel, &font_text_16, 0);
    lv_obj_set_style_text_color(lbl_channel, AMB_TEXT, 0);
    lv_obj_align(lbl_channel, LV_ALIGN_LEFT_MID, 0, SY(-9));

    lv_obj_t* lbl_channel_sub = lv_label_create(card_channel);
    lv_label_set_text(lbl_channel_sub, "Nightly builds can break");
    lv_obj_set_style_text_font(lbl_channel_sub, &font_text_12, 0);
    lv_obj_set_style_text_color(lbl_channel_sub, AMB_TEXT3, 0);
    lv_obj_align(lbl_channel_sub, LV_ALIGN_LEFT_MID, 0, SY(11));

    dd_ota_channel = lv_dropdown_create(card_channel);
    lv_dropdown_set_options(dd_ota_channel, "Stable\nNightly");
    lv_obj_set_size(dd_ota_channel, SX(180), SY(44));
    lv_obj_align(dd_ota_channel, LV_ALIGN_RIGHT_MID, 0, 0);

    // Style the dropdown button (closed state)
    lv_obj_set_style_bg_color(dd_ota_channel, AMB_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dd_ota_channel, AMB_BORDER, (lv_style_selector_t)((uint32_t)LV_PART_MAIN | (uint32_t)LV_STATE_PRESSED));
    lv_obj_set_style_text_color(dd_ota_channel, AMB_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd_ota_channel, &font_text_14, LV_PART_MAIN);
    lv_obj_set_style_radius(dd_ota_channel, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(dd_ota_channel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dd_ota_channel, AMB_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(dd_ota_channel, SX(12), LV_PART_MAIN);
    lv_obj_set_style_pad_right(dd_ota_channel, SX(12), LV_PART_MAIN);

    // Style the dropdown list (opened state) - this is the key for dark theme!
    lv_obj_set_style_bg_color(dd_ota_channel, AMB_CARD, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(dd_ota_channel, AMB_ACCENT, (lv_style_selector_t)((uint32_t)LV_PART_SELECTED | (uint32_t)LV_STATE_CHECKED));
    lv_obj_set_style_text_color(dd_ota_channel, AMB_TEXT, LV_PART_SELECTED);

    // Get the list object and style it for dark theme
    lv_obj_t* list = lv_dropdown_get_list(dd_ota_channel);
    if (list) {
        lv_obj_set_style_bg_color(list, AMB_CARD, 0);
        lv_obj_set_style_text_color(list, AMB_TEXT, 0);
        lv_obj_set_style_border_color(list, AMB_BORDER, 0);
        lv_obj_set_style_border_width(list, 1, 0);
    }

    // Load saved channel preference (default to Stable=0)
    ota_channel = wifiPrefs.getInt("ota_channel", 0);
    lv_dropdown_set_selected(dd_ota_channel, ota_channel);

    // Channel change callback
    lv_obj_add_event_cb(dd_ota_channel, [](lv_event_t* e) {
        ota_channel = lv_dropdown_get_selected(dd_ota_channel);
        wifiPrefs.putInt("ota_channel", ota_channel);
        Serial.printf("[OTA] Channel changed to: %s\n", ota_channel == 0 ? "Stable" : "Nightly");
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Status ──────────────────────────────────────────────────────────────
    lbl_ota_status = lv_label_create(content);
    lv_obj_set_pos(lbl_ota_status, 0, SY(OTA_STATUS_Y));
    lv_label_set_text(lbl_ota_status, "Tap 'Check for Updates' to begin");
    lv_obj_set_style_text_color(lbl_ota_status, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_ota_status, &font_icon_16, 0);
    // Width leaves the percentage its own column. Both labels used to be full
    // width on lines four pixels apart, so a long status ran under the number.
    lv_obj_set_width(lbl_ota_status, lv_pct(68));
    // DOT, not WRAP: this label sits in a fixed slot above the progress bar, and
    // a two-line status used to push into it.
    lv_label_set_long_mode(lbl_ota_status, LV_LABEL_LONG_DOT);

    // Progress percentage — same baseline as the status, right-hand column.
    lbl_ota_progress = lv_label_create(content);
    lv_obj_set_pos(lbl_ota_progress, SX(370), SY(OTA_STATUS_Y));
    lv_label_set_text(lbl_ota_progress, "");
    lv_obj_set_style_text_color(lbl_ota_progress, AMB_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_ota_progress, &font_text_16, 0);
    lv_obj_set_width(lbl_ota_progress, SX(166));
    lv_label_set_long_mode(lbl_ota_progress, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(lbl_ota_progress, LV_TEXT_ALIGN_RIGHT, 0);

    // Visual progress bar (hidden by default)
    bar_ota_progress = lv_bar_create(content);
    lv_obj_set_size(bar_ota_progress, lv_pct(100), SY(8));
    lv_obj_set_pos(bar_ota_progress, 0, SY(OTA_PROG_Y));
    lv_bar_set_range(bar_ota_progress, 0, 100);
    lv_bar_set_value(bar_ota_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_ota_progress, AMB_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_ota_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_ota_progress, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_ota_progress, AMB_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_ota_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_ota_progress, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);

    // ── Actions ─────────────────────────────────────────────────────────────
    // Check is the secondary action and Install the primary one, so Check is now
    // an outline button rather than a second filled accent slab competing with it.
    // On a surface, not transparent. A transparent button over AMB_BG (#0B0A09) is
    // a black rectangle next to a gold one — it read as broken rather than as the
    // quieter of two actions. AMB_CARD gives it the same footing as every other
    // secondary control on these pages.
    btn_check_update = lv_btn_create(content);
    lv_obj_set_size(btn_check_update, SX(250), SY(OTA_BTN_H));
    lv_obj_set_pos(btn_check_update, 0, SY(OTA_BTN_Y));
    lv_obj_set_style_bg_color(btn_check_update, AMB_CARD, 0);
    lv_obj_set_style_bg_color(btn_check_update, AMB_RAISED, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_check_update, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_check_update, 1, 0);
    lv_obj_set_style_border_color(btn_check_update, AMB_BORDER, 0);
    lv_obj_set_style_radius(btn_check_update, 12, 0);
    lv_obj_set_style_shadow_width(btn_check_update, 0, 0);
    lv_obj_add_event_cb(btn_check_update, ev_check_update, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_check = lv_label_create(btn_check_update);
    lv_label_set_text(lbl_check, AMB_IC_REFRESH " Check for Updates");
    lv_obj_set_style_text_color(lbl_check, AMB_TEXT, 0);
    lv_obj_set_style_text_font(lbl_check, &font_icon_16, 0);
    lv_obj_center(lbl_check);

    // Install Update button (hidden by default)
    btn_install_update = lv_btn_create(content);
    lv_obj_set_size(btn_install_update, SX(260), SY(OTA_BTN_H));
    lv_obj_set_pos(btn_install_update, SX(276), SY(OTA_BTN_Y));
    lv_obj_set_style_bg_color(btn_install_update, AMB_ACCENT, 0);
    lv_obj_set_style_radius(btn_install_update, 12, 0);
    lv_obj_set_style_shadow_width(btn_install_update, 0, 0);
    lv_obj_add_event_cb(btn_install_update, ev_install_update, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_install = lv_label_create(btn_install_update);
    lv_label_set_text(lbl_install, AMB_IC_DOWNLOAD " Install Update");
    lv_obj_set_style_text_color(lbl_install, AMB_ON_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_install, &font_icon_16, 0);
    lv_obj_center(lbl_install);
    lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);  // Hidden until update available

    // ── Footnote ────────────────────────────────────────────────────────────
    lv_obj_t* lbl_info = lv_label_create(content);
    lv_label_set_text(lbl_info,
        "Stable ships tested releases. Nightly carries the latest test builds.");
    lv_obj_set_style_text_color(lbl_info, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_info, &font_text_12, 0);
    lv_obj_set_width(lbl_info, lv_pct(100));
    lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(lbl_info, 0, SY(OTA_FOOT_Y));

    // Clear the previous check each time the screen opens, so its result cannot
    // be read as current. The first version of this guard tested
    // `latest_version.length() == 0`, which is backwards: it only rewrote the
    // label in the case where it already said "--", and left a genuinely stale
    // version, its status line, and an armed Install button exactly as they were.
    lv_obj_add_event_cb(scr_ota, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) return;
        if (ota_in_progress) return;             // an install is mid-flight
        latest_version = "";
        download_url   = "";
        if (lbl_latest_version) lv_label_set_text(lbl_latest_version, "--");
        if (lbl_ota_progress)   lv_label_set_text(lbl_ota_progress, "");
        if (bar_ota_progress)   lv_obj_add_flag(bar_ota_progress, LV_OBJ_FLAG_HIDDEN);
        if (btn_install_update) lv_obj_add_flag(btn_install_update, LV_OBJ_FLAG_HIDDEN);
        if (lbl_ota_status) {
            lv_label_set_text(lbl_ota_status, "Tap 'Check for Updates' to begin");
            lv_obj_set_style_text_color(lbl_ota_status, AMB_TEXT3, 0);
        }
    }, LV_EVENT_ALL, NULL);
}
