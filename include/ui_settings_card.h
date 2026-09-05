#pragma once
/**
 * Settings card helpers — shared dark-theme building blocks for the settings
 * screens (General, Clock, …). Each screen calls addCard() to create a grouped
 * section, then drops controls inside via addSettingLabel/addDescLabel/addSwitch.
 *
 * Implementations live in src/screens/ui_settings_card.cpp.
 */
#include "lvgl.h"

// Dark-theme tokens (slightly elevated from screen bg = COL_SCREEN).
// Aliases, not literals: these were duplicate 0x1A1A1A / 0x2A2A2A copies of
// COL_BG / COL_CARD, so retuning the palette silently left the settings cards
// behind. Both call sites already include ui_common.h, where COL_* is declared.
#define SET_CARD_BG     COL_BG
#define SET_CARD_BORDER COL_CARD

// Create a card container with a title + accent underline. Returns the card
// object so callers can add child controls (flex column layout, scroll disabled).
lv_obj_t* addCard(lv_obj_t* parent, const char* title);

// Small primary label inside a card (typically above a control).
void addSettingLabel(lv_obj_t* parent, const char* text);

// Secondary description text (wraps to card width).
lv_obj_t* addDescLabel(lv_obj_t* parent, const char* text);

// Styled switch matching the project's accent theme.
lv_obj_t* addSwitch(lv_obj_t* parent, bool initial);

// Accent-coloured current-value line (the "96%" / "1 min" under a setting label).
lv_obj_t* addValueLabel(lv_obj_t* parent, const char* text);

// Styled slider. Use this rather than hand-rolling lv_slider_create: the eleven
// style calls were duplicated in ui_display_screen.cpp (x3) and ui_clock_settings.cpp,
// and the copies had already drifted from each other's geometry.
lv_obj_t* addSlider(lv_obj_t* parent, int min, int max, int value);

// The screen title row every settings screen starts with, plus an optional
// right-aligned action button ("Scan"). Pass nullptr for action_text when the
// screen has no action; returns the button, or nullptr when there is none.
//
// There were three separate title patterns before this, putting the title's
// vertical centre at 14px, 20px or 22px depending on the screen, so the heading
// visibly jumped as you moved between tabs. See the .cpp for the breakdown.
lv_obj_t* addScreenHeader(lv_obj_t* parent, const char* title, const char* action_text);

// The action button's text label, for screens that retarget it (WiFi swaps
// "Scan" for "Scanning..."). Null-safe; returns nullptr for a header with no action.
lv_obj_t* screenHeaderActionLabel(lv_obj_t* action_btn);
