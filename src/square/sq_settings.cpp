// sq_settings.cpp — Réglages and the Wi-Fi setup screen.
#include "sq_ui.h"
#include "sq_work.h"
#include "ui_common.h"
#include "config.h"

// ── Réglages ────────────────────────────────────────────────────────────────
static lv_obj_t* s_scr;
static lv_obj_t* s_svc_sub;
static lv_obj_t* s_spk_sub;
static lv_obj_t* s_fav_sub;
static lv_obj_t* s_wifi_sub;

static lv_obj_t* card(lv_obj_t* parent, const char* icon, uint32_t color, const char* title,
                      lv_obj_t** sub_out) {
    lv_obj_t* c = sqBox(parent, LV_PCT(100), 66, SQ_SURFACE, 20);
    lv_obj_set_style_pad_left(c, 12, 0);
    lv_obj_set_style_pad_right(c, 9, 0);
    lv_obj_t* dot = sqBox(c, 40, 40, color, 20);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* ic = sqIcon(dot, icon, &font_sqi_20, SQ_INK);
    lv_obj_center(ic);
    lv_obj_t* t = sqLabel(c, title, &font_sq_18, SQ_TEXT);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 52, -11);
    lv_obj_t* s = sqLabel(c, "", &font_sq_14, SQ_MUTED);
    lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s, 230, 19);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, 52, 13);
    if (sub_out) *sub_out = s;
    return c;
}

static lv_obj_t* refreshButton(lv_obj_t* c, uint32_t color, lv_event_cb_t cb) {
    lv_obj_t* b = sqPillButton(c, 46, SQ_SURFACE, color, IC_REFRESH, "Rafraîchir", &font_sq_14);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void sqSettingsRefresh(void);

// Tap on the Services card: next Spotify account.
static void svcNextAccount(lv_event_t*) {
    SvcSource acc[6];
    int n = sqSpotifyAccounts(acc, 6);
    if (n < 2) return;
    int sid, serial, cur = 0;
    if (sqSpotify(&sid, &serial))
        for (int i = 0; i < n; ++i) if (acc[i].serial == serial) cur = i;
    const SvcSource& next = acc[(cur + 1) % n];
    sqSpotifyChoose(next.serial);
    char msg[96];
    snprintf(msg, sizeof(msg), "Compte : %s", next.label);
    sqToast(msg);
    sqSettingsRefresh();
}

static void svcRefresh(lv_event_t*) { sqWorkPost(SQ_JOB_SVC_REFRESH); sqToast("Je relis les services…"); }
static void spkRefresh(lv_event_t*) { sqWorkPost(SQ_JOB_DISCOVER); sqToast("Je cherche les enceintes…"); }
static void favRefresh(lv_event_t*) { sqWorkPost(SQ_JOB_FAVS_LOAD); sqToast("Je relis les favoris…"); }
static void wifiOpen(lv_event_t*) { sqWifiOpen(); }

static void brightnessChanged(lv_event_t* e) {
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    brightness_level = lv_slider_get_value(sl);
    display_set_brightness(brightness_level);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) wifiPrefs.putInt(NVS_KEY_BRIGHTNESS, brightness_level);
}

void sqSettingsRefresh(void);

void sqSettingsCreate(void) {
    s_scr = sqScreenCreate();
    sqHeader(s_scr, "Réglages", SQ_TEXT, true);
    lv_obj_t* list = lv_obj_create(s_scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, SQ_HEADER_H);
    lv_obj_set_size(list, SQ_W, SQ_H - SQ_HEADER_H);
    lv_obj_set_style_pad_hor(list, 16, 0);
    lv_obj_set_style_pad_bottom(list, 16, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* c = card(list, IC_ALBUM, SQ_MINT, "Spotify", &s_svc_sub);
    lv_obj_set_clickable(c, true);   // tap the card: next account
    lv_obj_add_event_cb(c, svcNextAccount, LV_EVENT_CLICKED, nullptr);
    refreshButton(c, SQ_MINT, svcRefresh);
    c = card(list, IC_SPEAKER, SQ_SKY, "Enceintes", &s_spk_sub);
    refreshButton(c, SQ_SKY, spkRefresh);
    c = card(list, IC_STAR, SQ_YELLOW, "Favoris", &s_fav_sub);
    refreshButton(c, SQ_YELLOW, favRefresh);

    c = card(list, IC_WIFI, SQ_LILAC, "Wi-Fi", &s_wifi_sub);
    lv_obj_t* go = sqRoundButton(c, 46, SQ_RAISED, IC_RIGHT, &font_sqi_26, SQ_MUTED);
    lv_obj_align(go, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(go, wifiOpen, LV_EVENT_CLICKED, nullptr);

    c = card(list, IC_BRIGHT, SQ_ORANGE, "Luminosité", nullptr);
    lv_obj_t* sl = lv_slider_create(c);
    lv_obj_set_size(sl, 160, 16);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_slider_set_range(sl, 10, 100);
    lv_slider_set_value(sl, brightness_level, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(SQ_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(SQ_ORANGE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(SQ_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 16);
    lv_obj_add_event_cb(sl, brightnessChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sl, brightnessChanged, LV_EVENT_RELEASED, nullptr);

    lv_obj_t* v = sqLabel(list, "SonosESP carré · " FIRMWARE_VERSION, &font_sq_14, SQ_MUTED);
    lv_obj_set_width(v, LV_PCT(100));
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);

    sqRegisterScreen(SQ_SCR_SETTINGS, s_scr, SQ_TAB_NONE);
}

void sqSettingsRefresh(void) {
    int sid, serial;
    SvcSource acc[6];
    int n = sqSpotifyAccounts(acc, 6);
    if (sqSpotify(&sid, &serial)) {
        const char* label = "Spotify";
        for (int i = 0; i < n; ++i) if (acc[i].serial == serial) label = acc[i].label;
        if (n > 1) lv_label_set_text_fmt(s_svc_sub, "%s · touche pour changer", label);
        else       lv_label_set_text(s_svc_sub, label);
    } else {
        lv_label_set_text(s_svc_sub, musicServices.state() == SVC_LOADING ? "Lecture en cours…"
                                                                          : "Spotify introuvable");
    }
    lv_label_set_text_fmt(s_spk_sub, "%d enceinte(s)", sonos.getDeviceCount());
    lv_label_set_text_fmt(s_fav_sub, "%d favori(s)", sqFavsCount());
    if (WiFi.status() == WL_CONNECTED)
        lv_label_set_text_fmt(s_wifi_sub, "%s · %d dBm", WiFi.SSID().c_str(), (int)WiFi.RSSI());
    else
        lv_label_set_text(s_wifi_sub, "Non connecté");
}

// ── Wi-Fi ───────────────────────────────────────────────────────────────────
static lv_obj_t* s_wifi;
static lv_obj_t* s_net_list;
static lv_obj_t* s_pass_panel;
static lv_obj_t* s_pass_title;
static lv_obj_t* s_pass_ta;
static lv_obj_t* s_status;
static String    s_ssid;
static bool      s_scanning = false;
static uint32_t  s_connect_started = 0;

static void netClicked(lv_event_t* e) {
    lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
    s_ssid = lv_label_get_text(lv_obj_get_child(b, 1));
    lv_label_set_text_fmt(s_pass_title, "Mot de passe de « %s »", s_ssid.c_str());
    lv_textarea_set_text(s_pass_ta, "");
    sqSetHidden(s_pass_panel, false);
}

static void connectClicked(lv_event_t*) {
    const char* pass = lv_textarea_get_text(s_pass_ta);
    pending_wifi_ssid = s_ssid;
    pending_wifi_pass = pass;
    // Never log the password itself; its length tells a typo from a radio problem.
    Serial.printf("[WIFI] Joining '%s' (password: %u chars)\n", s_ssid.c_str(), (unsigned)strlen(pass));
    WiFi.disconnect();
    WiFi.begin(s_ssid.c_str(), pass);
    s_connect_started = millis();
    sqSetHidden(s_pass_panel, true);
    lv_label_set_text_fmt(s_status, "Connexion à « %s »…", s_ssid.c_str());
}

static void passCancel(lv_event_t*) { sqSetHidden(s_pass_panel, true); }

static void scanStart(void) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    s_scanning = true;
    lv_label_set_text(s_status, "Je cherche les réseaux…");
}
static void rescanClicked(lv_event_t*) { scanStart(); }

void sqWifiCreate(void) {
    s_wifi = sqScreenCreate();
    lv_obj_t* hdr = sqHeader(s_wifi, "Wi-Fi", SQ_LILAC, true);
    lv_obj_t* re = sqRoundButton(hdr, 48, SQ_RAISED, IC_REFRESH, &font_sqi_26, SQ_LILAC);
    lv_obj_add_event_cb(re, rescanClicked, LV_EVENT_CLICKED, nullptr);

    s_status = sqLabel(s_wifi, "", &font_sq_16, SQ_MUTED);
    lv_obj_set_pos(s_status, 20, SQ_HEADER_H - 4);

    s_net_list = lv_obj_create(s_wifi);
    lv_obj_remove_style_all(s_net_list);
    lv_obj_set_pos(s_net_list, 0, SQ_HEADER_H + 24);
    lv_obj_set_size(s_net_list, SQ_W, SQ_H - SQ_HEADER_H - 24);
    lv_obj_set_style_pad_hor(s_net_list, 16, 0);
    lv_obj_set_style_pad_bottom(s_net_list, 16, 0);
    lv_obj_set_flex_flow(s_net_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_net_list, 8, 0);
    lv_obj_set_scroll_dir(s_net_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_net_list, LV_SCROLLBAR_MODE_OFF);

    // Password panel: covers the whole screen, keyboard at the bottom.
    s_pass_panel = sqBox(s_wifi, SQ_W, SQ_H, SQ_BG, 0);
    lv_obj_set_clickable(s_pass_panel, true);
    lv_obj_t* ph = sqHeader(s_pass_panel, nullptr, SQ_LILAC, false);
    lv_obj_t* cancel = sqRoundButton(ph, 48, SQ_RAISED, IC_CLOSE, &font_sqi_26, SQ_TEXT);
    lv_obj_add_event_cb(cancel, passCancel, LV_EVENT_CLICKED, nullptr);
    s_pass_title = sqLabel(ph, "", &font_sq_18, SQ_LILAC);
    lv_label_set_long_mode(s_pass_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_pass_title, 1);
    lv_obj_t* field = sqBox(s_pass_panel, SQ_W - 32, 60, SQ_SURFACE, 30);
    lv_obj_set_pos(field, 16, SQ_HEADER_H);
    lv_obj_set_style_border_width(field, 3, 0);
    lv_obj_set_style_border_color(field, lv_color_hex(SQ_LILAC), 0);
    s_pass_ta = lv_textarea_create(field);
    lv_obj_remove_style_all(s_pass_ta);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_obj_set_size(s_pass_ta, SQ_W - 72, 40);
    lv_obj_align(s_pass_ta, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_set_style_text_font(s_pass_ta, &font_sq_24, 0);
    lv_obj_set_style_text_color(s_pass_ta, lv_color_hex(SQ_TEXT), 0);
    lv_obj_set_style_border_color(s_pass_ta, lv_color_hex(SQ_LILAC), LV_PART_CURSOR);
    lv_obj_set_style_border_width(s_pass_ta, 3, LV_PART_CURSOR);
    lv_obj_set_style_border_side(s_pass_ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_add_state(s_pass_ta, LV_STATE_FOCUSED);
    lv_obj_t* kb = sqKeyboardCreate(s_pass_panel, s_pass_ta, "OK", SQ_LILAC, connectClicked);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);
    sqSetHidden(s_pass_panel, true);

    sqRegisterScreen(SQ_SCR_WIFI, s_wifi, SQ_TAB_NONE);
}

void sqWifiOpen(void) {
    sqShow(SQ_SCR_WIFI);
    scanStart();
}

void sqWifiPoll(void) {
    if (s_scanning) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            s_scanning = false;
            lv_obj_clean(s_net_list);
            // Strongest first, one row per SSID (meshes repeat it).
            String seen;
            for (int pass = 0; pass < n; ++pass) {
                int best = -1;
                for (int i = 0; i < n; ++i) {
                    String s = WiFi.SSID(i);
                    if (!s.length() || seen.indexOf("\x1f" + s + "\x1f") >= 0) continue;
                    if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
                }
                if (best < 0) break;
                String s = WiFi.SSID(best);
                seen += "\x1f" + s + "\x1f";
                lv_obj_t* b = lv_button_create(s_net_list);
                lv_obj_remove_style_all(b);
                lv_obj_set_size(b, LV_PCT(100), 60);
                lv_obj_set_style_radius(b, 20, 0);
                lv_obj_set_style_bg_color(b, lv_color_hex(SQ_ROW), 0);
                lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
                lv_obj_set_style_pad_hor(b, 16, 0);
                sqPressFeedback(b);
                lv_obj_t* ic = sqIcon(b, IC_WIFI, &font_sqi_26, WiFi.RSSI(best) > -67 ? SQ_MINT : SQ_MUTED);
                lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
                lv_obj_t* l = sqLabel(b, s.c_str(), &font_sq_20, SQ_TEXT);
                lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
                lv_obj_set_width(l, 340);
                lv_obj_align(l, LV_ALIGN_LEFT_MID, 44, 0);
                lv_obj_add_event_cb(b, netClicked, LV_EVENT_CLICKED, nullptr);
            }
            lv_label_set_text_fmt(s_status, "%s", WiFi.status() == WL_CONNECTED
                                                     ? ("Connecté à " + WiFi.SSID()).c_str()
                                                     : "Choisis ton réseau");
        }
    }
    if (s_connect_started) {
        if (WiFi.status() == WL_CONNECTED) {
            s_connect_started = 0;
            wifiPrefs.putString(NVS_KEY_SSID, pending_wifi_ssid);
            wifiPrefs.putString(NVS_KEY_PASSWORD, pending_wifi_pass);
            wifi_creds_need_save = false;
            lv_label_set_text_fmt(s_status, "Connecté à « %s »", WiFi.SSID().c_str());
            sqToast("Wi-Fi connecté !");
            extern void sqOnWifiConnected(void);
            sqOnWifiConnected();
        } else if (millis() - s_connect_started > 20000) {
            s_connect_started = 0;
            lv_label_set_text(s_status, "Ça n'a pas marché. Vérifie le mot de passe.");
            sqToast("Connexion impossible");
        }
    }
}
