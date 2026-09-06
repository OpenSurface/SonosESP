/**
 * WiFi Settings Screen
 * Standard phone-style WiFi:
 *   - Status line at top (connected / not connected)
 *   - Password strip (hidden until network tapped) at y=76, h=56
 *   - Network list always at y=140 — NEVER overlaps the strip
 *   - Keyboard slides up from bottom; strip stays visible above it
 */

#include "ui_common.h"
#include "ui_settings_card.h"   // addScreenHeader() - shared title row
#include "ui_fonts.h"
#include "studio_icons.h"
#include "studio.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// WiFi Screen
// Content area: 584x424 (800 - 216 rail), inner box 376 tall after padding.
// Vertical stack, from the design canvas — a status card, then a captioned
// list, instead of a bare status line above bare rows:
//   [0..40]    title row (+Scan button)
//   [44..98]   status card — the connection line, on a surface
//   [104..156] pw_strip (SSID | password | Connect) — hidden until a row is tapped
//   [110..128] "AVAILABLE" caption — occupies the same band; pw_strip is created
//              AFTER it and is opaque, so the strip covers the caption when shown
//   [164..376] network list (scrollable)
//   Keyboard: 175px tall at the screen's bottom edge. It is created on the
//   screen root after the settings dock, so it draws over the dock while typing;
//   pw_strip at 104..156 stays clear of it.
// ============================================================================
void createWiFiScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, ST_BG, 0);

    lv_obj_t* content = createSettingsSidebar(scr_wifi, 5);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title row ──────────────────────────────────────────────────────────────
    btn_wifi_scan = addScreenHeader(content, "WiFi", ST_IC_REFRESH " Scan");
    lv_obj_add_event_cb(btn_wifi_scan, ev_wifi_scan, LV_EVENT_CLICKED, NULL);
    // The scan button's label is retargeted while scanning ("Scanning...").
    lbl_scan_text = screenHeaderActionLabel(btn_wifi_scan);

    // ── Status card (y=44) ─────────────────────────────────────────────────────
    // The label itself is unchanged — roughly twenty call sites in ui_handlers.cpp
    // write its text and colour during scan and connect. Only its parent is new,
    // so the card's border stays neutral and the LABEL's colour keeps carrying
    // the state, exactly as it already did.
    lv_obj_t* status_card = lv_obj_create(content);
    lv_obj_set_size(status_card, lv_pct(100), SY(54));
    lv_obj_set_pos(status_card, 0, SY(44));
    lv_obj_set_style_bg_color(status_card, ST_CARD, 0);
    lv_obj_set_style_radius(status_card, 12, 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_border_color(status_card, ST_BORDER, 0);
    lv_obj_set_style_pad_hor(status_card, SX(16), 0);
    lv_obj_set_style_pad_ver(status_card, 0, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_CLICKABLE);

    lbl_wifi_status = lv_label_create(status_card);
    lv_label_set_text(lbl_wifi_status, "Tap Scan to find networks");
    lv_obj_set_style_text_color(lbl_wifi_status, ST_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_wifi_status, &font_icon_16, 0);
    lv_obj_set_width(lbl_wifi_status, lv_pct(100));
    lv_label_set_long_mode(lbl_wifi_status, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_wifi_status, LV_ALIGN_LEFT_MID, 0, 0);

    // ── "AVAILABLE" caption ────────────────────────────────────────────────────
    // Created BEFORE pw_strip on purpose: the strip shares this band and is
    // opaque, so it covers the caption while a password is being entered.
    lv_obj_t* cap_available = lv_label_create(content);
    lv_label_set_text(cap_available, "AVAILABLE");
    lv_obj_set_style_text_font(cap_available, &font_text_12, 0);
    lv_obj_set_style_text_color(cap_available, ST_TEXT3, 0);
    lv_obj_set_style_text_letter_space(cap_available, 3, 0);
    lv_obj_set_pos(cap_available, 0, SY(110));

    lv_obj_t* cap_rule = lv_obj_create(content);
    lv_obj_set_size(cap_rule, SX(24), SY(2));
    lv_obj_set_pos(cap_rule, 0, SY(130));
    lv_obj_set_style_bg_color(cap_rule, ST_ACCENT, 0);
    lv_obj_set_style_bg_opa(cap_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap_rule, 0, 0);
    lv_obj_set_style_radius(cap_rule, 1, 0);
    lv_obj_clear_flag(cap_rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cap_rule, LV_OBJ_FLAG_CLICKABLE);

    // ── Password strip (y=104, h=52) — ABOVE the list, hidden until tap ────────
    // Layout: [×](30) [SSID](140) gap [password field](255) gap [Connect](120)
    pw_strip = lv_obj_create(content);
    lv_obj_set_size(pw_strip, lv_pct(100), SY(52));
    lv_obj_set_pos(pw_strip, 0, SY(104));
    lv_obj_set_style_bg_color(pw_strip, ST_CARD, 0);
    lv_obj_set_style_border_width(pw_strip, 0, 0);
    lv_obj_set_style_radius(pw_strip, 10, 0);
    lv_obj_set_style_pad_hor(pw_strip, SX(10), 0);
    lv_obj_set_style_pad_ver(pw_strip, 0, 0);
    lv_obj_clear_flag(pw_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pw_strip, LV_OBJ_FLAG_HIDDEN);

    // Cancel (×) button — far left
    lv_obj_t* btn_cancel = lv_btn_create(pw_strip);
    lv_obj_set_size(btn_cancel, SMIN(32), SMIN(32));
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, ST_RAISED, 0);
    lv_obj_set_style_radius(btn_cancel, 16, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
        lv_obj_add_flag(pw_strip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(ta_password, "");
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_x = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_x, ST_IC_X);
    lv_obj_set_style_text_color(lbl_x, ST_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_x, &font_icon_16, 0);
    lv_obj_center(lbl_x);

    // SSID name
    lbl_pw_ssid = lv_label_create(pw_strip);
    lv_label_set_text(lbl_pw_ssid, "");
    lv_obj_set_style_text_font(lbl_pw_ssid, &font_text_14, 0);
    lv_obj_set_style_text_color(lbl_pw_ssid, ST_TEXT, 0);
    lv_obj_set_width(lbl_pw_ssid, SX(138));
    lv_label_set_long_mode(lbl_pw_ssid, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_pw_ssid, LV_ALIGN_LEFT_MID, SX(42), 0);

    // Password textarea
    ta_password = lv_textarea_create(pw_strip);
    lv_obj_set_size(ta_password, SX(255), SY(38));
    lv_obj_align(ta_password, LV_ALIGN_LEFT_MID, SX(190), 0);
    lv_textarea_set_password_mode(ta_password, true);
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_placeholder_text(ta_password, "Password");
    lv_obj_set_style_bg_color(ta_password, ST_RAISED, 0);
    lv_obj_set_style_text_color(ta_password, ST_TEXT, 0);
    lv_obj_set_style_border_color(ta_password, ST_RAISED, 0);
    lv_obj_set_style_radius(ta_password, 8, 0);
    lv_obj_add_event_cb(ta_password, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_FOCUSED) lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_ALL, NULL);

    // Connect button — far right
    btn_wifi_connect = lv_btn_create(pw_strip);
    lv_obj_set_size(btn_wifi_connect, SX(120), SY(38));
    lv_obj_align(btn_wifi_connect, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_wifi_connect, ST_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_connect, 10, 0);
    lv_obj_set_style_shadow_width(btn_wifi_connect, 0, 0);
    lv_obj_add_event_cb(btn_wifi_connect, ev_wifi_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cl = lv_label_create(btn_wifi_connect);
    lv_label_set_text(cl, ST_SC_CHECK " Connect");
    lv_obj_set_style_text_color(cl, ST_ON_ACCENT, 0);
    lv_obj_set_style_text_font(cl, &font_icon_16, 0);
    lv_obj_center(cl);

    // ── Network list (y=164) — always BELOW the strip, never overlaps ──────────
    // SETTINGS_LIST_H() reaches the bottom of the inner box exactly, so the last
    // network is never clipped. It follows the content area's height, which
    // shrank when the now-playing dock was added, without being edited here.
    list_wifi = lv_list_create(content);
    lv_obj_set_size(list_wifi, lv_pct(100), SETTINGS_LIST_H(164));
    lv_obj_set_pos(list_wifi, 0, SY(164));
    lv_obj_set_style_bg_color(list_wifi, ST_BG, 0);
    lv_obj_set_style_border_width(list_wifi, 0, 0);
    lv_obj_set_style_radius(list_wifi, 0, 0);
    lv_obj_set_style_pad_all(list_wifi, 0, 0);
    lv_obj_set_style_pad_row(list_wifi, SY(5), 0);

    // ── Scan spinner (centered in list area, hidden by default) ───────────────
    spinner_wifi_scan = lv_spinner_create(content);
    lv_obj_set_size(spinner_wifi_scan, SMIN(80), SMIN(80));
    lv_obj_align(spinner_wifi_scan, LV_ALIGN_CENTER, 0, SY(60));  // centre of list area
    lv_obj_set_style_arc_color(spinner_wifi_scan, ST_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_wifi_scan, ST_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner_wifi_scan, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner_wifi_scan, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(spinner_wifi_scan, true, LV_PART_INDICATOR);
    lv_obj_move_foreground(spinner_wifi_scan);
    lv_obj_add_flag(spinner_wifi_scan, LV_OBJ_FLAG_HIDDEN);

    // ── Keyboard (on screen root, not content — 175px from bottom) ────────────
    kb = lv_keyboard_create(scr_wifi);
    lv_keyboard_set_textarea(kb, ta_password);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_size(kb, SX(615), SY(175));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, SX(90), SY(-5));
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(kb, ST_CARD, 0);
    lv_obj_set_style_pad_all(kb, SMIN(5), 0);
    lv_obj_set_style_radius(kb, 10, 0);
    lv_obj_set_style_bg_color(kb, ST_RAISED, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, ST_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_ALL, NULL);

    // ── Show connection status every time screen opens ─────────────────────────
    lv_obj_add_event_cb(scr_wifi, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) return;
        if (WiFi.status() == WL_CONNECTED) {
            lv_label_set_text_fmt(lbl_wifi_status,
                ST_IC_WIFI " Connected to %s  (%s)",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            lv_obj_set_style_text_color(lbl_wifi_status, ST_LIVE, 0);
        } else {
            lv_label_set_text(lbl_wifi_status, "Not connected - tap Scan to find networks");
            lv_obj_set_style_text_color(lbl_wifi_status, ST_TEXT3, 0);
        }
    }, LV_EVENT_ALL, NULL);
}
