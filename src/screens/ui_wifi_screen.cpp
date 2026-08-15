/**
 * WiFi Settings Screen
 * Standard phone-style WiFi:
 *   - Status line at top (connected / not connected)
 *   - Password strip (hidden until network tapped) at y=76, h=56
 *   - Network list always at y=140 — NEVER overlaps the strip
 *   - Keyboard slides up from bottom; strip stays visible above it
 */

#include "ui_common.h"
#include "ui_fonts.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// WiFi Screen
// Content area: 620×480 (800px − 180px sidebar)
// Vertical stack:
//   [0..44]   title row  (+Scan button)
//   [50..70]  status label
//   [76..132] pw_strip (SSID label | password field | Connect btn) — hidden until tap
//   [140..480] network list (scrollable, 340px tall)
//   Keyboard: 175px, bottom of screen — sits at y=300..480
//             pw_strip at y=76..132 is safely above it
// ============================================================================
void createWiFiScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, COL_SCREEN, 0);

    lv_obj_t* content = createSettingsSidebar(scr_wifi, 5);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title row ──────────────────────────────────────────────────────────────
    lv_obj_t* title_row = lv_obj_create(content);
    lv_obj_set_size(title_row, lv_pct(100), SY(44));
    lv_obj_set_pos(title_row, 0, 0);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(title_row);
    lv_label_set_text(lbl_title, "WiFi");
    lv_obj_set_style_text_font(lbl_title, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    btn_wifi_scan = lv_button_create(title_row);
    lv_obj_set_size(btn_wifi_scan, SX(100), SY(34));
    lv_obj_align(btn_wifi_scan, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_wifi_scan, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_scan, 17, 0);
    lv_obj_set_style_shadow_width(btn_wifi_scan, 0, 0);
    lv_obj_add_event_cb(btn_wifi_scan, ev_wifi_scan, LV_EVENT_CLICKED, NULL);
    lbl_scan_text = lv_label_create(btn_wifi_scan);
    lv_label_set_text(lbl_scan_text, MDI_REFRESH " Scan");
    lv_obj_set_style_text_color(lbl_scan_text, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_scan_text, &lv_font_mdi_16, 0);
    lv_obj_center(lbl_scan_text);

    // ── Status label (y=50) ────────────────────────────────────────────────────
    lbl_wifi_status = lv_label_create(content);
    lv_obj_set_pos(lbl_wifi_status, 0, SY(50));
    lv_label_set_text(lbl_wifi_status, "Tap Scan to find networks");
    lv_obj_set_style_text_color(lbl_wifi_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_mdi_16, 0);
    lv_obj_set_width(lbl_wifi_status, lv_pct(100));
    lv_label_set_long_mode(lbl_wifi_status, LV_LABEL_LONG_DOT);

    // ── Password strip (y=76, h=56) — ABOVE the list, hidden until tap ─────────
    // Layout: [×](30) [SSID](140) gap [password field](255) gap [Connect](120)
    pw_strip = lv_obj_create(content);
    lv_obj_set_size(pw_strip, lv_pct(100), SY(56));
    lv_obj_set_pos(pw_strip, 0, SY(76));
    lv_obj_set_style_bg_color(pw_strip, COL_CARD, 0);
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
    lv_obj_set_style_bg_color(btn_cancel, COL_BTN, 0);
    lv_obj_set_style_radius(btn_cancel, 16, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
        lv_obj_add_flag(pw_strip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(ta_password, "");
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_x = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_x, MDI_CLOSE);
    lv_obj_set_style_text_color(lbl_x, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_x, &lv_font_mdi_16, 0);
    lv_obj_center(lbl_x);

    // SSID name
    lbl_pw_ssid = lv_label_create(pw_strip);
    lv_label_set_text(lbl_pw_ssid, "");
    lv_obj_set_style_text_font(lbl_pw_ssid, &font_text_14, 0);
    lv_obj_set_style_text_color(lbl_pw_ssid, COL_TEXT, 0);
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
    lv_obj_set_style_bg_color(ta_password, COL_BTN, 0);
    lv_obj_set_style_text_color(ta_password, COL_TEXT, 0);
    lv_obj_set_style_border_color(ta_password, COL_BTN, 0);
    lv_obj_set_style_radius(ta_password, 8, 0);
    lv_obj_add_event_cb(ta_password, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_FOCUSED) lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_ALL, NULL);

    // Connect button — far right
    btn_wifi_connect = lv_btn_create(pw_strip);
    lv_obj_set_size(btn_wifi_connect, SX(120), SY(38));
    lv_obj_align(btn_wifi_connect, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_wifi_connect, COL_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_connect, 10, 0);
    lv_obj_set_style_shadow_width(btn_wifi_connect, 0, 0);
    lv_obj_add_event_cb(btn_wifi_connect, ev_wifi_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cl = lv_label_create(btn_wifi_connect);
    lv_label_set_text(cl, MDI_CHECK " Connect");
    lv_obj_set_style_text_color(cl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(cl, &lv_font_mdi_16, 0);
    lv_obj_center(cl);

    // ── Network list (y=140, h=340) — always BELOW strip, never overlaps ───────
    // With keyboard (175px from bottom) list shows y=140..300 = 160px ≈ 3 items
    // pw_strip at y=76..132 stays fully visible above keyboard
    list_wifi = lv_list_create(content);
    lv_obj_set_size(list_wifi, lv_pct(100), SY(340));
    lv_obj_set_pos(list_wifi, 0, SY(140));
    lv_obj_set_style_bg_color(list_wifi, COL_SCREEN, 0);
    lv_obj_set_style_border_width(list_wifi, 0, 0);
    lv_obj_set_style_radius(list_wifi, 0, 0);
    lv_obj_set_style_pad_all(list_wifi, 0, 0);
    lv_obj_set_style_pad_row(list_wifi, SY(5), 0);

    // ── Scan spinner (centered in list area, hidden by default) ───────────────
    spinner_wifi_scan = lv_spinner_create(content);
    lv_obj_set_size(spinner_wifi_scan, SMIN(80), SMIN(80));
    lv_obj_align(spinner_wifi_scan, LV_ALIGN_CENTER, 0, SY(60));  // centre of list area
    lv_obj_set_style_arc_color(spinner_wifi_scan, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_wifi_scan, COL_BORDER, LV_PART_MAIN);
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
    lv_obj_set_style_bg_color(kb, COL_CARD, 0);
    lv_obj_set_style_pad_all(kb, SMIN(5), 0);
    lv_obj_set_style_radius(kb, 10, 0);
    lv_obj_set_style_bg_color(kb, COL_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_ALL, NULL);

    // ── Show connection status every time screen opens ─────────────────────────
    lv_obj_add_event_cb(scr_wifi, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) return;
        if (WiFi.status() == WL_CONNECTED) {
            lv_label_set_text_fmt(lbl_wifi_status,
                MDI_WIFI " Connected to %s  (%s)",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            lv_obj_set_style_text_color(lbl_wifi_status, COL_OK, 0);
        } else {
            lv_label_set_text(lbl_wifi_status, "Not connected — tap Scan to find networks");
            lv_obj_set_style_text_color(lbl_wifi_status, COL_TEXT2, 0);
        }
    }, LV_EVENT_ALL, NULL);
}
