#pragma once
/**
 * Streaming-service tiles on the Sources screen and their browse pages.
 *
 * Service containers live in the same Browse screen and navigation trail as the
 * local ContentDirectory ones; their ids are just tagged so createBrowseScreen()
 * knows to fetch them through music_services instead of a local Browse SOAP:
 *
 *     svc|<sid>|<serial>|<P or ->|<itemType>|<provider object id>
 *
 * (P = the container itself can be played, which adds a "Play all" row.)
 */

#include <Arduino.h>
#include "lvgl.h"

// Implemented in ui_settings_screens.cpp, shared with this file.
void browseEnterRoot(const String& id, const String& title);
void browseDescend(const String& id, const String& title);
lv_obj_t* sourceTileCreate(lv_obj_t* list, const char* icon, const char* label);

// True for a browse id produced by this module.
bool svcIsBrowseId(const String& id);

// Appends the linked-service tiles to the Sources grid (or a loading / retry
// placeholder) and returns how many tiles were added.
int svcAppendSourceTiles(lv_obj_t* list);

// Fills `list` with the service container named by current_browse_id. Rows
// arrive asynchronously; a "Loading..." row stands in until then.
void svcBrowseBegin(lv_obj_t* list);

// Drops a page still loading. Call before the Browse screen is rebuilt.
void svcBrowseCancel();
