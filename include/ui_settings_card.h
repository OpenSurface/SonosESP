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

// ── Content-area geometry ───────────────────────────────────────────────────
// createSettingsSidebar() returns a content area of SY(480) with SMIN(24) padding
// on every side, so the usable inner height is 480 - 24 - 24 = 432 design pixels.
// Children are positioned relative to that inner box, NOT to the full 480.
//
// Four screens got this wrong the same way — they sized their list to end at 455,
// having subtracted from 480 and forgotten the padding — so the last row of each
// list was clipped by 23px:
//
//   Speakers / Groups   pos SY(75) + height SY(380) = 455
//   Sources  / Browse   pos SY(50) + height SY(405) = 455
//
// SETTINGS_LIST_H(top) computes the height that reaches the bottom exactly. The
// arithmetic is exact on both panels: SMIN uses the smaller of the two scale
// factors, which is the vertical one (1.25 vs 1.28 on the 7"), so it matches SY.
// The content area is 424 tall, not the full 480: the now-playing dock added by
// createSettingsSidebar() takes the bottom 56 so transport control survives a
// trip into Settings. Every screen's list height derives from these, so that
// change landed without editing any of them — which is the reason the arithmetic
// was centralised here in the first place.
#define SETTINGS_CONTENT_H   424
#define SETTINGS_CONTENT_PAD 24
#define SETTINGS_INNER_H     (SETTINGS_CONTENT_H - 2 * SETTINGS_CONTENT_PAD)   // 376
#define SETTINGS_LIST_H(top) SY(SETTINGS_INNER_H - (top))

// Create a card container with a title + accent underline. Returns the card
// object so callers can add child controls (flex column layout, scroll disabled).
lv_obj_t* addCard(lv_obj_t* parent, const char* title);

// Small primary label inside a card (typically above a control).
void addSettingLabel(lv_obj_t* parent, const char* text);

// Secondary description text (wraps to card width).
lv_obj_t* addDescLabel(lv_obj_t* parent, const char* text);

// Styled switch matching the project's accent theme.
lv_obj_t* addSwitch(lv_obj_t* parent, bool initial);

// ── Setting rows ────────────────────────────────────────────────────────────
// From the "SonosESP Amber" design canvas: the control belongs to the RIGHT of
// its label, not underneath it.
//
// The stacked form (addSettingLabel → addDescLabel → addSwitch, each a separate
// full-width child) left the right half of every settings page empty while
// pushing content off the bottom, so Display and Clock both scrolled for want of
// space they already had. A row puts the label and description in a flexible
// left block and the control in a fixed slot on the right.
//
// addSettingRow returns the CONTROL SLOT — create the switch/dropdown/label in
// it, and it will sit right-aligned and vertically centred against the label.
//
//     lv_obj_t* slot = addSettingRow(card, "Show synced lyrics",
//                                    "Time-synced from LRCLIB. No API key.", true);
//     lv_obj_t* sw   = addSwitch(slot, lyrics_enabled);
//
// `separator` draws the hairline under the row; pass false for the last row in
// a card, which already has the card's own edge below it.
lv_obj_t* addSettingRow(lv_obj_t* parent, const char* title, const char* desc,
                        bool separator);

// The description label of the row owning `slot`, so callers can retarget its
// text later (the theme and clock-face pickers rewrite theirs on change).
// Returns nullptr when the row was created without a description.
lv_obj_t* settingRowDesc(lv_obj_t* slot);

// A slider's label, current value and track, laid out as the canvas draws them:
// title on the left and the accent value right-aligned on the same line, with
// the full-width track beneath. Returns the row — pass it to addSlider(). The
// accent value label is written to *out_value for the event callback to update.
lv_obj_t* addSliderRow(lv_obj_t* parent, const char* title, const char* desc,
                       bool separator, lv_obj_t** out_value);

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
