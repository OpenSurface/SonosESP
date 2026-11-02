/**
 * Portrait navigation frame — tab bar, dock, Settings menu.
 *
 * UI_PORTRAIT builds only (see ui_portrait_nav.h). Drawn from the "Portrait"
 * row of the redesign canvas: five tabs along the bottom, the now-playing dock
 * above them on every screen but the player, and the old Settings → Sources
 * page promoted to its own Library tab.
 *
 * createSettingsSidebar() is implemented here for portrait, with the landscape
 * signature, so the existing settings screens build unchanged into a frame that
 * fits a 480-wide screen: a "‹ Settings" back bar instead of the rail.
 */

#include "config.h"

#if UI_PORTRAIT

#include "ui_common.h"
#include "ui_fonts.h"
#include "amber.h"
#include "amber_icons.h"
#include "ui_icons.h"
#include "ui_settings_card.h"
#include "ui_portrait_nav.h"

lv_obj_t* scr_settings_menu = nullptr;

// ── Search glyph ────────────────────────────────────────────────────────────
// Neither icon font carries a magnifier, and regenerating them for one glyph
// is a tooling detour. A ring and a handle draw it at any size. lv_line keeps
// a POINTER to its points, so each size gets its own static pair.
lv_obj_t* portraitSearchGlyph(lv_obj_t* parent, int size, lv_color_t col) {
    static lv_point_precise_t pts[4][2];
    static int pts_size[4] = {0, 0, 0, 0};
    int slot = 0;
    while (slot < 4 && pts_size[slot] && pts_size[slot] != size) slot++;
    if (slot == 4) slot = 3;

    const int ring   = size * 64 / 100;
    const int stroke = size >= 40 ? SMIN(4) : SMIN(2) + 1;
    if (pts_size[slot] != size) {
        pts_size[slot] = size;
        const int c = ring * 85 / 100;   // where the handle leaves the ring
        pts[slot][0] = { (lv_value_precise_t)c, (lv_value_precise_t)c };
        pts[slot][1] = { (lv_value_precise_t)(size - stroke), (lv_value_precise_t)(size - stroke) };
    }

    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_scrollable(box, false);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* r = lv_obj_create(box);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, ring, ring);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r, stroke, 0);
    lv_obj_set_style_border_color(r, col, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* h = lv_line_create(box);
    lv_line_set_points(h, pts[slot], 2);
    lv_obj_set_style_line_width(h, stroke, 0);
    lv_obj_set_style_line_color(h, col, 0);
    lv_obj_set_style_line_rounded(h, true, 0);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

// ── Tab bar ─────────────────────────────────────────────────────────────────
static const char* const TAB_LABELS[PTAB_COUNT] = {"Player", "Library", "Search", "Rooms", "Settings"};
static const char* const TAB_ICONS[PTAB_COUNT]  = {AMB_IC_MUSIC, AMB_IC_FOLDER, nullptr,
                                                   AMB_IC_SPEAKER, AMB_IC_GEAR};

// A tab always lands on its ROOT: Library from inside a browse level goes back
// to the source list, Rooms from Groups back to the room list. That is what the
// active tab is for once you are a few levels down.
static void tabClicked(lv_event_t* e) {
    const PortraitTab t = (PortraitTab)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* target = nullptr;
    switch (t) {
        case PTAB_PLAYER:   target = scr_main;          break;
        case PTAB_LIBRARY:  target = scr_sources;       break;
        case PTAB_SEARCH:   target = scr_search;        break;
        case PTAB_ROOMS:    target = scr_devices;       break;
        case PTAB_SETTINGS: target = scr_settings_menu; break;
        default: break;
    }
    // A plain load, for the reason ui_sidebar.cpp gives: each screen has its own
    // dock, and a cross-fade shows the incoming one before it is filled.
    if (target && target != lv_screen_active()) lv_screen_load(target);
}

static void buildTabBar(lv_obj_t* screen, PortraitTab active) {
    lv_obj_t* bar = lv_obj_create(screen);
    lv_obj_set_size(bar, SX(UI_DESIGN_W), SY(PNAV_TAB_H));
    lv_obj_set_pos(bar, 0, SY(UI_DESIGN_H - PNAV_TAB_H));
    lv_obj_set_style_bg_color(bar, AMB_PANEL, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bar, AMB_LINE, 0);
    lv_obj_set_style_pad_hor(bar, SX(4), 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_scrollable(bar, false);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < PTAB_COUNT; i++) {
        const bool on = (i == (int)active);
        const lv_color_t col = on ? AMB_ACCENT : AMB_TEXT3;

        lv_obj_t* b = lv_button_create(bar);
        lv_obj_set_size(b, SX(88), SY(52));
        lv_obj_set_style_radius(b, SMIN(12), 0);
        lv_obj_set_style_bg_color(b, AMB_ACCENT_WASH, 0);
        lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(b, AMB_CARD, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, SY(3), 0);
        lv_obj_add_event_cb(b, tabClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        if (TAB_ICONS[i]) {
            lv_obj_t* ico = lv_label_create(b);
            lv_label_set_text(ico, TAB_ICONS[i]);
            lv_obj_set_style_text_font(ico, &font_icon_24, 0);
            lv_obj_set_style_text_color(ico, col, 0);
        } else {
            // Same height as an icon-font line, so "Search" sits on the same
            // baseline as the other tab labels instead of 3px higher.
            lv_obj_t* hold = lv_obj_create(b);
            lv_obj_remove_style_all(hold);
            lv_obj_set_size(hold, SMIN(24), lv_font_get_line_height(&font_icon_24));
            lv_obj_remove_flag(hold, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_scrollable(hold, false);
            lv_obj_center(portraitSearchGlyph(hold, SMIN(22), col));
        }

        lv_obj_t* lbl = lv_label_create(b);
        lv_label_set_text(lbl, TAB_LABELS[i]);
        lv_obj_set_style_text_font(lbl, &font_text_12, 0);
        lv_obj_set_style_text_color(lbl, col, 0);
    }
}

lv_obj_t* portraitFrame(lv_obj_t* screen, PortraitTab active, bool with_dock) {
    const int free_h = portraitFreeH(with_dock);

    lv_obj_t* content = lv_obj_create(screen);
    lv_obj_set_size(content, SX(UI_DESIGN_W), SY(free_h));
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_style_bg_color(content, AMB_BG, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scrollable(content, false);

    if (with_dock) nowDockBuild(screen, free_h, PNAV_DOCK_H);
    buildTabBar(screen, active);
    return content;
}

// Shared content-area style: the scrollbar the landscape sidebar styles once
// for every settings screen.
static void styleContent(lv_obj_t* content) {
    lv_obj_set_style_bg_color(content, AMB_BG, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, SMIN(SETTINGS_CONTENT_PAD), 0);
    lv_obj_set_style_bg_color(content, AMB_BORDER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(content, SX(6), LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, SX(3), LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(content, SX(2), LV_PART_SCROLLBAR);
}

// ── createSettingsSidebar(), portrait ───────────────────────────────────────
// Same indices as the landscape rail (General, Speakers, Groups, Sources,
// Display, WiFi, Clock, Update), mapped onto tabs. Speakers is the Rooms tab
// itself and Sources the Library tab, so those two have no back bar.
struct PortraitHome { PortraitTab tab; const char* back_label; };
static const PortraitHome SB_HOME[9] = {
    {PTAB_SETTINGS, "Settings"},   // 0 General
    {PTAB_ROOMS,    nullptr},      // 1 Speakers = Rooms tab root
    {PTAB_ROOMS,    "Rooms"},      // 2 Groups
    {PTAB_LIBRARY,  nullptr},      // 3 Sources  = Library tab root (and Browse)
    {PTAB_SETTINGS, "Settings"},   // 4 Display
    {PTAB_SETTINGS, "Settings"},   // 5 WiFi
    {PTAB_SETTINGS, "Settings"},   // 6 Clock
    {PTAB_SETTINGS, "Settings"},   // 7 Update
    {PTAB_SEARCH,   nullptr},      // 8 Browse opened from Search (SB_IDX_SEARCH_BROWSE)
};

static void backClicked(lv_event_t* e) {
    const PortraitTab t = (PortraitTab)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* target = (t == PTAB_ROOMS) ? scr_devices : scr_settings_menu;
    if (target) lv_screen_load(target);
}

lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx) {
    if (activeIdx < 0 || activeIdx > SB_IDX_SEARCH_BROWSE) activeIdx = 0;
    const PortraitHome& home = SB_HOME[activeIdx];
    lv_obj_t* frame = portraitFrame(screen, home.tab, true);

    int top = 0;
    if (home.back_label) {
        lv_obj_t* back = lv_button_create(frame);
        lv_obj_set_size(back, LV_SIZE_CONTENT, SY(PNAV_BACK_H));
        lv_obj_set_pos(back, SX(8), 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(back, AMB_CARD, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_radius(back, SMIN(10), 0);
        lv_obj_set_style_shadow_width(back, 0, 0);
        lv_obj_set_style_border_width(back, 0, 0);
        lv_obj_set_style_pad_hor(back, SX(8), 0);
        lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(back, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(back, SX(6), 0);
        lv_obj_add_event_cb(back, backClicked, LV_EVENT_CLICKED, (void*)(intptr_t)home.tab);

        lv_obj_t* ico = lv_label_create(back);
        lv_label_set_text(ico, MDI_ARROW_LEFT);
        lv_obj_set_style_text_font(ico, &font_icon_16, 0);
        lv_obj_set_style_text_color(ico, AMB_ACCENT, 0);

        lv_obj_t* lbl = lv_label_create(back);
        lv_label_set_text(lbl, home.back_label);
        lv_obj_set_style_text_font(lbl, &font_text_14, 0);
        lv_obj_set_style_text_color(lbl, AMB_ACCENT, 0);
        top = PNAV_BACK_H;
    }

    lv_obj_t* content = lv_obj_create(frame);
    lv_obj_set_size(content, SX(UI_DESIGN_W), SY(portraitFreeH(true) - top));
    lv_obj_set_pos(content, 0, SY(top));
    styleContent(content);
    return content;
}

// ── Settings menu ───────────────────────────────────────────────────────────
// What the landscape rail listed, minus the two sections that became tabs.
struct MenuEntry { const char* icon; const char* label; lv_obj_t** screen; };
static const MenuEntry MENU[] = {
    {AMB_IC_GEAR,     "General",         &scr_general},
    {AMB_IC_DISPLAY,  "Display",         &scr_display},
    {AMB_IC_WIFI,     "WiFi",            &scr_wifi},
    {AMB_IC_CLOCK,    "Clock",           &scr_clock_settings},
    {AMB_IC_DOWNLOAD, "Firmware update", &scr_ota},
};
static const int MENU_COUNT = (int)(sizeof(MENU) / sizeof(MENU[0]));
#define MENU_ROW_H 60

static void menuClicked(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < MENU_COUNT && *MENU[i].screen) lv_screen_load(*MENU[i].screen);
}

void createSettingsMenuScreen() {
    scr_settings_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings_menu, AMB_BG, 0);
    lv_obj_t* frame = portraitFrame(scr_settings_menu, PTAB_SETTINGS, true);
    lv_obj_set_style_pad_all(frame, SMIN(20), 0);
    lv_obj_set_flex_flow(frame, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(frame, SY(4), 0);

    lv_obj_t* title = lv_label_create(frame);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &font_text_24, 0);
    lv_obj_set_style_text_color(title, AMB_TEXT, 0);

    lv_obj_t* ver = lv_label_create(frame);
    lv_label_set_text(ver, "v" FIRMWARE_VERSION " · " PANEL_SIZE_LABEL " portrait");
    lv_obj_set_style_text_font(ver, &font_text_12, 0);
    lv_obj_set_style_text_color(ver, AMB_TEXT3, 0);
    lv_obj_set_style_text_letter_space(ver, 1, 0);

    lv_obj_t* card = lv_obj_create(frame);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(card, SY(16), 0);
    lv_obj_set_style_bg_color(card, AMB_CARD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, AMB_BORDER, 0);
    lv_obj_set_style_radius(card, SMIN(14), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t* row = lv_button_create(card);
        lv_obj_set_size(row, lv_pct(100), SY(MENU_ROW_H));
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, AMB_RAISED, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_hor(row, SX(16), 0);
        lv_obj_set_style_border_width(row, i < MENU_COUNT - 1 ? 1 : 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, AMB_LINE_SOFT, 0);
        lv_obj_add_event_cb(row, menuClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* ico = lv_label_create(row);
        lv_label_set_text(ico, MENU[i].icon);
        lv_obj_set_style_text_font(ico, &font_icon_24, 0);
        lv_obj_set_style_text_color(ico, AMB_ACCENT, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, MENU[i].label);
        lv_obj_set_style_text_font(lbl, &font_text_16, 0);
        lv_obj_set_style_text_color(lbl, AMB_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SX(40), 0);

        lv_obj_t* chev = lv_label_create(row);
        lv_label_set_text(chev, MDI_CHEVRON_RIGHT);
        lv_obj_set_style_text_font(chev, &font_icon_16, 0);
        lv_obj_set_style_text_color(chev, AMB_TEXT3, 0);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

#endif  // UI_PORTRAIT
