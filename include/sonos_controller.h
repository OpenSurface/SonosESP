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
    CMD_UPDATE_STATE
} SonosCommand_e;

typedef struct {
    SonosCommand_e type;
    int32_t value;
} CommandRequest_t;

// UI update notifications
typedef enum {
    UPDATE_TRACK_INFO,
    UPDATE_PLAYBACK_STATE,
    UPDATE_VOLUME,
    UPDATE_TRANSPORT,
    UPDATE_QUEUE,
    UPDATE_ALBUM_ART,
    UPDATE_ERROR
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
    
    // Queue
    int currentTrackNumber;
    int totalTracks;
    QueueItem queue[QUEUE_ITEMS_MAX];
    int queueSize;
    
    // Connection state
    bool connected;
    uint32_t lastUpdateTime;
    uint32_t errorCount;
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
};

#endif // SONOS_CONTROLLER_H