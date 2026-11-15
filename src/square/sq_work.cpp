// sq_work.cpp — the square UI's network worker (see sq_work.h).
#include "sq_work.h"
#include "ui_common.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

volatile uint32_t sq_favs_version  = 0;
volatile uint32_t sq_rooms_version = 0;
volatile uint32_t sq_svc_version   = 0;
volatile bool     sq_sonos_ready   = false;
char              sq_current_fav_uri[256] = "";

static QueueHandle_t     s_jobs   = nullptr;
static volatile bool     s_busy   = false;
static SemaphoreHandle_t s_lock   = nullptr;   // favourites store + toast
static bool              s_fs_ok  = false;

#define FAVS_FILE   "/favs.xml"
#define SVC_FILE    "/services.json"
#define MAX_FAVS    60

// ── Toasts ──────────────────────────────────────────────────────────────────
static char s_toast[96] = "";
static bool s_toast_new = false;

static void toast(const char* fmt, ...) {
    char buf[96];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_toast, buf, sizeof(s_toast));
    s_toast_new = true;
    xSemaphoreGive(s_lock);
}

bool sqWorkTakeToast(char* out, size_t n) {
    if (!s_lock) return false;
    bool got = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_toast_new) { strlcpy(out, s_toast, n); s_toast_new = false; got = true; }
    xSemaphoreGive(s_lock);
    return got;
}

// ── Flash ───────────────────────────────────────────────────────────────────
static bool fsWrite(const char* path, const String& data) {
    if (!s_fs_ok) return false;
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t n = f.print(data);
    f.close();
    return n == data.length();
}

static String fsRead(const char* path) {
    if (!s_fs_ok || !LittleFS.exists(path)) return "";
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    String s = f.readString();
    f.close();
    return s;
}

// ── Favourites store ────────────────────────────────────────────────────────
// Rows for the UI, plus each favourite's whole <item> (PSRAM) for playing it.
static SqFav* s_favs      = nullptr;   // PSRAM, MAX_FAVS
static char*  s_fav_xml[MAX_FAVS] = {};
static int    s_fav_count = 0;

static const char* favKind(const String& resMD, const String& uri) {
    if (resMD.indexOf("playlistContainer") >= 0) return "Playlist";
    if (resMD.indexOf("musicAlbum") >= 0 || resMD.indexOf("object.container.album") >= 0) return "Album";
    if (resMD.indexOf("audioBroadcast") >= 0 || uri.startsWith("x-sonosapi-stream:") ||
        uri.startsWith("x-rincon-mp3radio:") || uri.startsWith("aac:") || uri.startsWith("x-sonosapi-radio:"))
        return "Radio";
    if (resMD.indexOf("musicTrack") >= 0) return "Titre";
    if (resMD.indexOf("musicArtist") >= 0 || resMD.indexOf("person") >= 0) return "Artiste";
    if (uri.startsWith("x-rincon-cpcontainer:")) return "Liste";
    return "Favori";
}

// Parses a decoded FV:2 browse result into the store. Returns the count.
static int favsParse(const String& didl) {
    SqFav* rows = (SqFav*)heap_caps_calloc(MAX_FAVS, sizeof(SqFav), MALLOC_CAP_SPIRAM);
    char*  xml[MAX_FAVS] = {};
    if (!rows) return -1;
    int n = 0, pos = 0;
    while (n < MAX_FAVS && (pos = didl.indexOf("<item", pos)) >= 0) {
        int end = didl.indexOf("</item>", pos);
        if (end < 0) break;
        end += 7;
        String item = didl.substring(pos, end);
        pos = end;

        int idS = item.indexOf("id=\"");
        if (idS < 0) continue;
        idS += 4;
        int idE = item.indexOf('"', idS);
        String id = item.substring(idS, idE);
        String uri   = decodeHTMLEntities(sonos.extractXML(item, "res"));
        String resMD = decodeHTMLEntities(sonos.extractXML(item, "r:resMD"));
        SqFav& f = rows[n];
        strlcpy(f.id, id.c_str(), sizeof(f.id));
        strlcpy(f.title, sonos.decodeHTML(sonos.extractXML(item, "dc:title")).c_str(), sizeof(f.title));
        strlcpy(f.kind, favKind(resMD, uri), sizeof(f.kind));
        strlcpy(f.art, decodeHTMLEntities(sonos.extractXML(item, "upnp:albumArtURI")).c_str(), sizeof(f.art));
        strlcpy(f.uri, uri.c_str(), sizeof(f.uri));
        xml[n] = (char*)heap_caps_malloc(item.length() + 1, MALLOC_CAP_SPIRAM);
        if (xml[n]) memcpy(xml[n], item.c_str(), item.length() + 1);
        n++;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    SqFav* old = s_favs;
    char*  old_xml[MAX_FAVS];
    memcpy(old_xml, s_fav_xml, sizeof(old_xml));
    s_favs = rows;
    memcpy(s_fav_xml, xml, sizeof(s_fav_xml));
    s_fav_count = n;
    xSemaphoreGive(s_lock);
    if (old) heap_caps_free(old);
    for (char* p : old_xml) if (p) heap_caps_free(p);
    sq_favs_version++;
    return n;
}

int sqFavsCount(void) { return s_fav_count; }

bool sqFavsGet(int i, SqFav* out) {
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_favs && i >= 0 && i < s_fav_count) { *out = s_favs[i]; ok = true; }
    xSemaphoreGive(s_lock);
    return ok;
}

// Two URIs for the same thing differ in their query (sid, flags, sn): the
// speaker reports a station as ...?sid=254&flags=8224 while the favourite says
// ...?sid=333&flags=82. Compare what comes before the '?'.
static size_t uriKeyLen(const char* u) {
    const char* q = strchr(u, '?');
    return q ? (size_t)(q - u) : strlen(u);
}

int sqFavsFind(const char* uri) {
    if (!uri || !*uri || !s_lock) return -1;
    int found = -1;
    size_t n = uriKeyLen(uri);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; s_favs && i < s_fav_count; ++i)
        if (uriKeyLen(s_favs[i].uri) == n && !strncmp(s_favs[i].uri, uri, n)) { found = i; break; }
    xSemaphoreGive(s_lock);
    return found;
}

static void favsLoadFromSpeaker(void) {
    String didl = sonos.browseContent("FV:2", 0, MAX_FAVS);
    if (!didl.length() || didl.indexOf("<DIDL-Lite") < 0) {
        Serial.println("[SQ/FAV] FV:2 browse failed - keeping the flash copy");
        return;
    }
    int n = favsParse(didl);
    Serial.printf("[SQ/FAV] %d favourite(s) from the speaker\n", n);
    fsWrite(FAVS_FILE, didl);
}

static void favPlay(int index) {
    String item;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (index >= 0 && index < s_fav_count && s_fav_xml[index]) item = s_fav_xml[index];
    xSemaphoreGive(s_lock);
    if (!item.length()) return;

    // Same rules as the P4 Sources browser (ui_settings_screens.cpp): the
    // favourite's r:resMD names the service account, so it is what must be
    // handed to the player. playContainer() wants it still escaped, playURI()
    // wants it decoded.
    String uri   = decodeHTMLEntities(sonos.extractXML(item, "res"));
    String resMD = sonos.extractXML(item, "r:resMD");
    bool ok;
    if (uri.startsWith("x-rincon-cpcontainer:")) {
        ok = sonos.playContainer(uri.c_str(), resMD.length() ? resMD.c_str() : item.c_str());
    } else {
        ok = sonos.playURI(uri.c_str(), resMD.length() ? decodeHTMLEntities(resMD).c_str() : item.c_str());
    }
    if (!ok) toast("Impossible de lancer ce favori");
}

// ── Streaming services ──────────────────────────────────────────────────────
// A household can link several Spotify accounts ("Spotify (famille)", ...).
// The UI uses one, chosen in Réglages and kept in NVS; by default the first.
static int s_spotify_sn = -2;   // -2: not read from NVS yet, -1: no preference

int sqSpotifyAccounts(SvcSource* out, int max) {
    SvcSource src[12];
    int n = musicServices.sources(src, 12), k = 0;
    for (int i = 0; i < n && k < max; ++i)
        if (!strncasecmp(src[i].label, "Spotify", 7)) out[k++] = src[i];
    return k;
}

void sqSpotifyChoose(int serial) {
    s_spotify_sn = serial;
    Preferences p;
    p.begin("sq_ui", false);
    p.putInt("spotify_sn", serial);
    p.end();
    sq_svc_version++;
}

bool sqSpotify(int* sid, int* serial) {
    if (s_spotify_sn == -2) {
        Preferences p;
        p.begin("sq_ui", true);
        s_spotify_sn = p.getInt("spotify_sn", -1);
        p.end();
    }
    SvcSource acc[6];
    int n = sqSpotifyAccounts(acc, 6);
    if (n == 0) return false;
    int pick = 0;
    for (int i = 0; i < n; ++i) if (acc[i].serial == s_spotify_sn) pick = i;
    *sid = acc[pick].serviceId;
    *serial = acc[pick].serial;
    return true;
}

String sqItemUri(const SvcItem& item) {
    int sid, serial;
    if (!sqSpotify(&sid, &serial)) return "";
    String uri, didl;
    bool container = false;
    if (!musicServices.buildPlayback(sid, serial, item, &uri, &didl, &container)) return "";
    return uri;
}

static void svcLoad(bool force) {
    if (!force) {
        String cached = fsRead(SVC_FILE);
        if (cached.length() && musicServices.importCache(cached)) { sq_svc_version++; return; }
    }
    musicServices.requestLoad(force);
    uint32_t t0 = millis();
    while (musicServices.state() == SVC_LOADING && millis() - t0 < 60000) vTaskDelay(pdMS_TO_TICKS(200));
    if (musicServices.state() == SVC_READY) {
        String json = musicServices.exportCache();
        if (json.length() && fsWrite(SVC_FILE, json))
            Serial.println("[SQ/SVC] Accounts saved to flash - next boots skip the discovery");
        if (force) toast("Services mis à jour");
    } else {
        Serial.printf("[SQ/SVC] Account discovery failed: %s\n", musicServices.lastError().c_str());
        if (force) toast("Services : %s", musicServices.lastError().c_str());
    }
    sq_svc_version++;
}

static void svcPlay(const SvcItem& item) {
    int sid, serial;
    if (!sqSpotify(&sid, &serial)) { toast("Spotify n'est pas relié"); return; }
    String uri, didl;
    bool container = false;
    if (!musicServices.buildPlayback(sid, serial, item, &uri, &didl, &container)) {
        toast("Ce titre ne peut pas être lu");
        return;
    }
    bool ok;
    if (container) {
        // playContainer() decodes once before re-encoding: hand it escaped DIDL.
        String esc = didl;
        esc.replace("&", "&amp;"); esc.replace("<", "&lt;"); esc.replace(">", "&gt;"); esc.replace("\"", "&quot;");
        ok = sonos.playContainer(uri.c_str(), esc.c_str());
    } else {
        ok = sonos.playURI(uri.c_str(), didl.c_str());
    }
    if (!ok) toast("La lecture n'a pas démarré");
}

static void favAddItem(const SvcItem& item) {
    int sid, serial;
    if (!sqSpotify(&sid, &serial)) return;
    String uri, didl;
    bool container = false;
    if (!musicServices.buildPlayback(sid, serial, item, &uri, &didl, &container)) {
        toast("Impossible d'ajouter ce favori");
        return;
    }
    if (sonos.addFavorite(item.title, uri.c_str(), didl.c_str(), item.art, "Spotify")) {
        toast("Ajouté aux favoris");
        favsLoadFromSpeaker();
    } else {
        toast("Sonos a refusé le favori");
    }
}

static void favAddCurrent(void) {
    String uri, meta, title;
    if (!sonos.currentFavoriteCandidate(&uri, &meta, &title)) { toast("Rien à ajouter"); return; }
    strlcpy(sq_current_fav_uri, uri.c_str(), sizeof(sq_current_fav_uri));
    if (sqFavsFind(uri.c_str()) >= 0) { toast("Déjà dans les favoris"); return; }
    String art = decodeHTMLEntities(sonos.extractXML(meta, "upnp:albumArtURI"));
    if (sonos.addFavorite(title.length() ? title.c_str() : "Favori", uri.c_str(), meta.c_str(),
                          art.c_str(), nullptr)) {
        toast("Ajouté aux favoris");
        favsLoadFromSpeaker();
    } else {
        toast("Sonos a refusé le favori");
    }
}

// ── Rooms ───────────────────────────────────────────────────────────────────
static int deviceIndexOf(SonosDevice* d) {
    for (int i = 0; d && i < sonos.getDeviceCount(); ++i)
        if (sonos.getDevice(i) == d) return i;
    return -1;
}

static void startSonos(void) {
    bool ok = sonos.tryLoadCachedDevice();
    if (ok) {
        sonos.selectDevice(0);
        sonos.startTasks();
        sq_sonos_ready = true;
        sq_rooms_version++;
    }
    // Always run a discovery: the cache only holds the selected speaker, and the
    // Enceintes screen needs them all.
    int n = sonos.discoverDevices();
    Serial.printf("[SQ] Discovery: %d speaker(s)\n", n);
    if (!ok && n > 0) {
        int pick = 0;
        for (int i = 0; i < n; ++i) {
            SonosDevice* d = sonos.getDevice(i);
            if (d && d->isGroupCoordinator) { pick = i; break; }
        }
        sonos.selectDevice(pick);
        sonos.startTasks();
        sq_sonos_ready = true;
    }
    if (n > 0) sonos.updateGroupInfo();
    sq_rooms_version++;
    if (!sq_sonos_ready) toast("Aucune enceinte trouvée");
}

static void selectRoom(int index) {
    SonosDevice* d = sonos.getDevice(index);
    if (!d) return;
    // Control the group, not a member: transport commands go to the coordinator.
    SonosDevice* coord = sonos.groupCoordinatorFor(d);
    int ci = deviceIndexOf(coord);
    sonos.selectDevice(ci >= 0 ? ci : index);
    sonos.updateGroupInfo();
    sq_rooms_version++;
}

static void toggleGroup(int index) {
    SonosDevice* cur = sonos.getCurrentDevice();
    SonosDevice* d   = sonos.getDevice(index);
    if (!cur || !d || d == cur) return;
    int coord = deviceIndexOf(sonos.groupCoordinatorFor(cur));
    if (coord < 0) coord = deviceIndexOf(cur);
    if (sonos.isDeviceInGroup(index, coord)) sonos.leaveGroup(index);
    else                                      sonos.joinGroupCascade(index, coord);
    sonos.updateGroupInfo();
    sq_rooms_version++;
}

static void groupAll(void) {
    SonosDevice* cur = sonos.getCurrentDevice();
    if (!cur) return;
    int coord = deviceIndexOf(sonos.groupCoordinatorFor(cur));
    if (coord < 0) coord = deviceIndexOf(cur);
    for (int i = 0; i < sonos.getDeviceCount(); ++i) {
        if (i == coord || sonos.isDeviceInGroup(i, coord)) continue;
        SonosDevice* d = sonos.getDevice(i);
        if (!d || !d->connected) continue;
        sonos.joinGroupCascade(i, coord);
    }
    sonos.updateGroupInfo();
    sq_rooms_version++;
    toast("Toute la maison !");
}

// ── Worker ──────────────────────────────────────────────────────────────────
static void workerTask(void*) {
    SqJob job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        s_busy = true;
        switch (job.type) {
            case SQ_JOB_START_SONOS:    startSonos(); break;
            case SQ_JOB_DISCOVER:       sonos.discoverDevices(); sonos.updateGroupInfo();
                                        sq_rooms_version++; toast("Enceintes mises à jour"); break;
            case SQ_JOB_SVC_LOAD:       svcLoad(false); break;
            case SQ_JOB_SVC_REFRESH:    svcLoad(true); break;
            case SQ_JOB_FAVS_LOAD:      favsLoadFromSpeaker(); break;
            case SQ_JOB_PLAY_FAV:       favPlay(job.a); break;
            case SQ_JOB_PLAY_ITEM:      if (job.item) svcPlay(*job.item); break;
            case SQ_JOB_FAV_ADD_ITEM:   if (job.item) favAddItem(*job.item); break;
            case SQ_JOB_FAV_ADD_CURRENT: favAddCurrent(); break;
            case SQ_JOB_FAV_REMOVE:
                if (sonos.removeFavorite(job.text)) { toast("Retiré des favoris"); favsLoadFromSpeaker(); }
                else toast("Impossible de retirer ce favori");
                break;
            case SQ_JOB_SELECT_ROOM:    selectRoom(job.a); break;
            case SQ_JOB_TOGGLE_GROUP:   toggleGroup(job.a); break;
            case SQ_JOB_GROUP_ALL:      groupAll(); break;
        }
        if (job.item) heap_caps_free(job.item);
        s_busy = false;
    }
}

bool sqWorkPost(SqJobType type, int a, const char* text, const SvcItem* item) {
    if (!s_jobs) return false;
    SqJob job = {};
    job.type = type;
    job.a = a;
    if (text) strlcpy(job.text, text, sizeof(job.text));
    if (item) {
        job.item = (SvcItem*)heap_caps_malloc(sizeof(SvcItem), MALLOC_CAP_SPIRAM);
        if (!job.item) return false;
        *job.item = *item;
    }
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        if (job.item) heap_caps_free(job.item);
        return false;
    }
    return true;
}

bool sqWorkBusy(void) { return s_busy || (s_jobs && uxQueueMessagesWaiting(s_jobs) > 0); }

void sqWorkStart(void) {
    s_lock = xSemaphoreCreateMutex();
    s_jobs = xQueueCreate(16, sizeof(SqJob));
    s_fs_ok = LittleFS.begin(true, "/littlefs", 10, "spiffs");
    Serial.printf("[SQ] LittleFS %s (%u KB used / %u KB)\n", s_fs_ok ? "mounted" : "FAILED",
                  s_fs_ok ? (unsigned)(LittleFS.usedBytes() / 1024) : 0,
                  s_fs_ok ? (unsigned)(LittleFS.totalBytes() / 1024) : 0);
    // The flash copy of the favourites shows at once; the speaker refreshes it.
    String favs = fsRead(FAVS_FILE);
    if (favs.length()) Serial.printf("[SQ/FAV] %d favourite(s) from flash\n", favsParse(favs));
    // Internal-RAM stack: this task writes flash (LittleFS), which disables the
    // cache, and a PSRAM stack would then be unreachable.
    xTaskCreatePinnedToCore(workerTask, "SqWork", 12288, nullptr, 2, nullptr, 0);
}
