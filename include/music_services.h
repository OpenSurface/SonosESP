#pragma once
/**
 * Streaming-service browsing (Spotify, Apple Music, Amazon Music, TuneIn, ...)
 * through SMAPI, with the accounts the Sonos household already holds.
 *
 * How it works — no sign-in on the panel:
 *
 *   1. Accounts. The speaker publishes the household's service accounts,
 *      encrypted, in the ThirdPartyMediaServersX variable of its
 *      ZoneGroupTopology UPnP event. We subscribe once, take the initial
 *      NOTIFY, unsubscribe, and decrypt it (AES-128-CBC under a key derived
 *      from the household id). That yields each account's token/key.
 *   2. Services. ListAvailableServices gives each service's SMAPI endpoint,
 *      auth type and capability bits.
 *   3. Browsing. getMetadata is a SOAP call straight to the provider's SMAPI
 *      endpoint over HTTPS, with the account credentials in the header.
 *      Expired tokens are refreshed in memory (never written to the speaker).
 *   4. Playing. A browsed item becomes a URI + DIDL naming the account (cdudn),
 *      handed to the speaker exactly like a favourite — see smapi.h.
 *
 * The protocol is the desktop-controller flow reverse-engineered in SoCo
 * PR #1010; it is not a published Sonos API and can change with Sonos firmware.
 *
 * Threading. All network work runs on one worker task (PSRAM stack), never on
 * the LVGL thread: a TLS handshake plus the 3s HTTPS cooldown would otherwise
 * freeze the UI for seconds. The UI submits a request, gets an id back, and
 * polls takePage(id) from an lv_timer. Only the newest browse request is kept;
 * an older one still queued is simply dropped.
 */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// One browsed row. Fixed-size so a page is a single PSRAM block and the UI can
// keep a copy per row without touching the scarce internal heap.
struct SvcItem {
    char id[384];
    char title[128];
    char subtitle[96];
    char itemType[32];
    char mimeType[32];
    bool collection;     // container (album, playlist, folder) vs playable leaf
    bool canPlay;
    bool canEnumerate;
};

struct SvcPage {
    uint32_t reqId;
    bool     ok;
    char     error[112];   // human-readable, shown in the list when !ok
    int      index;        // first item's position in the container
    int      total;        // provider's total, for "Load more"
    int      count;        // items[] length
    SvcItem* items;        // PSRAM, count entries
};

// One linked account, as the Sources screen shows it.
struct SvcSource {
    int  serviceId;
    int  serial;
    bool anonymous;        // radio-style services with no account
    char label[64];        // "Spotify", or "Spotify (Ben)" when there are two
};

enum SvcState { SVC_IDLE, SVC_LOADING, SVC_READY, SVC_FAILED };

class MusicServices {
public:
    // ── UI thread ────────────────────────────────────────────────────────────
    // Start (or with force, redo) the account discovery. No-op while loading.
    void requestLoad(bool force = false);
    SvcState state() const { return state_; }
    // Why the last load failed. Valid while state() == SVC_FAILED.
    String lastError();
    // Copies up to `max` linked sources into out[]; returns how many.
    int sources(SvcSource* out, int max);

    // Browse one page of a service container ("root" for the top). Returns the
    // request id to poll with takePage().
    uint32_t requestBrowse(int serviceId, int serial, const char* objectId, int index, int count);
    // The finished page for reqId, or nullptr while it is still running. The
    // caller owns the page and must release it with freePage().
    SvcPage* takePage(uint32_t reqId);
    static void freePage(SvcPage* page);

    // URI + DIDL for playing `item` through its account. `container` true means
    // enqueue it (playContainer), false means play it directly (playURI).
    bool buildPlayback(int serviceId, int serial, const SvcItem& item,
                       String* uri, String* didl, bool* container);

private:
    void ensureTask();
    static void taskEntry(void* arg);
    void taskLoop();
    void doLoad();
    SvcPage* doBrowse(uint32_t reqId, int sid, int serial, const String& objectId,
                      int index, int count);
    void fail(const String& why);

    volatile SvcState state_ = SVC_IDLE;
    SemaphoreHandle_t lock_  = nullptr;
    TaskHandle_t      task_  = nullptr;
    StaticTask_t      taskTCB_;
    StackType_t*      taskStack_ = nullptr;
};

extern MusicServices musicServices;
