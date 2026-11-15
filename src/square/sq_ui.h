#pragma once
/**
 * sq_ui.h — the kids UI (UI_KIDS builds): the square 480x480 S3 panel and the
 * 480x800 P4 portrait panel. Same screens; SQ_TALL picks the portrait layouts.
 *
 * One colour per destination, so a child who cannot read yet still finds the
 * way: pink = Lecture, orange = Chercher, yellow = Favoris, mint = Spotify,
 * blue = Enceintes. Navy ground, rounded Nunito, chunky buttons.
 *
 * Threading: every lv_* call happens on the UI loop (loop() in main_square.cpp).
 * Network work (SOAP, SMAPI, artwork) runs on worker tasks, which hand results
 * back through the small mailboxes in sq_work.h and never touch LVGL.
 */
#include <Arduino.h>
#include "lvgl.h"
#include "config.h"   // DISPLAY_WIDTH / DISPLAY_HEIGHT

#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(font_sq_14)
LV_FONT_DECLARE(font_sq_16)
LV_FONT_DECLARE(font_sq_18)
LV_FONT_DECLARE(font_sq_20)
LV_FONT_DECLARE(font_sq_24)
LV_FONT_DECLARE(font_sq_28)
LV_FONT_DECLARE(font_sq_34)
LV_FONT_DECLARE(font_sqi_20)
LV_FONT_DECLARE(font_sqi_26)
LV_FONT_DECLARE(font_sqi_32)
LV_FONT_DECLARE(font_sqi_44)
#ifdef __cplusplus
}
#endif

// ── Palette ─────────────────────────────────────────────────────────────────
#define SQ_BG        0x16133A   // night navy
#define SQ_SURFACE   0x231F52   // cards
#define SQ_ROW       0x1F1B4D   // list rows
#define SQ_RAISED    0x2E2A66   // round buttons, keys, chips
#define SQ_RAISED2   0x3A3578   // special keys
#define SQ_DOCK      0x1E1A4A
#define SQ_INK       0x16133A   // text on a coloured fill
#define SQ_TEXT      0xFFF7EA
#define SQ_MUTED     0xB7B1E3
#define SQ_PINK      0xFF7AB6   // Lecture
#define SQ_ORANGE    0xFF9A3C   // Chercher
#define SQ_YELLOW    0xFFD23F   // Favoris
#define SQ_MINT      0x3BE0A0   // Spotify
#define SQ_SKY       0x6CB8FF   // Enceintes
#define SQ_LILAC     0xB69CFF
#define SQ_SKY_DEEP  0x22396B   // speaker pill
#define SQ_SKY_TEXT  0xDDEEFF

// ── Icons (Material Design Icons, UTF-8) ────────────────────────────────────
#define IC_NOTE      "\xF3\xB0\x8E\x87"   // F0387 music-note
#define IC_SEARCH    "\xF3\xB0\x8D\x89"   // F0349 magnify
#define IC_STAR      "\xF3\xB0\x93\x8E"   // F04CE star
#define IC_STAR_O    "\xF3\xB0\x93\x92"   // F04D2 star-outline
#define IC_ALBUM     "\xF3\xB0\x80\xA5"   // F0025 album
#define IC_SPEAKER   "\xF3\xB0\x93\x83"   // F04C3 speaker
#define IC_PLAY      "\xF3\xB0\x90\x8A"   // F040A play
#define IC_PAUSE     "\xF3\xB0\x8F\xA4"   // F03E4 pause
#define IC_PREV      "\xF3\xB0\x92\xAE"   // F04AE skip-previous
#define IC_NEXT      "\xF3\xB0\x92\xAD"   // F04AD skip-next
#define IC_BACK      "\xF3\xB0\x85\x81"   // F0141 chevron-left
#define IC_DOWN      "\xF3\xB0\x85\x80"   // F0140 chevron-down
#define IC_RIGHT     "\xF3\xB0\x85\x82"   // F0142 chevron-right
#define IC_PLUS      "\xF3\xB0\x90\x95"   // F0415 plus
#define IC_MINUS     "\xF3\xB0\x8D\xB4"   // F0374 minus
#define IC_CLOSE     "\xF3\xB0\x85\x96"   // F0156 close
#define IC_BKSP      "\xF3\xB0\xAD\x9C"   // F0B5C backspace-outline
#define IC_HOME      "\xF3\xB0\x9A\xA1"   // F06A1 home-outline
#define IC_TUNE      "\xF3\xB1\x95\x82"   // F1542 tune-variant
#define IC_REFRESH   "\xF3\xB0\x91\x90"   // F0450 refresh
#define IC_CHECK     "\xF3\xB0\x84\xAC"   // F012C check
#define IC_SOFA      "\xF3\xB1\x95\xAD"   // F156D sofa-outline
#define IC_POT       "\xF3\xB0\x8C\xA6"   // F0326 pot-steam-outline
#define IC_BED       "\xF3\xB0\x82\x99"   // F0099 bed-outline
#define IC_TEDDY     "\xF3\xB1\xA3\xBB"   // F18FB teddy-bear
#define IC_MONITOR   "\xF3\xB0\x8D\xB9"   // F0379 monitor
#define IC_SUN       "\xF3\xB0\x96\xA8"   // F05A8 white-balance-sunny
#define IC_HEART     "\xF3\xB0\x8B\x91"   // F02D1 heart
#define IC_PLAYLIST  "\xF3\xB0\xB2\xB8"   // F0CB8 playlist-music
#define IC_SPARKLE   "\xF3\xB0\x99\xB4"   // F0674 creation
#define IC_RADIO     "\xF3\xB0\x90\xB9"   // F0439 radio
#define IC_LINK      "\xF3\xB0\x8C\xB9"   // F0339 link-variant
#define IC_VOLUME    "\xF3\xB0\x95\xBE"   // F057E volume-high
#define IC_MOON      "\xF3\xB0\x96\x94"   // F0594 weather-night
#define IC_DOWNLOAD  "\xF3\xB0\x87\x9A"   // F01DA download
#define IC_BRIGHT    "\xF3\xB0\x83\x9F"   // F00DF brightness-6
#define IC_WIFI      "\xF3\xB0\x96\xA9"   // F05A9 wifi
#define IC_ARROW     "\xF3\xB0\x81\x94"   // F0054 arrow-right
#define IC_SPK_OFF   "\xF3\xB0\x93\x84"   // F04C4 speaker-off
#define IC_LIBRARY   "\xF3\xB0\x8C\xB3"   // F0333 music-box-multiple
#define IC_ARTIST    "\xF3\xB0\xA0\x83"   // F0803 account-music
#define IC_MIC       "\xF3\xB0\x8D\xAC"   // F036C microphone
#define IC_MIC_OFF   "\xF3\xB0\x8D\xAD"   // F036D microphone-off

// Bench: name any UI-thread call that takes long (a frozen screen).
#define SQ_SLOW(label, expr) ([&]() { uint32_t _t = millis(); auto _r = (expr); \
    uint32_t _d = millis() - _t; if (_d > 200) Serial.printf("[SLOW] %s %lums\n", label, (unsigned long)_d); \
    return _r; }())

// ── Destinations ────────────────────────────────────────────────────────────
enum SqScreen {
    SQ_SCR_PLAYER = 0,
    SQ_SCR_SEARCH,
    SQ_SCR_RESULTS,
    SQ_SCR_FAVS,
    SQ_SCR_SPOTIFY,
    SQ_SCR_BROWSE,
    SQ_SCR_ROOMS,
    SQ_SCR_SETTINGS,
    SQ_SCR_WIFI,
    SQ_SCR_LYRICS,
    SQ_SCR_COUNT
};

// Which dock tab a screen belongs to (-1: no dock on that screen).
enum SqTab { SQ_TAB_NONE = -1, SQ_TAB_PLAYER = 0, SQ_TAB_SEARCH, SQ_TAB_FAVS, SQ_TAB_SPOTIFY };

#define SQ_W        DISPLAY_WIDTH    // 480 on both panels
#define SQ_H        DISPLAY_HEIGHT   // 480 square, 800 portrait
#define SQ_TALL     (SQ_H > 600)     // portrait layouts (bigger cover, keys...)
#define SQ_HEADER_H 72
#define SQ_DOCK_H   84

// ── Building blocks (sq_widgets.cpp) ────────────────────────────────────────
lv_obj_t* sqScreenCreate(void);
lv_obj_t* sqBox(lv_obj_t* parent, int w, int h, uint32_t bg, int radius);
lv_obj_t* sqLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color);
lv_obj_t* sqIcon(lv_obj_t* parent, const char* icon, const lv_font_t* font, uint32_t color);
// A round (or pill) button holding one icon. Returns the button; its icon is child 0.
lv_obj_t* sqRoundButton(lv_obj_t* parent, int size, uint32_t bg, const char* icon,
                        const lv_font_t* icon_font, uint32_t fg);
lv_obj_t* sqPillButton(lv_obj_t* parent, int h, uint32_t bg, uint32_t fg, const char* icon,
                       const char* text, const lv_font_t* font);
// Header row with an optional back button and a coloured title. Returns the row.
lv_obj_t* sqHeader(lv_obj_t* scr, const char* title, uint32_t color, bool back);
// The blue "speaker" pill that opens the Enceintes screen; kept up to date by
// sqRoomPillsRefresh(). compact = room name only.
lv_obj_t* sqRoomPill(lv_obj_t* parent, bool compact);
void      sqRoomPillsRefresh(void);
// A flat "cover" placeholder in candy colours, chosen from a hash of `seed`.
lv_obj_t* sqCoverPlaceholder(lv_obj_t* parent, int size, int radius, const char* seed);
void      sqSetHidden(lv_obj_t* obj, bool hidden);
uint32_t  sqHash(const char* s);
void      sqPressFeedback(lv_obj_t* btn);   // darken on press, shared style

// ── Navigation (sq_nav.cpp) ─────────────────────────────────────────────────
void      sqNavInit(void);
void      sqShow(SqScreen scr);
void      sqBack(void);
SqScreen  sqCurrent(void);
lv_obj_t* sqScreenObj(SqScreen scr);
void      sqRegisterScreen(SqScreen id, lv_obj_t* obj, SqTab tab);
void      sqToast(const char* text);

// ── Screens ─────────────────────────────────────────────────────────────────
void sqPlayerCreate(void);
void sqPlayerUpdate(void);          // from the Sonos update queue
void sqPlayerTick(void);            // progress bar, ~4 Hz
// What the Lecture screen shows now (the lyrics screen follows it).
const char* sqPlayerTitle(void);
const char* sqPlayerArtist(void);
const char* sqPlayerArtUrl(void);
void sqLyricsCreate(void);
void sqLyricsOnShow(void);
void sqLyricsTick(void);           // ~10 Hz while the screen is up
void sqRoomsCreate(void);
void sqRoomsRefresh(void);
void sqFavsCreate(void);
void sqFavsRefresh(void);           // rebuild from the favourites store
void sqFavsCommit(void);            // send the star-removals; called on leaving Favoris
void sqSearchCreate(void);
void sqResultsCreate(void);
void sqResultsStart(const char* term);
void sqSpotifyCreate(void);
void sqBrowseCreate(void);
struct SvcItem;
// Opens a container: a Spotify root section, an album, a playlist, an artist.
void sqBrowseOpen(const SvcItem& item, uint32_t color);
void sqBrowseRefresh(void);
void sqSpotifyRefresh(void);
void sqResultsRefresh(void);
void sqSettingsCreate(void);
void sqWifiCreate(void);
void sqWifiOpen(void);
void sqUiPoll(void);                // per-loop: mailboxes from worker tasks

// ── SMAPI item rows (sq_items.cpp) — results, playlists, albums ───────────
struct SqItemRow {
    lv_obj_t* img;
    lv_obj_t* ph;
    lv_obj_t* star;
    char      art[256];
    char      fav_uri[256];   // the URI a favourite of this item would carry
    int       art_px;         // decoded size of the picture (48 in a row)
};
const char* sqItemKind(const SvcItem& it);
// A 62px row: thumbnail, title, "kind · subtitle", star. onTap/onStar get
// `index` as user data.
lv_obj_t* sqItemRowCreate(lv_obj_t* list, const SvcItem& it, int index, lv_event_cb_t onTap,
                          lv_event_cb_t onStar, SqItemRow* out);
void sqItemRowArt(SqItemRow* r);
void sqItemRowStar(SqItemRow* r);
// Star on a SMAPI item: add it to the Sonos favourites, or remove it.
void sqItemToggleFav(const SvcItem& it, const char* fav_uri);
// Tap on a SMAPI item: open a container, play a track.
void sqItemOpen(const SvcItem& it, uint32_t color);

// ── Keyboard (sq_keyboard.cpp) ──────────────────────────────────────────────
// AZERTY + a digits/symbols layer. `ta` receives the text; `on_enter` runs on
// the action key. The action key is labelled `action`.
lv_obj_t* sqKeyboardCreate(lv_obj_t* parent, lv_obj_t* ta, const char* action,
                           uint32_t action_color, lv_event_cb_t on_enter);
