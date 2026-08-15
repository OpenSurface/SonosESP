/**
 * UI Settings Screens
 * Remaining screens: Queue, Sources, Browse, Settings redirect
 * (Other settings screens have been extracted to separate files)
 */

#include "ui_common.h"
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
        lv_obj_set_style_bg_color(btn, isPlaying ? lv_color_hex(0x252525) : lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
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
    lv_obj_set_style_bg_color(scr_queue, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_queue);
    lv_obj_set_size(header, SX(800), SY(70));
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Playlist");
    lv_obj_set_style_text_font(lbl_title, &font_text_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, SX(30), 0);

    // Refresh button in header
    lv_obj_t* btn_refresh = lv_button_create(header);
    lv_obj_set_size(btn_refresh, SMIN(50), SMIN(50));
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, SX(-80), 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x333333), 0);
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
    lv_obj_set_style_text_color(ico_refresh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_refresh, &lv_font_mdi_24, 0);
    lv_obj_center(ico_refresh);

    // Close button in header
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, SMIN(50), SMIN(50));
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, SX(-20), 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, MDI_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
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
    lv_obj_set_style_bg_color(list_queue, lv_color_hex(0x1A1A1A), 0);
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
void createSourcesScreen() {
    scr_sources = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sources, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Sources is index 3)
    lv_obj_t* content = createSettingsSidebar(scr_sources, 3);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, "Sources");
    lv_obj_set_style_text_font(lbl_title, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

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

    // Music source items
    struct MusicSource {
        const char* name;
        const char* icon;
        const char* objectID;
    };

    MusicSource sources[] = {
        {"Sonos Playlists", MDI_PLAYLIST, "SQ:"}
    };

    for (int i = 0; i < 1; i++) {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, lv_pct(100), SY(50));
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, SMIN(15), 0);
        lv_obj_set_user_data(btn, (void*)sources[i].objectID);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, sources[i].icon);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_mdi_24, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, SX(5), 0);

        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, sources[i].name);
        lv_obj_set_style_text_color(name, COL_TEXT, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, SX(40), 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* btn_target = (lv_obj_t*)lv_event_get_target(e);
            const char* objID = (const char*)lv_obj_get_user_data(btn_target);
            lv_obj_t* label = lv_obj_get_child(btn_target, 1);
            const char* title = lv_label_get_text(label);

            current_browse_id = String(objID);
            current_browse_title = String(title);

            createBrowseScreen();
            lv_screen_load(scr_browse);
        }, LV_EVENT_CLICKED, NULL);
    }
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

void createBrowseScreen() {
    if (scr_browse) {
        // Per-row ItemData is released by browseItemDeleteCb as LVGL tears down the
        // subtree — no manual sweep needed (and the old one walked the wrong node).
        lv_obj_del(scr_browse);
    }

    scr_browse = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_browse, lv_color_hex(0x121212), 0);

    // Create sidebar and get content area (Sources is index 3)
    lv_obj_t* content = createSettingsSidebar(scr_browse, 3);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* lbl_title = lv_label_create(content);
    lv_label_set_text(lbl_title, current_browse_title.c_str());
    lv_obj_set_style_text_font(lbl_title, &font_text_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

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

    String didl = sonos.browseContent(current_browse_id.c_str());

    Serial.printf("[BROWSE] ID=%s, DIDL length=%d\n", current_browse_id.c_str(), didl.length());

    if (didl.length() == 0) {
        lv_obj_t* lbl_empty = lv_label_create(list);
        lv_label_set_text(lbl_empty, "No items found");
        lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
        return;
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
        String title = sonos.extractXML(itemXML, "dc:title");

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
        lv_label_set_text(icon, isContainer ? MDI_FOLDER : MDI_SPEAKER);
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
                    String title = sonos.extractXML(itemXML, "dc:title");
                    Serial.printf("[BROWSE] Playing playlist: %s (ID: %s)\n", title.c_str(), id.c_str());
                    sonos.playPlaylist(id.c_str(), title.c_str());
                    lv_screen_load(scr_main);
                } else {
                    current_browse_id = id;
                    current_browse_title = sonos.extractXML(itemXML, "dc:title");
                    createBrowseScreen();
                    lv_screen_load(scr_browse);
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
                            current_browse_id = containerID;
                            current_browse_title = sonos.extractXML(resMD, "dc:title");
                            Serial.printf("[BROWSE] Shortcut to container: %s\n", containerID.c_str());
                            createBrowseScreen();
                            lv_screen_load(scr_browse);
                            return;
                        }

                        uri = sonos.extractXML(resMD, "res");
                    }
                }

                if (uri.startsWith("x-rincon-cpcontainer:")) {
                    // Sonos Favorites (x-rincon-cpcontainer) not supported.
                    Serial.println("[BROWSE] Sonos Favorites not supported");
                } else if (uri.length() > 0) {
                    Serial.printf("[BROWSE] Playing URI: %s\n", uri.c_str());
                    sonos.playURI(uri.c_str(), itemXML.c_str());
                    lv_screen_load(scr_main);
                } else {
                    Serial.println("[BROWSE] No URI found!");
                }
            }
        }, LV_EVENT_CLICKED, NULL);

        searchPos = endPos + (isContainer ? 12 : 7);
        itemCount++;
        if (itemCount >= 100) {
            Serial.printf("[BROWSE] Reached 100 item limit, stopping\n");
            break;
        }
    }

    if (itemCount == 0) {
        lv_obj_t* lbl_empty = lv_label_create(list);
        lv_label_set_text(lbl_empty, "No items found");
        lv_obj_set_style_text_color(lbl_empty, COL_TEXT2, 0);
    }

    Serial.printf("[BROWSE] Created %d items, free heap: %d bytes\n", itemCount, esp_get_free_heap_size());
}
