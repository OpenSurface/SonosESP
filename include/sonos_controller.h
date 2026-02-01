#ifndef SONOS_CONTROLLER_H
#define SONOS_CONTROLLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define MAX_SONOS_DEVICES 10
#define QUEUE_ITEMS_MAX 50  // Keep at 50 for stable performance

// Command queue for network task
typedef enum {
    CMD_PLAY,
    CMD_PAUSE,
    CMD_NEXT,
    CMD_PREV,
    CMD_SET_VOLUME,
    CMD_SET_MUTE,
    CMD_SET_SHUFFLE,
    CMD_SET_REPEAT,
    CMD_SEEK,
    CMD_PLAY_QUEUE_ITEM,
    CMD_UPDATE_STATE,
    CMD_JOIN_GROUP,
    CMD_LEAVE_GROUP
} SonosCommand_e;

typedef struct {
    SonosCommand_e type;
    int32_t value;
    int32_t value2;  // Secondary value for group commands (target device index)
} CommandRequest_t;

// UI update notifications
typedef enum {
    UPDATE_TRACK_INFO,
    UPDATE_PLAYBACK_STATE,
    UPDATE_VOLUME,
    UPDATE_TRANSPORT,
    UPDATE_QUEUE,
    UPDATE_ALBUM_ART,
    UPDATE_ERROR,
    UPDATE_GROUPS
} UIUpdateType_e;

typedef struct {
    UIUpdateType_e type;
    char message[128];
} UIUpdate_t;

struct QueueItem {
    String title;
    String artist;
    String album;
    String duration;
    int trackNumber;
    String albumArtURL;
};

struct SonosDevice {
    IPAddress ip;
    String name;
    String roomName;
    String rinconID;

    // Playback state
    bool isPlaying;
    int volume;
    bool isMuted;
    bool shuffleMode;
    String repeatMode;       // "NONE", "ONE", "ALL"
    
    // Track info
    String currentTrack;
    String currentArtist;
    String currentAlbum;
    String albumArtURL;
    String relTime;          // Current position "0:02:15"
    String trackDuration;    // Total duration "0:03:47"
    int relTimeSeconds;      // Current position in seconds
    int durationSeconds;     // Total duration in seconds

    // Radio station info
    bool isRadioStation;          // True if playing radio (detected by URI pattern)
    String currentURI;            // Track URI (needed for radio detection)
    String radioStationName;      // Station name from GetMediaInfo's CurrentURIMetaData
    String radioStationArtURL;    // Station logo URL (fallback when song has no art)
    String streamContent;         // Current song from r:streamContent (if available)

    // Queue
    int currentTrackNumber;
    int totalTracks;
    QueueItem queue[QUEUE_ITEMS_MAX];
    int queueSize;
    
    // Connection state
    bool connected;
    uint32_t lastUpdateTime;
    uint32_t errorCount;

    // Group info
    String groupCoordinatorUUID;  // RINCON ID of the group coordinator (empty if standalone)
    bool isGroupCoordinator;      // True if this device is the coordinator of its group
    int groupMemberCount;         // Number of members in this device's group (1 if standalone)
};

class SonosController {
private:
    SonosDevice devices[MAX_SONOS_DEVICES];
    int deviceCount;
    int currentDeviceIndex;
    WiFiUDP udp;
    WiFiClient client;
    Preferences prefs;

    // FreeRTOS synchronization
    SemaphoreHandle_t deviceMutex;
    QueueHandle_t commandQueue;
    QueueHandle_t uiUpdateQueue;
    TaskHandle_t networkTaskHandle;
    TaskHandle_t pollingTaskHandle;
    
    // Internal methods
    String sendSOAP(const char* service, const char* action, const char* args);
    void getRoomName(SonosDevice* dev);
    int timeToSeconds(String time);
    void notifyUI(UIUpdateType_e type);
    
    // Task functions
    static void networkTaskFunction(void* parameter);
    static void pollingTaskFunction(void* parameter);
    void processCommand(CommandRequest_t* cmd);
    
public:
    SonosController();
    ~SonosController();
    
    // Initialization
    void begin();
    void startTasks();
    
    // Discovery
    int discoverDevices();
    String getCachedDeviceIP();
    void cacheDeviceIP(String ip);
    bool tryLoadCachedDevice();        // Try to load cached device from NVS (fast boot)
    void cacheSelectedDevice();        // Save selected device to NVS
    int getDeviceCount() { return deviceCount; }
    SonosDevice* getDevice(int index);
    SonosDevice* getCurrentDevice();
    void selectDevice(int index);
    
    // Playback control (non-blocking, queued)
    void play();
    void pause();
    void next();
    void previous();
    void seek(int seconds);
    void setShuffle(bool enable);
    void setRepeat(const char* mode);  // "NONE", "ONE", "ALL"
    void playQueueItem(int index);     // Play specific track from queue (1-based)
    bool saveCurrentTrack(const char* playlistName = "Favorites");  // Save current track to playlist
    String browseContent(const char* objectID, int startIndex = 0, int count = 100);  // Browse ContentDirectory
    bool playURI(const char* uri, const char* metadata = "");  // Play URI with optional metadata
    bool playPlaylist(const char* playlistID);  // Play a Sonos playlist by ID (e.g., "SQ:25")
    bool playContainer(const char* containerURI, const char* metadata = "");  // Play a container URI with DIDL metadata
    String listMusicServices();  // List available music services
    String getCurrentTrackInfo();  // Get current track URI and metadata for analysis

    // Helper methods (public for UI)
    String extractXML(const String& xml, const char* tag);
    String decodeHTML(String text);

    // Volume control (non-blocking, queued)
    void setVolume(int volume);
    void volumeUp(int step = 5);
    void volumeDown(int step = 5);
    void setMute(bool mute);
    int getVolume();
    bool getMute();
    
    // State queries (thread-safe)
    bool updateTrackInfo();
    bool updateMediaInfo();          // Get station name for radio from GetMediaInfo
    bool updatePlaybackState();
    bool updateVolume();
    bool updateQueue();
    bool updateTransportSettings();
    
    // Queue access
    QueueHandle_t getCommandQueue() { return commandQueue; }
    QueueHandle_t getUIUpdateQueue() { return uiUpdateQueue; }
    
    // Error handling
    void handleNetworkError(const char* message);
    void resetErrorCount();

    // Group management
    bool joinGroup(int deviceIndex, int coordinatorIndex);   // Join device to coordinator's group
    bool leaveGroup(int deviceIndex);                        // Remove device from its group (make standalone)
    void updateGroupInfo();                                  // Refresh group membership info for all devices
    int getGroupMemberCount(int coordinatorIndex);           // Get number of members in a group
    bool isDeviceInGroup(int deviceIndex, int coordinatorIndex);  // Check if device is in coordinator's group

    // Task management for OTA
    void suspendTasks();  // Suspend polling/network tasks for OTA
    void resumeTasks();   // Resume polling/network tasks after OTA
};

#endif // SONOS_CONTROLLER_H