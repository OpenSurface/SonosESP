#pragma once
/**
 * Settings card helpers — shared dark-theme building blocks for the settings
 * screens (General, Clock, …). Each screen calls addCard() to create a grouped
 * section, then drops controls inside via addSettingLabel/addDescLabel/addSwitch.
 *
 * Implementations live in src/screens/ui_settings_card.cpp.
 */
#include "lvgl.h"

// Dark-theme tokens (slightly elevated from screen bg = 0x121212)
#define SET_CARD_BG     lv_color_hex(0x1A1A1A)
#define SET_CARD_BORDER lv_color_hex(0x2A2A2A)

// Create a card container with a title + accent underline. Returns the card
// object so callers can add child controls (flex column layout, scroll disabled).
lv_obj_t* addCard(lv_obj_t* parent, const char* title);

// Small primary label inside a card (typically above a control).
void addSettingLabel(lv_obj_t* parent, const char* text);

// Secondary description text (wraps to card width).
lv_obj_t* addDescLabel(lv_obj_t* parent, const char* text);

// Styled switch matching the project's accent theme.
lv_obj_t* addSwitch(lv_obj_t* parent, bool initial);
