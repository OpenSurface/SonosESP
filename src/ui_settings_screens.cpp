/**
 * UI Settings Screens
 * Remaining screens: Queue, Sources, Browse, Settings redirect
 * (Other settings screens have been extracted to separate files)
 */

#include "ui_common.h"

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
    lv_label_set_text_fmt(lbl_queue_status, "%d %s in queue", d->queueSize, d->queueSize == 1 ? "track" : "tracks");

    for (int i = 0; i < d->queueSize; i++) {
        QueueItem* item = &d->queue[i];
        int trackNum = i + 1;
        bool isPlaying = (trackNum == d->currentTrackNumber);

        lv_obj_t* btn = lv_btn_create(list_queue);
        lv_obj_set_size(btn, 727, 60);  // Full width, uniform height
        lv_obj_set_style_bg_color(btn, isPlaying ? lv_color_hex(0x252525) : lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 0, 0);  // No rounded corners - clean list
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 12, 0);
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
            lv_label_set_text(num, LV_SYMBOL_PLAY);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_18, 0);
        } else {
            lv_label_set_text_fmt(num, "%d", trackNum);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_14, 0);
        }
        lv_obj_set_style_text_color(num, isPlaying ? COL_ACCENT : COL_TEXT2, 0);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 5, 0);

        // Title - highlight when playing
        lv_obj_t* title = lv_label_create(btn);
        lv_label_set_text(title, item->title.c_str());
        lv_obj_set_style_text_color(title, isPlaying ? COL_ACCENT : COL_TEXT, 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_width(title, 610);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 45, -11);

        // Artist - subtle gray
        lv_obj_t* artist = lv_label_create(btn);
        lv_label_set_text(artist, item->artist.c_str());
        lv_obj_set_style_text_color(artist, COL_TEXT2, 0);
        lv_obj_set_style_text_font(artist, &lv_font_montserrat_12, 0);
        lv_obj_set_width(artist, 610);
        lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
        lv_obj_align(artist, LV_ALIGN_LEFT_MID, 45, 11);
    }
}

void createQueueScreen() {
    scr_queue = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_queue, lv_color_hex(0x1A1A1A), 0);

    // Professional header
    lv_obj_t* header = lv_obj_create(scr_queue);
    lv_obj_set_size(header, 800, 70);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title in header
    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Playlist");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 30, 0);

    // Refresh button in header
    lv_obj_t* btn_refresh = lv_button_create(header);
    lv_obj_set_size(btn_refresh, 50, 50);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, -80, 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_refresh, 25, 0);
    lv_obj_set_style_shadow_width(btn_refresh, 0, 0);
    lv_obj_add_event_cb(btn_refresh, [](lv_event_t* e) { sonos.updateQueue(); refreshQueueList(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(ico_refresh, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(ico_refresh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_refresh, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_refresh);

    // Close button in header
    lv_obj_t* btn_close = lv_button_create(header);
    lv_obj_set_size(btn_close, 50, 50);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn_close, 25, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, ev_back_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ico_close = lv_label_create(btn_close);
    lv_label_set_text(ico_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ico_close, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ico_close, &lv_font_montserrat_24, 0);
    lv_obj_center(ico_close);

    // Status label below header
    lbl_queue_status = lv_label_create(scr_queue);
    lv_obj_align(lbl_queue_status, LV_ALIGN_TOP_LEFT, 40, 85);
    lv_label_set_text(lbl_queue_status, "Loading...");
    lv_obj_set_style_text_color(lbl_queue_status, COL_TEXT2, 0);
    lv_obj_set_style_text_font(lbl_queue_status, &lv_font_montserrat_14, 0);

    // Queue list - modern clean design
    list_queue = lv_list_create(scr_queue);
    lv_obj_set_size(list_queue, 730, 360);
    lv_obj_set_pos(list_queue, 35, 115);
    lv_obj_set_style_bg_color(list_queue, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_queue, 0, 0);
    lv_obj_set_style_radius(list_queue, 0, 0);
    lv_obj_set_style_pad_all(list_queue, 0, 0);
    lv_obj_set_style_pad_row(list_queue, 0, 0);  // No spacing between items

    // Modern thin scrollbar on the right edge
    lv_obj_set_style_pad_right(list_queue, 3, LV_PART_SCROLLBAR);
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
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

    // Scrollable list
    lv_obj_t* list = lv_obj_create(content);
    lv_obj_set_pos(list, 0, 50);
    lv_obj_set_size(list, lv_pct(100), 405);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 8, 0);

    // Music source items
    struct MusicSource {
        const char* name;
        const char* icon;
        const char* objectID;
    };

    MusicSource sources[] = {
        {"Sonos Favorites", LV_SYMBOL_DIRECTORY, "FV:2"},
        {"Sonos Playlists", LV_SYMBOL_LIST, "SQ:"}
    };

    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, lv_pct(100), 50);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 15, 0);
        lv_obj_set_user_data(btn, (void*)sources[i].objectID);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, sources[i].icon);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, sources[i].name);
        lv_obj_set_style_text_color(name, COL_TEXT, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 0);

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
void cleanupBrowseData(lv_obj_t* list) {
    if (!list) return;
    uint32_t child_count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(list, i);
        if (child) {
            void* data = lv_obj_get_user_data(child);
            if (data) {
                heap_caps_free(data);
                lv_obj_set_user_data(child, NULL);
            }
        }
    }
}

void createBrowseScreen() {
    if (scr_browse) {
        lv_obj_t* list = lv_obj_get_child(scr_browse, -1);
        cleanupBrowseData(list);
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
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_pos(lbl_title, 0, 0);

    // Content list
    lv_obj_t* list = lv_obj_create(content);
    lv_obj_set_pos(list, 0, 50);
    lv_obj_set_size(list, lv_pct(100), 405);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 10, 0);

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
        lv_obj_set_size(btn, lv_pct(100), 60);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, COL_CARD, 0);
        lv_obj_set_style_bg_color(btn, COL_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 15, 0);

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

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, isContainer ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, title.c_str());
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 40, 0);
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
                    sonos.playPlaylist(id.c_str());
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
                    String title = sonos.extractXML(itemXML, "dc:title");
                    Serial.printf("[BROWSE] Playing container: %s\n", title.c_str());

                    // Extract inner DIDL-Lite from r:resMD tag
                    String resMD = sonos.extractXML(itemXML, "r:resMD");
                    if (resMD.length() > 0) {
                        resMD = sonos.decodeHTML(resMD);

                        // Extract <res> tag from outer item and inject into inner DIDL
                        String resTag = sonos.extractXML(itemXML, "res");
                        String protocolInfo = "";
                        int protoStart = itemXML.indexOf("protocolInfo=\"");
                        if (protoStart > 0) {
                            int protoEnd = itemXML.indexOf("\"", protoStart + 14);
                            if (protoEnd > protoStart) {
                                protocolInfo = itemXML.substring(protoStart + 14, protoEnd);
                            }
                        }

                        // Build complete <res> element
                        String resElement = "<res protocolInfo=\"" + protocolInfo + "\">" + uri + "</res>";

                        // Insert <res> into the inner DIDL's <item> (after <upnp:class>)
                        int insertPos = resMD.indexOf("</upnp:class>") + 13;
                        if (insertPos > 13) {
                            resMD = resMD.substring(0, insertPos) + resElement + resMD.substring(insertPos);
                        }

                        Serial.printf("[BROWSE] Enhanced inner DIDL with <res> tag (%d bytes)\n", resMD.length());
                        sonos.playContainer(uri.c_str(), resMD.c_str());
                    } else {
                        Serial.println("[BROWSE] No r:resMD found, using full itemXML");
                        sonos.playContainer(uri.c_str(), itemXML.c_str());
                    }
                    lv_screen_load(scr_main);
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
        if (itemCount >= 20) {
            Serial.printf("[BROWSE] Reached 20 item limit, stopping\n");
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
