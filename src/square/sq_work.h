#pragma once
/**
 * sq_work.h — the square UI's background work (UI_SQUARE builds only).
 *
 * One worker task runs everything that blocks on the network: discovery,
 * favourites, playing an item, grouping speakers. A second task fetches and
 * decodes artwork. Neither touches LVGL: results land in small stores that the
 * UI reads on its own loop, keyed by version counters so a screen rebuilds only
 * when its data really changed.
 */
#include <Arduino.h>
#include "lvgl.h"
#include "music_services.h"

// ── Jobs ────────────────────────────────────────────────────────────────────
enum SqJobType {
    SQ_JOB_START_SONOS,      // cached speaker, else SSDP discovery, then tasks
    SQ_JOB_DISCOVER,         // force a fresh SSDP discovery (Réglages)
    SQ_JOB_SVC_LOAD,         // streaming accounts: flash cache, else the speaker
    SQ_JOB_SVC_REFRESH,      // re-read the accounts from the speaker, rewrite the cache
    SQ_JOB_FAVS_LOAD,        // browse FV:2, rewrite the flash copy
    SQ_JOB_PLAY_FAV,         // a = favourite index
    SQ_JOB_PLAY_ITEM,        // item = SMAPI item to play
    SQ_JOB_FAV_ADD_ITEM,     // item = SMAPI item to save as a Sonos favourite
    SQ_JOB_FAV_ADD_CURRENT,  // what is playing now
    SQ_JOB_FAV_REMOVE,       // text = FV:2/n
    SQ_JOB_SELECT_ROOM,      // a = device index
    SQ_JOB_TOGGLE_GROUP,     // a = device index (join the current room / leave)
    SQ_JOB_GROUP_ALL,
};

struct SqJob {
    SqJobType type;
    int       a;
    char      text[64];
    SvcItem*  item;          // heap copy, freed by the worker
};

void sqWorkStart(void);
bool sqWorkPost(SqJobType type, int a = 0, const char* text = nullptr, const SvcItem* item = nullptr);
bool sqWorkBusy(void);

// Toasts raised by the worker, shown by the UI loop.
bool sqWorkTakeToast(char* out, size_t n);

// ── Version counters the UI watches ─────────────────────────────────────────
extern volatile uint32_t sq_favs_version;
extern volatile uint32_t sq_rooms_version;
extern volatile uint32_t sq_svc_version;
extern volatile bool     sq_sonos_ready;

// ── Favourites store ────────────────────────────────────────────────────────
struct SqFav {
    char id[24];             // FV:2/n
    char title[96];
    char kind[16];           // Album / Playlist / Radio / Titre / Favori
    char art[256];
    char uri[256];           // <res>, for "is this already a favourite"
};
int  sqFavsCount(void);
bool sqFavsGet(int i, SqFav* out);
// The favourite whose URI is `uri`, or -1.
int  sqFavsFind(const char* uri);
// The URI a SMAPI item plays with (for sqFavsFind); "" when it cannot be built.
String sqItemUri(const SvcItem& item);
// The URI the player is on now, refreshed by the worker after a star.
extern char sq_current_fav_uri[256];

// ── Spotify (the one streaming account the UI shows) ───────────────────────
bool sqSpotify(int* sid, int* serial);
int  sqSpotifyAccounts(SvcSource* out, int max);
void sqSpotifyChoose(int serial);

// ── Artwork ─────────────────────────────────────────────────────────────────
// Decoded RGB565 images, square, cached by URL+size. sqArtGet() returns the
// image if it is ready, else queues it and returns nullptr; poll again later.
// The returned descriptor stays valid until the cache evicts it, which only
// happens for entries not requested for a while: callers re-get on refresh.
const lv_image_dsc_t* sqArtGet(const char* url, int size);
extern volatile uint32_t sq_art_version;   // bumps whenever an image lands
void sqArtStart(void);

// ── Lyrics (sq_lyrics.cpp) ──────────────────────────────────────────────────
// LRCLIB lyrics for what is playing. The player calls sqLyricsWant() on every
// update; only a new artist+title starts a fetch.
enum SqLyrState {
    SQ_LYR_NONE,      // nothing to look up (radio, TV, nothing playing)
    SQ_LYR_LOADING,
    SQ_LYR_SYNCED,    // lines cued to the music
    SQ_LYR_PLAIN,     // words without timing
    SQ_LYR_MISSING,   // looked up, none (or an instrumental)
};
void       sqLyricsStart(void);
void       sqLyricsWant(const char* artist, const char* title, int duration_s, bool eligible);
SqLyrState sqLyricsState(void);
extern volatile uint32_t sq_lyrics_version;   // bumps when the state or the lines change
// Reading the lines: hold the lock, and copy what you keep.
void        sqLyricsLock(void);
void        sqLyricsUnlock(void);
int         sqLyricsCount(void);
int         sqLyricsMs(int i);          // -1 for plain lyrics
const char* sqLyricsText(int i);
int         sqLyricsIndexAt(int pos_ms);
