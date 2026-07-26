/**
 * Synced Lyrics Display
 * Fetches time-synced lyrics from LRCLIB and displays them overlaid on album art
 */

#pragma once
#include <Arduino.h>
#include <lvgl.h>

#define MAX_LYRIC_LINES 100  // Reduced from 150 to save PSRAM (100 × 104 bytes = ~10KB)
#define MAX_LYRIC_TEXT 100

struct LyricLine {
    int time_ms;
    char text[MAX_LYRIC_TEXT];
};

// Lyrics are dynamically allocated in PSRAM to save DRAM
extern int lyric_count;
extern volatile bool lyrics_ready;
extern volatile bool lyrics_fetching;
extern volatile bool lyrics_abort_requested;  // Abort flag for rapid track changes
extern int current_lyric_index;

// Initialize lyrics system (allocates PSRAM buffer)
void initLyrics();

// Request lyrics for a track (spawns background fetch task).
// Returns true if a new task was spawned, false if the previous task is still
// running (caller should NOT update lyrics_last_track — retry next frame).
bool requestLyrics(const String& artist, const String& title, int durationSec);

// Clear lyrics and hide overlay
void clearLyrics();

// Create the lyrics overlay UI (called once from createMainScreen)
void createLyricsOverlay(lv_obj_t* parent);

// Update lyrics display based on playback position (called from updateUI)
void updateLyricsDisplay(int position_seconds);

// Show/hide lyrics overlay
void setLyricsVisible(bool show);

// Update lyrics status indicator (top left of album art)
void updateLyricsStatus();

// Text of the currently active lyric line, or "" when there is none.
// Lets a theme render lyrics its own way (see the Immersive theme) while
// updateLyricsDisplay() keeps doing the timing/index work.
const char* lyricsCurrentText();
