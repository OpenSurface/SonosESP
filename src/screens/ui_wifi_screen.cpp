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
#include "amber_icons.h"
#include "amber.h"

// Forward declaration
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ── Live signal strength (#196) ─────────────────────────────────────────────
// The screen showed SSID and IP but nothing about link quality, so a panel
// misbehaving in a far room gave no hint that the link was the problem. The
// only RSSI anywhere was on the boot screen, which scrolls past.
//
// Four bars, read the way every phone draws them, using the SAME thresholds
// the scan list already colours its rows with (ui_handlers.cpp): -60 and -75.
// Keeping those identical matters - two different scales for "good signal" in
// one product is worse than not showing it at all.
#define WB_BARS   4
static lv_obj_t* wifi_bars[WB_BARS];
static lv_obj_t* lbl_wifi_rssi   = nullptr;
static lv_timer_t* wifi_rssi_timer = nullptr;

// Bars grow left to right. Heights are design-space; SY() is applied on build.
static const int WB_H[WB_BARS] = { 5, 9, 13, 17 };

static void wifiBarsUpdate(int32_t rssi, bool connected) {
    // Same three tiers as the scan list, plus a fourth bar so the strong case
    // reads as "full" rather than "one short of something".
    int lit;
    lv_color_t col;
    if      (!connected) { lit = 0; col = AMB_TEXT3;   }
    else if (rssi > -60) { lit = 4; col = AMB_LIVE;    }
    else if (rssi > -67) { lit = 3; col = AMB_LIVE;    }
    else if (rssi > -75) { lit = 2; col = AMB_ACCENT;  }
    // Weak is DIM, not red. The scan list greys its rows with COL_ERROR, but
    // that palette is not this one - Amber has no red at all, because gold is
    // its single action colour, and adding one would be a palette change the
    // UI linter would rightly flag. One lit bar out of four already says
    // "weak" without needing a colour to repeat it.
    else                 { lit = 1; col = AMB_TEXT3;   }

    for (int i = 0; i < WB_BARS; i++) {
        if (!wifi_bars[i]) continue;
        const bool on = (i < lit);
        lv_obj_set_style_bg_color(wifi_bars[i], on ? col : AMB_BORDER, 0);
        // Unlit bars stay visible but recede, so the widget keeps its shape
        // and you can see how many bars are MISSING, not just how many are on.
        lv_obj_set_style_bg_opa(wifi_bars[i], on ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if (lbl_wifi_rssi) {
        if (connected) lv_label_set_text_fmt(lbl_wifi_rssi, "%d dBm", (int)rssi);
        else           lv_label_set_text(lbl_wifi_rssi, "");
        lv_obj_set_style_text_color(lbl_wifi_rssi, col, 0);
    }
}

static void wifiRssiTick(lv_timer_t*) {
    const bool up = (WiFi.status() == WL_CONNECTED);
    wifiBarsUpdate(up ? WiFi.RSSI() : 0, up);
}

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
    lv_obj_set_style_bg_color(scr_wifi, AMB_BG, 0);

    lv_obj_t* content = createSettingsSidebar(scr_wifi, 5);
    lv_obj_set_scrollable(content, false);

    // ── Title row ──────────────────────────────────────────────────────────────
    btn_wifi_scan = addScreenHeader(content, "WiFi", AMB_IC_REFRESH " Scan");
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
    lv_obj_set_style_bg_color(status_card, AMB_CARD, 0);
    lv_obj_set_style_radius(status_card, 12, 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_border_color(status_card, AMB_BORDER, 0);
    lv_obj_set_style_pad_hor(status_card, SX(16), 0);
    lv_obj_set_style_pad_ver(status_card, 0, 0);
    lv_obj_set_scrollable(status_card, false);
    lv_obj_set_clickable(status_card, false);

    lbl_wifi_status = lv_label_create(status_card);
    lv_label_set_text(lbl_wifi_status, "Tap Scan to find networks");
    lv_obj_set_style_text_color(lbl_wifi_status, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_wifi_status, &font_icon_16, 0);
    lv_obj_set_width(lbl_wifi_status, lv_pct(100));
    lv_label_set_long_mode(lbl_wifi_status, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_wifi_status, LV_ALIGN_LEFT_MID, 0, 0);

    // Signal strength, right-aligned in the status card (#196). Built here so
    // it exists before the SCREEN_LOADED handler below ever fires.
    {
        lv_obj_t* sig = lv_obj_create(status_card);
        lv_obj_remove_style_all(sig);
        lv_obj_set_size(sig, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_scrollable(sig, false);
        lv_obj_set_flex_flow(sig, LV_FLEX_FLOW_ROW);
        // Cross-axis END sits the bars on a common baseline so the staircase
        // grows upward, which is what makes it read as signal strength.
        lv_obj_set_flex_align(sig, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(sig, SX(3), 0);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, 0, 0);

        lbl_wifi_rssi = ambLabel(sig, &font_text_12, AMB_TEXT3, "");
        lv_obj_set_style_pad_right(lbl_wifi_rssi, SX(8), 0);

        for (int i = 0; i < WB_BARS; i++) {
            wifi_bars[i] = ambRoundRect(sig, 4, WB_H[i], 2, AMB_BORDER);
            lv_obj_set_style_bg_opa(wifi_bars[i], LV_OPA_40, 0);
        }
        // The label must not be pushed off by a long SSID, and the status text
        // is LONG_DOT at 100% width, so give the bars the foreground.
        lv_obj_move_foreground(sig);
    }

    // ── "AVAILABLE" caption ────────────────────────────────────────────────────
    // Created BEFORE pw_strip on purpose: the strip shares this band and is
    // opaque, so it covers the caption while a password is being entered.
    lv_obj_t* cap_available = lv_label_create(content);
    lv_label_set_text(cap_available, "AVAILABLE");
    lv_obj_set_style_text_font(cap_available, &font_text_12, 0);
    lv_obj_set_style_text_color(cap_available, AMB_TEXT3, 0);
    lv_obj_set_style_text_letter_space(cap_available, 3, 0);
    lv_obj_set_pos(cap_available, 0, SY(110));

    lv_obj_t* cap_rule = lv_obj_create(content);
    lv_obj_set_size(cap_rule, SX(24), SY(2));
    lv_obj_set_pos(cap_rule, 0, SY(130));
    lv_obj_set_style_bg_color(cap_rule, AMB_ACCENT, 0);
    lv_obj_set_style_bg_opa(cap_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap_rule, 0, 0);
    lv_obj_set_style_radius(cap_rule, 1, 0);
    lv_obj_set_scrollable(cap_rule, false);
    lv_obj_set_clickable(cap_rule, false);

    // ── Password strip (y=104, h=52) — ABOVE the list, hidden until tap ────────
    // Layout across a 516px inner box: [×](30) [SSID](140) gap [password](198) gap
// [Connect](120). Re-derive these if SB_RAIL_W or SETTINGS_CONTENT_PAD changes —
// the previous numbers were inherited from a 620px content area and overlapped.
    pw_strip = lv_obj_create(content);
    lv_obj_set_size(pw_strip, lv_pct(100), SY(52));
    lv_obj_set_pos(pw_strip, 0, SY(104));
    lv_obj_set_style_bg_color(pw_strip, AMB_CARD, 0);
    lv_obj_set_style_border_width(pw_strip, 0, 0);
    lv_obj_set_style_radius(pw_strip, 10, 0);
    lv_obj_set_style_pad_hor(pw_strip, SX(10), 0);
    lv_obj_set_style_pad_ver(pw_strip, 0, 0);
    lv_obj_set_scrollable(pw_strip, false);
    lv_obj_set_hidden(pw_strip, true);

    // Cancel (×) button — far left
    lv_obj_t* btn_cancel = lv_button_create(pw_strip);
    lv_obj_set_size(btn_cancel, SMIN(32), SMIN(32));
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, AMB_RAISED, 0);
    lv_obj_set_style_radius(btn_cancel, 16, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
        lv_obj_set_hidden(pw_strip, true);
        lv_obj_set_hidden(kb, true);
        lv_textarea_set_text(ta_password, "");
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_x = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_x, AMB_IC_X);
    lv_obj_set_style_text_color(lbl_x, AMB_TEXT3, 0);
    lv_obj_set_style_text_font(lbl_x, &font_icon_16, 0);
    lv_obj_center(lbl_x);

    // SSID name
    lbl_pw_ssid = lv_label_create(pw_strip);
    lv_label_set_text(lbl_pw_ssid, "");
    lv_obj_set_style_text_font(lbl_pw_ssid, &font_text_14, 0);
    lv_obj_set_style_text_color(lbl_pw_ssid, AMB_TEXT, 0);
    lv_obj_set_width(lbl_pw_ssid, SX(138));
    lv_label_set_long_mode(lbl_pw_ssid, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_pw_ssid, LV_ALIGN_LEFT_MID, SX(42), 0);

    // Password textarea
    //
    // 198 wide, not 255. The strip's inner box is 516 design px: screen 800, less
    // the 216 sidebar rail, less 2x24 content padding, less 2x10 strip padding.
    // The field starts at 190 and Connect is 120 wide pinned right, so it owns
    // 396..516 — a 255-wide field reached 445 and ran 49px UNDER the button.
    //
    // That was not just cosmetic. Connect is created after the textarea, so LVGL
    // gives it the overlapping band: a tap on the right-hand end of the password
    // field fired Connect instead of focusing the field, and the user got
    // "Authentication failed" with a password they had not finished typing. On
    // the first screen a new owner has to complete.
    //
    // The header comment above still described a 620-wide content area from
    // before the rail widened to 216 and SETTINGS_CONTENT_PAD was introduced;
    // its budget (30+140+255+120 = 545) never fitted 516.
    ta_password = lv_textarea_create(pw_strip);
    lv_obj_set_size(ta_password, SX(198), SY(38));
    lv_obj_align(ta_password, LV_ALIGN_LEFT_MID, SX(190), 0);
    lv_textarea_set_password_mode(ta_password, true);
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_placeholder_text(ta_password, "Password");
    lv_obj_set_style_bg_color(ta_password, AMB_RAISED, 0);
    lv_obj_set_style_text_color(ta_password, AMB_TEXT, 0);
    lv_obj_set_style_border_color(ta_password, AMB_RAISED, 0);
    lv_obj_set_style_radius(ta_password, 8, 0);
    lv_obj_add_event_cb(ta_password, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_FOCUSED) lv_obj_set_hidden(kb, false);
    }, LV_EVENT_ALL, NULL);

    // Connect button — far right
    btn_wifi_connect = lv_button_create(pw_strip);
    lv_obj_set_size(btn_wifi_connect, SX(120), SY(38));
    lv_obj_align(btn_wifi_connect, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_wifi_connect, AMB_ACCENT, 0);
    lv_obj_set_style_radius(btn_wifi_connect, 10, 0);
    lv_obj_set_style_shadow_width(btn_wifi_connect, 0, 0);
    lv_obj_add_event_cb(btn_wifi_connect, ev_wifi_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cl = lv_label_create(btn_wifi_connect);
    lv_label_set_text(cl, AMB_SC_CHECK " Connect");
    lv_obj_set_style_text_color(cl, AMB_ON_ACCENT, 0);
    lv_obj_set_style_text_font(cl, &font_icon_16, 0);
    lv_obj_center(cl);

    // ── Network list (y=164) — always BELOW the strip, never overlaps ──────────
    // SETTINGS_LIST_H() reaches the bottom of the inner box exactly, so the last
    // network is never clipped. It follows the content area's height, which
    // shrank when the now-playing dock was added, without being edited here.
    // Plain object + flex column, not lv_list_create(): lv_list is deprecated
    // in LVGL 9.6. It was never more than this - lv_list_class is lv_obj_class
    // with no constructor and only a different default size, which the
    // set_size() below overrides anyway, plus the flex flow set here.
    list_wifi = lv_obj_create(content);
    lv_obj_set_flex_flow(list_wifi, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(list_wifi, lv_pct(100), SETTINGS_LIST_H(164));
    lv_obj_set_pos(list_wifi, 0, SY(164));
    lv_obj_set_style_bg_color(list_wifi, AMB_BG, 0);
    lv_obj_set_style_border_width(list_wifi, 0, 0);
    lv_obj_set_style_radius(list_wifi, 0, 0);
    lv_obj_set_style_pad_all(list_wifi, 0, 0);
    lv_obj_set_style_pad_row(list_wifi, SY(5), 0);

    // ── Scan spinner (centered in list area, hidden by default) ───────────────
    spinner_wifi_scan = lv_spinner_create(content);
    lv_obj_set_size(spinner_wifi_scan, SMIN(80), SMIN(80));
    lv_obj_align(spinner_wifi_scan, LV_ALIGN_CENTER, 0, SY(60));  // centre of list area
    lv_obj_set_style_arc_color(spinner_wifi_scan, AMB_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_wifi_scan, AMB_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner_wifi_scan, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner_wifi_scan, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(spinner_wifi_scan, true, LV_PART_INDICATOR);
    lv_obj_move_foreground(spinner_wifi_scan);
    lv_obj_set_hidden(spinner_wifi_scan, true);

    // ── Keyboard (on screen root, not content — 175px from bottom) ────────────
    kb = lv_keyboard_create(scr_wifi);
    lv_keyboard_set_textarea(kb, ta_password);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_size(kb, SX(615), SY(175));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, SX(90), SY(-5));
    lv_obj_set_hidden(kb, true);
    lv_obj_set_style_bg_color(kb, AMB_CARD, 0);
    lv_obj_set_style_pad_all(kb, SMIN(5), 0);
    lv_obj_set_style_radius(kb, 10, 0);
    lv_obj_set_style_bg_color(kb, AMB_RAISED, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, AMB_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY) lv_obj_set_hidden(kb, true);
    }, LV_EVENT_ALL, NULL);

    // ── Show connection status every time screen opens ─────────────────────────
    // The signal refresh lives and dies with the screen (#196): dious38 asked
    // for ~5s "only while the screen is open", and that is also the correct
    // scope - a timer left running would poll the radio from every other
    // screen for a widget nobody can see. UNLOADED deletes it, and the null
    // guard makes a double-unload harmless.
    lv_obj_add_event_cb(scr_wifi, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_SCREEN_UNLOADED) return;
        if (wifi_rssi_timer) { lv_timer_delete(wifi_rssi_timer); wifi_rssi_timer = nullptr; }
    }, LV_EVENT_ALL, NULL);

    lv_obj_add_event_cb(scr_wifi, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) return;

        // Paint immediately, then keep it live. Without the first call the
        // bars would sit blank for five seconds every time the screen opens.
        wifiRssiTick(nullptr);
        if (!wifi_rssi_timer) wifi_rssi_timer = lv_timer_create(wifiRssiTick, 5000, nullptr);

        if (WiFi.status() == WL_CONNECTED) {
            lv_label_set_text_fmt(lbl_wifi_status,
                AMB_IC_WIFI " Connected to %s  (%s)",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            lv_obj_set_style_text_color(lbl_wifi_status, AMB_LIVE, 0);
        } else {
            lv_label_set_text(lbl_wifi_status, "Not connected - tap Scan to find networks");
            lv_obj_set_style_text_color(lbl_wifi_status, AMB_TEXT3, 0);
        }
    }, LV_EVENT_ALL, NULL);
}
