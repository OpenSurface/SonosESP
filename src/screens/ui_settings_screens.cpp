/**
 * UI Settings Screens
 * Remaining screens: Queue, Sources, Browse, Settings redirect
 * (Other settings screens have been extracted to separate files)
 */

#include "ui_common.h"
#include "ui_settings_card.h"   // addScreenHeader() - shared title row
#include "config.h"
#include "ui_fonts.h"

// Forward declaration for sidebar (now in ui_sidebar.cpp)
lv_obj_t* createSettingsSidebar(lv_obj_t* screen, int activeIdx);

// ============================================================================
// Queue Screen
// ============================================================================
void refreshQueueList() {
    lv_obj_clean(list_queue);
    SonosDevice* d = sonos.getCurrentDevice();
    if (!d) { lv_label_set_text(lbl_queue_status, "No device"); return; }
    if (d->queueSize == 0) { lv_label_set_text(lbl_queue_status, "Queue is empty"); return; }

    // Show window range when we have a partial view, e.g. "Tracks 4–13 of 47"
    int firstTrack = d->queue[0].trackNumber;
    int lastTrack  = d->queue[d->queueSize - 1].trackNumber;
    if (d->totalTracks > 0 && d->queueSize < d->totalTracks) {
        lv_label_set_text_fmt(lbl_queue_status, "Tracks %d-%d of %d",
                              firstTrack, lastTrack, d->totalTracks);
    } else {
        lv_label_set_text_fmt(lbl_queue_status, "%d %s",
                              d->queueSize, d->queueSize == 1 ? "track" : "tracks");
    }

    for (int i = 0; i < d->queueSize; i++) {
        QueueItem* item = &d->queue[i];
        int trackNum = item->trackNumber;  // absolute 1-based position in the full queue
        bool isPlaying = (trackNum == d->currentTrackNumber);

        lv_obj_t* btn = lv_btn_create(list_queue);
        lv_obj_set_size(btn, SX(727), SY(60));  // Full width, uniform height
        lv_obj_set_style_bg_color(btn, isPlaying ? COL_CARD2 : COL_BG, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 0, 0);  // No rounded corners - clean list
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, SMIN(12), 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)trackNum);
        lv_obj_add_event_cb(btn, ev_queue_item, LV_EVENT_CLICKED, NULL);

        // Subtle left border for currently playing
        if (isPlaying) {
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_width(btn, 3, 0);
            lv_obj_set_style_border_color(btn, COL_ACCENT, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
        }

        // Play icon for currently playing track OR track number
        lv_obj_t* num = lv_label_create(btn);
        if (isPlaying) {
            lv_label_set_text(num, MDI_PLAY);
            lv_obj_set_style_text_font(num, &lv_font_mdi_16, 0);
        } else {
            lv_label_set_text_fmt(num, "%d", trackNum);
            lv_obj_set_style_text_font(num, &font_text_14, 0);
        }
        lv_obj_set_style_text_color(num, isPlaying ? COL_ACCENT : COL_TEXT2, 0);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, SX(5), 0);

        // Title - highlight when playing
        lv_obj_t* title = lv_label_create(btn);
        lv_label_set_text(title, item->title.c_str());
        lv_obj_set_style_text_color(title, isPlaying ? COL_ACCENT : COL_TEXT, 0);
        lv_obj_set_style_text_font(title, &font_text_16, 0);
        lv_obj_set_width(title, SX(610));
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, SX(45), SY(-11));

        // Artist - subtle gray
        lv_obj_t* artist = lv_label_create(btn);
        lv_label_set_text(artist, item->artist.c_str());
        lv_obj_set_style_text_color(artist, COL_TEXT2, 0);
        lv_obj_set_style_text_font(artist, &font_text_12, 0);
        lv_obj_set_width(artist, SX(610));
        lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
        lv_obj_align(artist, LV_ALIGN_LEFT_MID, SX(45), SY(11));
    }
}

void createQueueScreen() {
    scr_queue = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_queue, COL_BG, 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_queue);
    lv_obj_set_size(header, SX(800), SY(70));
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, COL_CARD2, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Playlist");
    lv_obj_set_style_text_font(lbl_title, &font_text_32, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, SX(30), 0);

    // Refresh button in header
    lv_obj_t* btn_refresh = lv_button_create(header);
    lv_obj_set_size(btn_refresh, SMIN(50), SMIN(50));
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, SX(-80), 0);
    lv_obj_set_style_bg_color(btn_refresh, COL_SELECTED, 0);
    lv_obj_set_style_radius(btn_refresh, 25, 0);
    lv_obj_set_style_shadow_width(btn_refresh, 0, 0);
    lv_obj_add_event_cb(btn_refresh, [](lv_event_t* e) {
        // Request a windowed fetch from the polling task (safe: no SOAP on UI thread).
        SonosDevice* d = sonos.getCurrentDevice();
        int start = 0;
        if (d && d->currentTrackNumber > 0) {
            start = d->currentTrackNumber - SONOS_QUEUE_BATCH_SIZE / 2;
            if (start < 0) start = 0;
            if (d->totalTracks > 0 && start + SONOS_QUEUE_BATCH_SIZE > d->totalTracks)
                start = d->totalTracks - SONOS_QUEUE_BATCH_SIZE;
            if (start < 0) start = 0;
        }
        queue_fetch_start_index = start;
        queue_fetch_requested   = true;
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(ico_refresh, MDI_REFRESH);
    lv_obj_set_style_text_color(ico_refresh, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_refresh, &lv_font_mdi_24, 0);
    lv_obj_center(ico_refresh);

    // Close button in header
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, SMIN(50), SMIN(50));
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, SX(-20), 0);
    lv_obj_set_style_bg_color(btn_close, COL_SELECTED, 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, MDI_CLOSE);
    lv_obj_set_style_text_color(ico_close, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_mdi_24, 0);
    lv_obj_center(ico_close);

    // Status label below header
    lbl_queue_status = lv_label_create(scr_queue);
    lv_obj_align(lbl_queue_status, LV_ALIGN_TOP_LEFT, SX(40), SY(85));
    lv_label_set_text(lbl_queue_status, "Loading...");
    lv_obj_set_style_text_color(lbl_queue_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_queue_status, &font_text_14, 0);

    // Queue list - modern clean design
    list_queue = lv_list_create(scr_queue);
    lv_obj_set_size(list_queue, SX(730), SY(360));
    lv_obj_set_pos(list_queue, SX(35), SY(115));
    lv_obj_set_style_bg_color(list_queue, COL_BG, 0);
    lv_obj_set_style_border_width(list_queue, 0, 0);
    lv_obj_set_style_radius(list_queue, 0, 0);
    lv_obj_set_style_pad_all(list_queue, 0, 0);
    lv_obj_set_style_pad_row(list_queue, 0, 0);  // No spacing between items

    // Modern thin scrollbar on the right edge
    lv_obj_set_style_pad_right(list_queue, SX(3), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_queue, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_queue, COL_ACCENT, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list_queue, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_queue, 0, LV_PART_SCROLLBAR);
}

// ============================================================================
// Settings Screen (just redirects to Speakers)
// ============================================================================
void createSettingsScreen() {
    // Settings screen just redirects to Speakers screen (which has the sidebar)
    // scr_settings will point to scr_devices so clicking Settings button loads Speakers
    if (!scr_devices) {
        createDevicesScreen();
    }
    scr_settings = scr_devices;  // Point to the same screen
}

// ============================================================================
// Sources Screen
// ============================================================================
// The source list is whatever the player reports from Browse("0"), not a
// hardcoded list. Households differ — no library share, no favorites, no saved
// queues — and a static list produces rows that do nothing when tapped.
//
// Display metadata for the containers Sonos returns at the root. The device's
// own titles are unusable: it calls the music library "Attributes". Anything NOT
// in this table still appears, using whatever title the device gave it, so a
// container Sonos adds later shows up without a firmware change.
struct SourceMeta {
    const char* rootID;      // id as returned by Browse("0")
    const char* browseID;    // what we actually browse when the row is tapped
    const char* label;
    const char* icon;
};

static const SourceMeta SOURCE_META[] = {
    {"A:",  "A:",    "Music Library",   MDI_MUSIC_BOX},
    {"S:",  "S:",    "Music Shares",    MDI_FOLDER},
    {"SQ:", "SQ:",   "Sonos Playlists", MDI_PLAYLIST},
    // FV: contains a single child, FV:2, itself titled "Favorites" — so browsing
    // FV: costs a tap to reach a row with the same name as the one just tapped.
    // FV:2 is the conventional Sonos favorites container and is what other
    // controllers use. If a household ever differs, the row lands on the existing
    // "No items found" state rather than failing.
    {"FV:", "FV:2",  "Favorites",       MDI_MUSIC_NOTE},
    // R: reports TotalMatches=0 at its OWN root — browsing it shows nothing at
    // all — while the content lives one level down at R:0, which holds BOTH
    // "Radio Stations" (R:0/0) and "Radio Shows" (R:0/1).
    //
    // Deliberately R:0 and not R:0/0: jumping straight to the stations saves a
    // tap but silently hides Radio Shows, which is where podcasts live. Costing
    // one tap to not hide a whole category is the right trade.
    {"R:",  "R:0",   "Internet Radio",  MDI_RADIO},
    // Q: lists "Queue Instance 0/1" wrappers nobody wants to see. Q:0 is the
    // queue that is actually playing.
    {"Q:",  "Q:0",   "Queue",           MDI_SPEAKER},
};
static const int SOURCE_META_COUNT = sizeof(SOURCE_META) / sizeof(SOURCE_META[0]);

static lv_obj_t* sources_list = nullptr;

// ── Browse navigation trail ─────────────────────────────────────────────────
// createBrowseScreen() rebuilds the screen for every container and keeps only
// current_browse_id, so "where did I come from" has to be recorded explicitly or
// there is no way back out of a nested container except the sidebar. Fixed depth:
// Sonos trees are shallow in practice, and this avoids allocating per navigation.
#define BROWSE_STACK_MAX 8
static String browse_stack_id[BROWSE_STACK_MAX];
static String browse_stack_title[BROWSE_STACK_MAX];
static int    browse_depth = 0;

// Enter a top-level source. Clears the trail, so Back leaves for the Sources list.
static void browseEnterRoot(const String& id, const String& title) {
    browse_depth = 0;
    current_browse_id    = id;
    current_browse_title = title;
    createBrowseScreen();
    lv_screen_load(scr_browse);
}

// Descend into a child container, remembering the level being left.
static void browseDescend(const String& id, const String& title) {
    if (browse_depth < BROWSE_STACK_MAX) {
        browse_stack_id[browse_depth]    = current_browse_id;
        browse_stack_title[browse_depth] = current_browse_title;
        browse_depth++;
    }
    // Past the cap the trail simply stops growing rather than blocking the tap:
    // Back then surfaces one level higher than expected, which is a far better
    // failure than a row that does nothing.
    current_browse_id    = id;
    current_browse_title = title;
    createBrowseScreen();
    lv_screen_load(scr_browse);
}

// Back arrow: up one container, or out to the Sources list at the top.
static void browseBack(void) {
    if (browse_depth > 0) {
        browse_depth--;
        current_browse_id    = browse_stack_id[browse_depth];
        current_browse_title = browse_stack_title[browse_depth];
        createBrowseScreen();
        lv_screen_load(scr_browse);
    } else {
        lv_screen_load(scr_sources);
    }
}

// Each row owns a heap copy of the ObjectID to browse. Freed when LVGL destroys
// the button, for any reason — same contract as the browse rows below.
static void sourceRowDeleteCb(lv_event_t* e) {
    void* p = lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    if (p) free(p);
}

// Rebuilt on every screen open rather than once at boot: createSourcesScreen()
// runs during setup(), before the Sonos is necessarily discovered, so a list
// built there would stay empty forever. One Browse per visit, and only when the
// user actually opens the screen — nothing is spent at boot.
static void refreshSourcesList(lv_event_t* e) {
    if (!sources_list) return;
    lv_obj_clean(sources_list);   // per-row delete cbs free their ObjectIDs

    String didl = sonos.browseContent("0");
    Serial.printf("[SOURCES] root DIDL length=%d\n", didl.length());

    int searchPos = 0, shown = 0;
    while (searchPos < (int)didl.length()) {
        int cPos = didl.indexOf("<container", searchPos);
        if (cPos < 0) break;
        int endPos = didl.indexOf("</container>", cPos);
        if (endPos < 0) break;

        String itemXML = didl.substring(cPos, endPos + 12);
        // decodeHTML again: the DIDL arrives double-escaped. browseContent()
        // decodes once, which turns it into valid XML, but an & inside a title
        // was escaped a second time to survive that — so "R&B" is still sitting
        // there as "R&amp;B" until this pass. Same for any name with & < > or a
        // quote in it.
        String title   = sonos.decodeHTML(sonos.extractXML(itemXML, "dc:title"));
        int idStart = itemXML.indexOf("id=\"") + 4;
        int idEnd   = itemXML.indexOf("\"", idStart);
        String id   = itemXML.substring(idStart, idEnd);
        searchPos = endPos + 12;

        const SourceMeta* meta = nullptr;
        for (int i = 0; i < SOURCE_META_COUNT; i++) {
            if (id == SOURCE_META[i].rootID) { meta = &SOURCE_META[i]; break; }
        }

        const char* label    = meta ? meta->label    : title.c_str();
        const char* icon     = meta ? meta->icon     : MDI_FOLDER;
        String      browseID = meta ? meta->browseID : id;
        if (browseID.length() == 0) continue;

        char* idCopy = (char*)malloc(browseID.length() + 1);
        if (!idCopy) { Serial.println("[SOURCES] malloc failed"); break; }
        strcpy(idCopy, browseID.c_str());

        lv_obj_t* btn = lv_btn_create(sources_list);
        lv_obj_set_size(btn, lv_pct(100), SY(50));
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, SMIN(15), 0);
        lv_obj_set_user_data(btn, idCopy);
        lv_obj_add_event_cb(btn, sourceRowDeleteCb, LV_EVENT_DELETE, NULL);

        lv_obj_t* ico = lv_label_create(btn);
        lv_label_set_text(ico, icon);
        lv_obj_set_style_text_color(ico, COL_ACCENT, 0);
        lv_obj_set_style_text_font(ico, &lv_font_mdi_24, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, SX(5), 0);

        // Child index 1 — the click handler reads the title back from here.
        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, label);
        lv_obj_set_style_text_color(name, COL_TEXT, 0);
        lv_obj_set_style_text_font(name, &font_text_20, 0);
        lv_obj_set_width(name, SX(340));   // widened with the 18->20px font so the
                                           // same number of characters stays visible
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, SX(40), 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            lv_obj_t* b = (lv_obj_t*)lv_event_get_target(ev);
            const char* objID = (const char*)lv_obj_get_user_data(b);
            if (!objID) return;
            browseEnterRoot(String(objID),
                            String(lv_label_get_text(lv_obj_get_child(b, 1))));
        }, LV_EVENT_CLICKED, NULL);

        Serial.printf("[SOURCES] %s -> browse %s\n", label, browseID.c_str());
        shown++;
    }

    // Line-In is not in the browse tree at all — ObjectID "AI:" returns nothing on
    // S2 — so it cannot appear from Browse("0") no matter what. It is played by
    // pointing the transport straight at x-rincon-stream:<rincon>, so it gets a
    // synthetic row with its own handler rather than a browse target.
    SonosDevice* dev = sonos.getCurrentDevice();
    if (dev && dev->hasLineIn) {
        lv_obj_t* btn = lv_btn_create(sources_list);
        lv_obj_set_size(btn, lv_pct(100), SY(50));
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, SMIN(15), 0);

        lv_obj_t* ico = lv_label_create(btn);
        // MDI_WAVEFORM is the line-in hero glyph but exists only at 40/80, so at
        // mdi_24 it renders as a tofu box. MDI_BROADCAST is in this size.
        lv_label_set_text(ico, MDI_BROADCAST);
        lv_obj_set_style_text_color(ico, COL_ACCENT, 0);
        lv_obj_set_style_text_font(ico, &lv_font_mdi_24, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, SX(5), 0);

        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, "Line-In");
        lv_obj_set_style_text_color(name, COL_TEXT, 0);
        lv_obj_set_style_text_font(name, &font_text_20, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, SX(40), 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            SonosDevice* d = sonos.getCurrentDevice();
            if (!d) return;
            String uri = "x-rincon-stream:" + d->rinconID;
            Serial.printf("[SOURCES] Line-In -> %s\n", uri.c_str());
            sonos.playURI(uri.c_str(), "");
            lv_screen_load(scr_main);
        }, LV_EVENT_CLICKED, NULL);
        shown++;
    }

    if (shown == 0) {
        lv_obj_t* lbl = lv_label_create(sources_list);
        lv_label_set_text(lbl, sonos.getCurrentDevice()
                               ? "No sources found"
                               : "No Sonos device connected");
        lv_obj_set_style_text_color(lbl, COL_TEXT2, 0);
        lv_obj_set_style_text_font(lbl, &font_text_16, 0);
    }
    Serial.printf("[SOURCES] %d source(s) listed\n", shown);
}

void createSourcesScreen() {
    scr_sources = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sources, COL_SCREEN, 0);

    // Create sidebar and get content area (Sources is index 3)
    lv_obj_t* content = createSettingsSidebar(scr_sources, 3);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    addScreenHeader(content, "Sources", nullptr);

    // Scrollable list
    lv_obj_t* list = lv_obj_create(content);
    lv_obj_set_pos(list, 0, SY(50));
    lv_obj_set_size(list, lv_pct(100), SY(405));
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, SY(8), 0);

    // Populated by refreshSourcesList() on every screen open — see the note there
    // for why this is not filled in here at boot.
    sources_list = list;
    lv_obj_add_event_cb(scr_sources, refreshSourcesList, LV_EVENT_SCREEN_LOAD_START, NULL);
}

// ============================================================================
// Browse Screen
// ============================================================================
// Frees a browse row's heap-allocated ItemData when LVGL destroys the button — for ANY
// reason (screen rebuild, lv_obj_del of an ancestor, list refresh). Attached per-button
// where the ItemData is created.
//
// Replaces the old cleanupBrowseData(list) sweep, which was handed
// lv_obj_get_child(scr_browse, -1) — the *content container*, not the item list. The list
// is a grandchild, so the sweep only ever inspected {title, list} (neither carries
// user_data) and freed nothing: ~2.2KB per row leaked on every navigation.
// Deliberately NOT a recursive sweep: user_data elsewhere in this project stores plain
// integer indices cast to void* (device/group/queue rows), which must never be freed.
static void browseItemDeleteCb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    if (!btn) return;
    void* data = lv_obj_get_user_data(btn);
    if (data) {
        heap_caps_free(data);
        lv_obj_set_user_data(btn, NULL);
    }
}

// ── Browse paging ───────────────────────────────────────────────────────────
// Rows are fetched a page at a time instead of "the first 100 and nothing else".
// A 500-track queue used to show 100 items with no indication the rest existed.
//
// This is a memory decision as much as a display one: every row owns an ItemData
// of ~2.1KB in PSRAM, so a page size of 50 costs ~107KB and the user only pays
// for the pages they actually ask for.
#define BROWSE_PAGE_SIZE 50

static lv_obj_t* browse_list     = nullptr;   // list being appended to
static lv_obj_t* browse_more_btn = nullptr;   // the "Load more" row, if shown
static int       browse_offset   = 0;         // next StartingIndex to request

static int browsePopulate(lv_obj_t* list, int startIndex);

// Row icon from the DIDL upnp:class, so an album, a playlist and a radio station
// no longer all read as a generic folder. Falls back to folder/note for classes
// not listed, which keeps unknown content types rendering sensibly.
// NOTE: only icons present in lv_font_mdi_16 may be used here. MDI_WAVEFORM and
// MDI_TELEVISION are generated at 40/80 only — they are hero glyphs — and using
// one at this size renders a tofu box, not an icon. Genres used MDI_WAVEFORM and
// showed exactly that. A genre is just a container, so it takes the folder.
static const char* browseIconFor(const String& cls, bool isContainer) {
    if (cls.indexOf("musicAlbum") >= 0)        return MDI_MUSIC_BOX;
    if (cls.indexOf("playlistContainer") >= 0) return MDI_PLAYLIST;
    if (cls.indexOf("audioBroadcast") >= 0)    return MDI_RADIO;
    if (cls.indexOf("musicTrack") >= 0)        return MDI_MUSIC_NOTE;
    return isContainer ? MDI_FOLDER : MDI_MUSIC_NOTE;
}

// "Music Library > Artists" — the levels above the one being shown. Empty at the
// top, where the back arrow alone says everything. ASCII '>' deliberately: the
// nicer chevrons live outside Latin-Ext-A and would render as tofu.
static String browseTrailText(void) {
    String s;
    for (int i = 0; i < browse_depth; i++) {
        if (s.length()) s += " > ";
        s += browse_stack_title[i];
    }
    return s;
}

static void browseLoadMore(lv_event_t* e) {
    if (!browse_list) return;
    // Drop the button first so the new rows land at the end of the list, then
    // browsePopulate() re-adds it if the page came back full.
    if (browse_more_btn) { lv_obj_del(browse_more_btn); browse_more_btn = nullptr; }
    browse_offset += browsePopulate(browse_list, browse_offset);
}

void createBrowseScreen() {
    // Keep the outgoing screen alive until the new one is fully built. Deleting
    // it up-front left lv_display_t::act_scr NULL across browsePopulate()'s
    // Browse SOAP — a 2s timeout plus a 500ms retry, so several seconds with no
    // active screen. Build first, delete last; the caller loads immediately after.
    lv_obj_t* old_browse = scr_browse;

    scr_browse = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_browse, COL_SCREEN, 0);

    // Create sidebar and get content area (Sources is index 3)
    lv_obj_t* content = createSettingsSidebar(scr_browse, 3);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Back arrow — up one container, or out to Sources at the top level. Without
    // this the only way out of a nested container was the sidebar, which jumps
    // all the way back to the Sources root and loses your place entirely.
    lv_obj_t* btn_back = lv_btn_create(content);
    lv_obj_set_size(btn_back, SMIN(38), SMIN(38));
    lv_obj_set_pos(btn_back, 0, 0);
    lv_obj_set_style_radius(btn_back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_20, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_border_color(btn_back, COL_TEXT, 0);
    lv_obj_set_style_border_opa(btn_back, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_set_ext_click_area(btn_back, 8);
    lv_obj_add_event_cb(btn_back, [](lv_event_t* e) { browseBack(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_back = lv_label_create(btn_back);
    lv_label_set_text(ico_back, MDI_ARROW_LEFT);
    lv_obj_set_style_text_color(ico_back, COL_TEXT, 0);
    lv_obj_set_style_text_font(ico_back, &lv_font_mdi_24, 0);
    lv_obj_center(ico_back);

    // Breadcrumb — the levels above this one, so you can tell "Albums" inside
    // Music Library from "Albums" inside a service. Only drawn when nested.
    String trail = browseTrailText();
    bool nested = trail.length() > 0;
    if (nested) {
        lv_obj_t* lbl_trail = lv_label_create(content);
        lv_label_set_text(lbl_trail, trail.c_str());
        lv_obj_set_style_text_font(lbl_trail, &font_text_12, 0);
        lv_obj_set_style_text_color(lbl_trail, COL_TEXT2, 0);
        lv_obj_set_pos(lbl_trail, SX(48), SY(2));
        lv_obj_set_width(lbl_trail, SX(520));
        lv_label_set_long_mode(lbl_trail, LV_LABEL_LONG_DOT);
    }

    // Title — sits right of the back arrow, and ellipsises rather than running
    // under the sidebar: container names are user content and unbounded. Drops
    // below the breadcrumb when there is one, otherwise keeps the whole row.
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, current_browse_title.c_str());
    lv_obj_set_style_text_font(lbl_title, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, SX(48), nested ? SY(18) : SY(4));
    lv_obj_set_width(lbl_title, SX(520));
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_DOT);

    // Content list
    lv_obj_t* list = lv_obj_create(content);
    lv_obj_set_pos(list, 0, SY(50));
    lv_obj_set_size(list, lv_pct(100), SY(405));
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, SY(10), 0);

    browse_list     = list;
    browse_more_btn = nullptr;
    browse_offset   = browsePopulate(list, 0);

    // Now the replacement is populated, retire the old screen. Per-row ItemData
    // is released by browseItemDeleteCb as LVGL tears down the subtree — no
    // manual sweep needed (and the old one walked the wrong node).
    if (old_browse) lv_obj_del(old_browse);
}

// Appends one page of rows to `list`, starting at `startIndex`. Returns how many
// were added, so the caller can advance the offset.
static int browsePopulate(lv_obj_t* list, int startIndex) {
    String didl = sonos.browseContent(current_browse_id.c_str(), startIndex, BROWSE_PAGE_SIZE);

    Serial.printf("[BROWSE] ID=%s start=%d DIDL length=%d\n",
                  current_browse_id.c_str(), startIndex, didl.length());

    if (didl.length() == 0) {
        // Only say "empty" for the first page — a later page coming back empty
        // just means we reached the end, and the rows already shown are valid.
        if (startIndex == 0) {
            lv_obj_t* lbl_empty = lv_label_create(list);
            lv_label_set_text(lbl_empty, "No items found");
            lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
        }
        return 0;
    }

    int searchPos = 0;
    int itemCount = 0;

    while (searchPos < (int)didl.length()) {
        int containerPos = didl.indexOf("<container", searchPos);
        int itemPos = didl.indexOf("<item", searchPos);

        if (containerPos < 0 && itemPos < 0) break;

        bool isContainer = false;
        if (containerPos >= 0 && (itemPos < 0 || containerPos < itemPos)) {
            searchPos = containerPos;
            isContainer = true;
        } else if (itemPos >= 0) {
            searchPos = itemPos;
            isContainer = false;
        } else {
            break;
        }

        int endPos = isContainer ? didl.indexOf("</container>", searchPos) : didl.indexOf("</item>", searchPos);
        if (endPos < 0) break;

        String itemXML = didl.substring(searchPos, endPos + (isContainer ? 12 : 7));
        // See the note in refreshSourcesList(): DIDL is double-escaped, so a
        // title containing & < > or a quote needs a second decode after
        // browseContent()'s. Without it a genre reads "R&amp;B".
        String title = sonos.decodeHTML(sonos.extractXML(itemXML, "dc:title"));

        int idStart = itemXML.indexOf("id=\"") + 4;
        int idEnd = itemXML.indexOf("\"", idStart);
        String id = itemXML.substring(idStart, idEnd);

        Serial.printf("[BROWSE] Item #%d: %s (container=%d, id=%s)\n",
                      itemCount, title.c_str(), isContainer, id.c_str());

        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, lv_pct(100), SY(60));
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, SMIN(15), 0);

        struct ItemData {
            char id[128];
            char itemXML[2048];  // Increased for full DIDL-Lite metadata with r:resMD
            bool isContainer;
        };
        ItemData* data = (ItemData*)heap_caps_malloc(sizeof(ItemData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!data) {
            Serial.println("[BROWSE] PSRAM malloc failed, trying regular heap...");
            data = (ItemData*)malloc(sizeof(ItemData));
            if (!data) {
                Serial.println("[BROWSE] Regular malloc also failed!");
                break;
            }
        }
        strncpy(data->id, id.c_str(), sizeof(data->id) - 1);
        data->id[sizeof(data->id) - 1] = '\0';

        if (itemXML.length() >= sizeof(data->itemXML)) {
            itemXML = itemXML.substring(0, sizeof(data->itemXML) - 1);
        }
        strncpy(data->itemXML, itemXML.c_str(), sizeof(data->itemXML) - 1);
        data->itemXML[sizeof(data->itemXML) - 1] = '\0';
        data->isContainer = isContainer;
        lv_obj_set_user_data(btn, data);
        // LVGL frees this ItemData when the button is destroyed (see browseItemDeleteCb).
        lv_obj_add_event_cb(btn, browseItemDeleteCb, LV_EVENT_DELETE, NULL);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, browseIconFor(sonos.extractXML(itemXML, "upnp:class"), isContainer));
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_mdi_16, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, SX(5), 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, title.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &font_text_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, SX(40), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lbl, lv_pct(90));

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            struct ItemData {
                char id[128];
                char itemXML[2048];
                bool isContainer;
            };
            ItemData* data = (ItemData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            String itemXML = String(data->itemXML);
            String id = String(data->id);

            String uri = sonos.extractXML(itemXML, "res");
            uri = sonos.decodeHTML(uri);

            if (data->isContainer) {
                if (id.startsWith("SQ:") && id.indexOf("/") < 0) {
                    // Decoded: the title goes into the DIDL playPlaylist() builds,
                    // so a raw &amp; here would be re-escaped into &amp;amp;.
                    String title = sonos.decodeHTML(sonos.extractXML(itemXML, "dc:title"));
                    Serial.printf("[BROWSE] Playing playlist: %s (ID: %s)\n", title.c_str(), id.c_str());
                    sonos.playPlaylist(id.c_str(), title.c_str());
                    lv_screen_load(scr_main);
                } else {
                    browseDescend(id, sonos.decodeHTML(sonos.extractXML(itemXML, "dc:title")));
                }
            } else {

                if (uri.length() == 0) {
                    String resMD = sonos.extractXML(itemXML, "r:resMD");
                    if (resMD.length() > 0) {
                        resMD = sonos.decodeHTML(resMD);

                        if (resMD.indexOf("<upnp:class>object.container</upnp:class>") >= 0) {
                            int idStart = resMD.indexOf("id=\"") + 4;
                            int idEnd = resMD.indexOf("\"", idStart);
                            String containerID = resMD.substring(idStart, idEnd);
                            Serial.printf("[BROWSE] Shortcut to container: %s\n", containerID.c_str());
                            browseDescend(containerID,
                                          sonos.decodeHTML(sonos.extractXML(resMD, "dc:title")));
                            return;
                        }

                        uri = sonos.extractXML(resMD, "res");
                    }
                }

                if (uri.startsWith("x-rincon-cpcontainer:")) {
                    // Favorites, and service albums/playlists, are containers: the
                    // whole thing becomes the transport rather than one track being
                    // played.
                    //
                    // The metadata MUST be r:resMD, not the favourite item itself.
                    // A favourite looks like this:
                    //     <res>x-rincon-cpcontainer:1006...?sid=284</res>
                    //     <r:resMD>...<desc id="cdudn">SA_RINCON72711_..._Token</desc>...</r:resMD>
                    // and that cdudn token is what tells the player WHICH service
                    // account resolves the container. Hand Sonos the outer <item>
                    // instead and it cannot resolve anything, so the tap silently
                    // does nothing — which is exactly what was reported.
                    //
                    // resMD is passed still-escaped: playContainer() runs
                    // decodeHTMLEntities() on it before re-encoding for SOAP.
                    String meta = sonos.extractXML(itemXML, "r:resMD");
                    if (meta.length() == 0) meta = itemXML;   // non-favourite containers
                    Serial.printf("[BROWSE] Playing container: %s\n", uri.c_str());
                    sonos.playContainer(uri.c_str(), meta.c_str());
                    lv_screen_load(scr_main);
                } else if (uri.length() > 0) {
                    // A favourited RADIO STATION needs r:resMD exactly as a favourited
                    // playlist does. x-sonosapi-stream: is service content too, and the
                    // <desc id="cdudn">SA_RINCON..._Token</desc> inside resMD is what
                    // names the account that resolves it. Handing Sonos the outer
                    // <item> instead is accepted WITHOUT an error and then plays
                    // nothing at all — #125, still failing after the size limit was
                    // raised, because only the container branch had been fixed.
                    //
                    // Note the asymmetry, which is easy to get wrong: playContainer()
                    // runs decodeHTMLEntities() on what it is given, so it wants the
                    // escaped form; playURI() runs encodeXML(), so it wants the decoded
                    // one. Passing the same string to both would double-escape here.
                    String resMD = sonos.extractXML(itemXML, "r:resMD");
                    String meta  = resMD.length() ? sonos.decodeHTML(resMD) : itemXML;
                    Serial.printf("[BROWSE] Playing URI: %s%s\n", uri.c_str(),
                                  resMD.length() ? "  (with favourite metadata)" : "");
                    sonos.playURI(uri.c_str(), meta.c_str());
                    lv_screen_load(scr_main);
                } else {
                    Serial.println("[BROWSE] No URI found!");
                }
            }
        }, LV_EVENT_CLICKED, NULL);

        searchPos = endPos + (isContainer ? 12 : 7);
        itemCount++;
    }

    if (itemCount == 0 && startIndex == 0) {
        lv_obj_t* lbl_empty = lv_label_create(list);
        lv_label_set_text(lbl_empty, "No items found");
        lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
    }

    // A full page means there is probably more. browseContent() returns only the
    // DIDL, not TotalMatches, so this is the honest test available — worst case
    // is one "Load more" that turns out to fetch nothing, which then removes
    // itself because the next page comes back empty.
    if (itemCount == BROWSE_PAGE_SIZE) {
        browse_more_btn = lv_btn_create(list);
        lv_obj_set_size(browse_more_btn, lv_pct(100), SY(50));
        lv_obj_set_style_radius(browse_more_btn, 10, 0);
        lv_obj_set_style_shadow_width(browse_more_btn, 0, 0);
        lv_obj_set_style_bg_color(browse_more_btn, COL_BG, 0);
        lv_obj_set_style_bg_color(browse_more_btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(browse_more_btn, 1, 0);
        lv_obj_set_style_border_color(browse_more_btn, COL_BORDER, 0);
        lv_obj_add_event_cb(browse_more_btn, browseLoadMore, LV_EVENT_CLICKED, NULL);

        lv_obj_t* lbl_more = lv_label_create(browse_more_btn);
        lv_label_set_text_fmt(lbl_more, "Load more  (%d shown)", startIndex + itemCount);
        lv_obj_set_style_text_color(lbl_more, COL_ACCENT, 0);
        lv_obj_set_style_text_font(lbl_more, &font_text_16, 0);
        lv_obj_center(lbl_more);
    }

    Serial.printf("[BROWSE] +%d rows from %d, free heap: %d bytes\n",
                  itemCount, startIndex, esp_get_free_heap_size());
    return itemCount;
}
