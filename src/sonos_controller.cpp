/**
 * Sonos Controller - Optimized for ESP32-S3
 * Uses HTTPClient for better connection handling
 */

#include "sonos_controller.h"
#include <HTTPClient.h>
#include "esp_log.h"

static const char* TAG = "SONOS";

// Debounce for button presses
static uint32_t lastCommandTime = 0;
static const uint32_t DEBOUNCE_MS = 400;

SonosController::SonosController() {
    deviceCount = 0;
    currentDeviceIndex = -1;
    deviceMutex = NULL;
    commandQueue = NULL;
    uiUpdateQueue = NULL;
    networkTaskHandle = NULL;
    pollingTaskHandle = NULL;
}

SonosController::~SonosController() {
    if (networkTaskHandle) vTaskDelete(networkTaskHandle);
    if (pollingTaskHandle) vTaskDelete(pollingTaskHandle);
    if (deviceMutex) vSemaphoreDelete(deviceMutex);
    if (commandQueue) vQueueDelete(commandQueue);
    if (uiUpdateQueue) vQueueDelete(uiUpdateQueue);
}

void SonosController::begin() {
    deviceMutex = xSemaphoreCreateMutex();
    commandQueue = xQueueCreate(10, sizeof(CommandRequest_t));
    uiUpdateQueue = xQueueCreate(20, sizeof(UIUpdate_t));
    prefs.begin("sonos", false);
    ESP_LOGI(TAG, "SonosController initialized");
}

void SonosController::startTasks() {
    if (networkTaskHandle == NULL) {
        xTaskCreatePinnedToCore(networkTaskFunction, "SonosNet", 6144, this, 2, &networkTaskHandle, 1);  // Priority 2 (lower), reduced stack
    }
    if (pollingTaskHandle == NULL) {
        xTaskCreatePinnedToCore(pollingTaskFunction, "SonosPoll", 4096, this, 3, &pollingTaskHandle, 1);  // Priority 3, reduced stack
    }
    ESP_LOGI(TAG, "Background tasks started");
}

// ============================================================================
// Discovery
// ============================================================================
int SonosController::discoverDevices() {
    ESP_LOGI(TAG, "Starting discovery...");
    deviceCount = 0;
    
    udp.stop();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    IPAddress multicast(239, 255, 255, 250);
    if (!udp.beginMulticast(multicast, 1900)) {
        ESP_LOGE(TAG, "UDP multicast failed");
        return 0;
    }
    
    const char* msg = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n";
    
    udp.beginPacket(multicast, 1900);
    udp.write((const uint8_t*)msg, strlen(msg));
    udp.endPacket();
    
    unsigned long start = millis();
    while (millis() - start < 10000) {  // 10 seconds for large Sonos setups
        int size = udp.parsePacket();
        if (size > 0) {
            char buf[513];  // 512 + 1 for null terminator
            int len = udp.read(buf, sizeof(buf) - 1);
            if (len > 0 && len < (int)sizeof(buf)) {  // Safety check
                buf[len] = 0;
                if (strstr(buf, "Sonos") || strstr(buf, "ZonePlayer")) {
                    IPAddress ip = udp.remoteIP();
                    
                    bool exists = false;
                    for (int i = 0; i < deviceCount; i++) {
                        if (devices[i].ip == ip) { exists = true; break; }
                    }
                    
                    if (!exists && deviceCount < MAX_SONOS_DEVICES) {
                        devices[deviceCount].ip = ip;
                        devices[deviceCount].roomName = ip.toString();
                        devices[deviceCount].isPlaying = false;
                        devices[deviceCount].volume = 50;
                        devices[deviceCount].isMuted = false;
                        devices[deviceCount].shuffleMode = false;
                        devices[deviceCount].repeatMode = "NONE";
                        devices[deviceCount].connected = false;
                        devices[deviceCount].errorCount = 0;
                        devices[deviceCount].currentTrackNumber = 0;
                        devices[deviceCount].totalTracks = 0;
                        devices[deviceCount].queueSize = 0;
                        devices[deviceCount].groupCoordinatorUUID = "";
                        devices[deviceCount].isGroupCoordinator = true;  // Standalone by default
                        devices[deviceCount].groupMemberCount = 1;

                        ESP_LOGI(TAG, "Found: %s", ip.toString().c_str());
                        deviceCount++;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    udp.stop();
    
    for (int i = 0; i < deviceCount; i++) {
        getRoomName(&devices[i]);
    }
    
    if (deviceCount > 0) {
        prefs.putString("device_ip", devices[0].ip.toString());
    }
    
    ESP_LOGI(TAG, "Found %d device(s)", deviceCount);
    return deviceCount;
}

void SonosController::getRoomName(SonosDevice* dev) {
    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400/xml/device_description.xml", dev->ip.toString().c_str());

    http.begin(url);
    http.setTimeout(2000);

    int code = http.GET();
    if (code == 200) {
        String xml = http.getString();  // Keep String for XML parsing (indexOf/substring)
        int start = xml.indexOf("<roomName>");
        int end = xml.indexOf("</roomName>");
        if (start > 0 && end > start) {
            dev->roomName = xml.substring(start + 10, end);
            ESP_LOGI(TAG, "Room: %s", dev->roomName.c_str());
        }

        start = xml.indexOf("<UDN>uuid:");
        end = xml.indexOf("</UDN>", start);
        if (start > 0 && end > start) {
            dev->rinconID = xml.substring(start + 10, end);
            Serial.printf("[DEVICE] RINCON ID: %s\n", dev->rinconID.c_str());
        }
    }
    http.end();
}

String SonosController::getCachedDeviceIP() {
    return prefs.getString("device_ip", "");
}

void SonosController::cacheDeviceIP(String ip) {
    prefs.putString("device_ip", ip);
}

SonosDevice* SonosController::getDevice(int index) {
    if (index >= 0 && index < deviceCount) return &devices[index];
    return nullptr;
}

SonosDevice* SonosController::getCurrentDevice() {
    if (currentDeviceIndex >= 0 && currentDeviceIndex < deviceCount) {
        return &devices[currentDeviceIndex];
    }
    return nullptr;
}

void SonosController::selectDevice(int index) {
    if (index >= 0 && index < deviceCount) {
        currentDeviceIndex = index;
        devices[index].connected = true;
        ESP_LOGI(TAG, "Selected: %s", devices[index].ip.toString().c_str());
    }
}

// ============================================================================
// SOAP Request - Faster timeout
// ============================================================================
String SonosController::sendSOAP(const char* service, const char* action, const char* args) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return "";

    // Use static buffers to eliminate String allocation/fragmentation
    static char url[256];
    static char body[2048];  // Large buffer for SOAP body
    static char soapAction[256];
    const char* endpoint;

    // Determine endpoint (no String allocation)
    if (strstr(service, "AVTransport")) {
        endpoint = "/MediaRenderer/AVTransport/Control";
    } else if (strstr(service, "RenderingControl")) {
        endpoint = "/MediaRenderer/RenderingControl/Control";
    } else if (strstr(service, "ContentDirectory")) {
        endpoint = "/MediaServer/ContentDirectory/Control";
    } else {
        // Fallback - build endpoint in buffer
        static char custom_endpoint[128];
        snprintf(custom_endpoint, sizeof(custom_endpoint), "/MediaRenderer/%s/Control", service);
        endpoint = custom_endpoint;
    }

    // Build URL without String concatenation
    snprintf(url, sizeof(url), "http://%s:1400%s", dev->ip.toString().c_str(), endpoint);

    // Build SOAP body without String concatenation
    snprintf(body, sizeof(body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%s xmlns:u=\"urn:schemas-upnp-org:service:%s:1\">%s</u:%s>"
        "</s:Body></s:Envelope>",
        action, service, args, action);

    // Build SOAPAction header
    snprintf(soapAction, sizeof(soapAction), "urn:schemas-upnp-org:service:%s:1#%s", service, action);

    // Simple, robust: Create fresh HTTPClient for each request (no pooling)
    HTTPClient http;
    http.begin(url);
    http.setTimeout(2000);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");

    // Build full header value with quotes
    static char soapActionHeader[280];
    snprintf(soapActionHeader, sizeof(soapActionHeader), "\"%s\"", soapAction);
    http.addHeader("SOAPAction", soapActionHeader);

    int code = http.POST(body);
    String response = "";  // Keep String for return value (used by callers)

    if (code == 200) {
        response = http.getString();
        dev->errorCount = 0;
        dev->connected = true;
    } else {
        Serial.printf("[SOAP] HTTP error %d for %s.%s\n", code, service, action);
        dev->errorCount++;
        // Immediate disconnect on network errors (connection refused, timeout, etc)
        if (code == -1 || code == -11) {
            if (dev->connected) {
                Serial.printf("[SONOS] Device disconnected (network error %d)\n", code);
            }
            dev->connected = false;
            dev->errorCount = 10;  // Set high to prevent flapping
        } else if (dev->errorCount > 5) {
            if (dev->connected) {
                Serial.println("[SONOS] Device disconnected (too many errors)");
            }
            dev->connected = false;
        }
    }

    http.end();
    return response;
}

// ============================================================================
// Helpers
// ============================================================================
int SonosController::timeToSeconds(String time) {
    int h = 0, m = 0, s = 0;
    int c1 = time.indexOf(':');
    int c2 = time.indexOf(':', c1 + 1);
    
    if (c1 > 0 && c2 > c1) {
        h = time.substring(0, c1).toInt();
        m = time.substring(c1 + 1, c2).toInt();
        s = time.substring(c2 + 1).toInt();
    } else if (c1 > 0) {
        m = time.substring(0, c1).toInt();
        s = time.substring(c1 + 1).toInt();
    }
    return h * 3600 + m * 60 + s;
}

String SonosController::extractXML(const String& xml, const char* tag) {
    String startTag = "<" + String(tag) + ">";
    String endTag = "</" + String(tag) + ">";
    
    int start = xml.indexOf(startTag);
    if (start < 0) {
        // Try with attributes
        startTag = "<" + String(tag) + " ";
        start = xml.indexOf(startTag);
        if (start < 0) return "";
        start = xml.indexOf(">", start);
        if (start < 0) return "";
        start++;
    } else {
        start += startTag.length();
    }
    
    int end = xml.indexOf(endTag, start);
    if (end < 0) return "";
    
    return xml.substring(start, end);
}

String SonosController::decodeHTML(String text) {
    // Optimized single-pass replacement using lookup table
    // Reserve space to avoid multiple reallocations
    text.reserve(text.length() + 10);

    // Lookup table for replacements (pattern -> replacement)
    static const struct { const char* from; const char* to; } replacements[] = {
        // HTML entities (most common first)
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"},

        // URL-encoded
        {"%3a", ":"}, {"%3A", ":"}, {"%2f", "/"}, {"%2F", "/"}, {"%3f", "?"}, {"%3F", "?"},
        {"%3d", "="}, {"%3D", "="}, {"%26", "&"},

        // Numeric HTML entities (hex)
        {"&#xe9;", "e"}, {"&#xE9;", "e"}, {"&#xe8;", "e"}, {"&#xE8;", "e"},
        {"&#xea;", "e"}, {"&#xEA;", "e"}, {"&#xe0;", "a"}, {"&#xE0;", "a"},
        {"&#xe2;", "a"}, {"&#xE2;", "a"}, {"&#xf4;", "o"}, {"&#xF4;", "o"},
        {"&#xf9;", "u"}, {"&#xF9;", "u"}, {"&#xfb;", "u"}, {"&#xFB;", "u"},
        {"&#xee;", "i"}, {"&#xEE;", "i"}, {"&#xe7;", "c"}, {"&#xE7;", "c"},
        {"&#xf1;", "n"}, {"&#xF1;", "n"},

        // Numeric HTML entities (decimal)
        {"&#233;", "e"}, {"&#232;", "e"}, {"&#234;", "e"}, {"&#224;", "a"},
        {"&#226;", "a"}, {"&#244;", "o"}, {"&#249;", "u"}, {"&#251;", "u"},
        {"&#238;", "i"}, {"&#231;", "c"}, {"&#241;", "n"},

        // UTF-8 sequences (2-byte accented)
        {"\xC3\xA9", "e"}, {"\xC3\xA8", "e"}, {"\xC3\xAA", "e"}, {"\xC3\xAB", "e"},
        {"\xC3\xA0", "a"}, {"\xC3\xA1", "a"}, {"\xC3\xA2", "a"}, {"\xC3\xA4", "a"},
        {"\xC3\xB2", "o"}, {"\xC3\xB3", "o"}, {"\xC3\xB4", "o"}, {"\xC3\xB6", "o"},
        {"\xC3\xB9", "u"}, {"\xC3\xBA", "u"}, {"\xC3\xBB", "u"}, {"\xC3\xBC", "u"},
        {"\xC3\xAC", "i"}, {"\xC3\xAD", "i"}, {"\xC3\xAE", "i"}, {"\xC3\xAF", "i"},
        {"\xC3\xA7", "c"}, {"\xC3\xB1", "n"}, {"\xC3\x89", "E"}, {"\xC3\x88", "E"},

        // UTF-8 smart punctuation (3-byte)
        {"\xE2\x80\x98", "'"}, {"\xE2\x80\x99", "'"}, {"\xE2\x80\x9C", "\""},
        {"\xE2\x80\x9D", "\""}, {"\xE2\x80\x93", "-"}, {"\xE2\x80\x94", "--"},
        {"\xE2\x80\xA6", "..."}
    };

    // Single pass through replacements
    for (const auto& r : replacements) {
        text.replace(r.from, r.to);
    }

    return text;
}

// ============================================================================
// Playback Commands - With debounce
// ============================================================================
void SonosController::play() {
    CommandRequest_t cmd = { CMD_PLAY, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::pause() {
    CommandRequest_t cmd = { CMD_PAUSE, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::next() {
    uint32_t now = millis();
    if (now - lastCommandTime < DEBOUNCE_MS) return;
    lastCommandTime = now;
    
    CommandRequest_t cmd = { CMD_NEXT, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::previous() {
    uint32_t now = millis();
    if (now - lastCommandTime < DEBOUNCE_MS) return;
    lastCommandTime = now;
    
    CommandRequest_t cmd = { CMD_PREV, 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::seek(int seconds) {
    CommandRequest_t cmd = { CMD_SEEK, seconds };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::setVolume(int vol) {
    vol = constrain(vol, 0, 100);
    CommandRequest_t cmd = { CMD_SET_VOLUME, vol };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::volumeUp(int step) {
    SonosDevice* d = getCurrentDevice();
    if (d) setVolume(d->volume + step);
}

void SonosController::volumeDown(int step) {
    SonosDevice* d = getCurrentDevice();
    if (d) setVolume(d->volume - step);
}

void SonosController::setMute(bool mute) {
    CommandRequest_t cmd = { CMD_SET_MUTE, mute ? 1 : 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::setShuffle(bool enable) {
    CommandRequest_t cmd = { CMD_SET_SHUFFLE, enable ? 1 : 0 };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::setRepeat(const char* mode) {
    int v = 0;
    if (strcmp(mode, "ONE") == 0) v = 1;
    else if (strcmp(mode, "ALL") == 0) v = 2;
    CommandRequest_t cmd = { CMD_SET_REPEAT, v };
    xQueueSend(commandQueue, &cmd, 0);
}

void SonosController::playQueueItem(int index) {
    // index is 1-based queue position
    CommandRequest_t cmd = { CMD_PLAY_QUEUE_ITEM, index };
    xQueueSend(commandQueue, &cmd, 0);
}

bool SonosController::saveCurrentTrack(const char* playlistName) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[FAV] Device not available or not connected");
        return false;
    }

    Serial.printf("[FAV] Adding current track to: %s\n", playlistName);

    // Get current track number in queue
    int currentTrackNum = dev->currentTrackNumber;
    if (currentTrackNum <= 0) {
        Serial.println("[FAV] No valid track number");
        return false;
    }

    Serial.printf("[FAV] Current track number in queue: %d\n", currentTrackNum);

    // Browse the queue to get the current track WITH metadata
    String browseQueue = sendSOAP("ContentDirectory", "Browse",
        "<ObjectID>Q:0</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>1000</RequestedCount>"
        "<SortCriteria></SortCriteria>");

    // Extract DIDL from Result
    String queueDIDL = extractXML(browseQueue, "Result");

    // Decode HTML entities
    queueDIDL.replace("&lt;", "<");
    queueDIDL.replace("&gt;", ">");
    queueDIDL.replace("&quot;", "\"");
    queueDIDL.replace("&amp;", "&");

    // Find the item for current track number
    String trackMetadata = "";
    String trackURI = "";

    int pos = 0;
    int itemCount = 0;
    while ((pos = queueDIDL.indexOf("<item", pos)) >= 0) {
        itemCount++;

        if (itemCount == currentTrackNum) {
            int endPos = queueDIDL.indexOf("</item>", pos) + 7;
            String itemXML = queueDIDL.substring(pos, endPos);

            // Re-encode for SOAP
            itemXML.replace("&", "&amp;");
            itemXML.replace("<", "&lt;");
            itemXML.replace(">", "&gt;");
            itemXML.replace("\"", "&quot;");

            trackMetadata = itemXML;

            // Also extract URI
            String decodedItem = queueDIDL.substring(pos, endPos);
            int resStart = decodedItem.indexOf("<res");
            if (resStart >= 0) {
                int resEnd = decodedItem.indexOf("</res>", resStart);
                int resContentStart = decodedItem.indexOf(">", resStart) + 1;
                trackURI = decodedItem.substring(resContentStart, resEnd);
            }

            Serial.printf("[FAV] Found track metadata, length: %d\n", trackMetadata.length());
            Serial.printf("[FAV] Track URI: %s\n", trackURI.c_str());
            break;
        }

        pos = queueDIDL.indexOf("</item>", pos) + 7;
    }

    if (trackMetadata.length() == 0 || trackURI.length() == 0) {
        Serial.println("[FAV] Could not find track in queue");
        return false;
    }

    // Check if playlist exists
    String browseResp = sendSOAP("ContentDirectory", "Browse",
        "<ObjectID>SQ:</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>100</RequestedCount>"
        "<SortCriteria></SortCriteria>");

    String didlContent = extractXML(browseResp, "Result");
    didlContent.replace("&lt;", "<");
    didlContent.replace("&gt;", ">");
    didlContent.replace("&quot;", "\"");
    didlContent.replace("&amp;", "&");

    String playlistID = "";
    pos = 0;
    while ((pos = didlContent.indexOf("<container", pos)) >= 0) {
        int endPos = didlContent.indexOf("</container>", pos);
        if (endPos < 0) break;

        String container = didlContent.substring(pos, endPos);

        int idStart = container.indexOf("id=\"") + 4;
        int idEnd = container.indexOf("\"", idStart);
        String id = container.substring(idStart, idEnd);

        int titleStart = container.indexOf("<dc:title>") + 10;
        int titleEnd = container.indexOf("</dc:title>", titleStart);
        if (titleStart < 10 || titleEnd < 0) { pos = endPos; continue; }
        String title = container.substring(titleStart, titleEnd);

        if (title == playlistName) {
            playlistID = id;
            Serial.printf("[FAV] Found existing playlist: %s\n", playlistID.c_str());
            break;
        }

        pos = endPos;
    }

    // Create playlist if doesn't exist
    if (playlistID.length() == 0) {
        Serial.printf("[FAV] Creating playlist: %s\n", playlistName);

        String createArgs = "<InstanceID>0</InstanceID>"
                           "<Title>" + String(playlistName) + "</Title>"
                           "<EnqueuedURI></EnqueuedURI>"
                           "<EnqueuedURIMetaData></EnqueuedURIMetaData>";

        String createResp = sendSOAP("AVTransport", "CreateSavedQueue", createArgs.c_str());
        playlistID = extractXML(createResp, "AssignedObjectID");

        if (playlistID.length() == 0) {
            Serial.println("[FAV] Failed to create playlist");
            return false;
        }
    }

    // Get UpdateID
    String browsePlaylist = sendSOAP("ContentDirectory", "Browse",
        ("<ObjectID>" + playlistID + "</ObjectID>"
        "<BrowseFlag>BrowseMetadata</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>1</RequestedCount>"
        "<SortCriteria></SortCriteria>").c_str());

    String updateID = extractXML(browsePlaylist, "UpdateID");
    if (updateID.length() == 0) updateID = "0";

    // Add track with proper metadata
    String addArgs = "<InstanceID>0</InstanceID>"
                    "<ObjectID>" + playlistID + "</ObjectID>"
                    "<UpdateID>" + updateID + "</UpdateID>"
                    "<EnqueuedURI>" + trackURI + "</EnqueuedURI>"
                    "<EnqueuedURIMetaData>" + trackMetadata + "</EnqueuedURIMetaData>"
                    "<AddAtIndex>4294967295</AddAtIndex>";

    String addResp = sendSOAP("AVTransport", "AddURIToSavedQueue", addArgs.c_str());

    if (addResp.length() > 0 && addResp.indexOf("Fault") < 0) {
        Serial.println("[FAV] Track added to playlist successfully!");
        return true;
    } else {
        Serial.println("[FAV] Failed to add track");
        return false;
    }
}

String SonosController::browseContent(const char* objectID, int startIndex, int count) {
    // Use static buffer to avoid String concatenation
    static char args[512];
    snprintf(args, sizeof(args),
        "<ObjectID>%s</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>%d</StartingIndex>"
        "<RequestedCount>%d</RequestedCount>"
        "<SortCriteria></SortCriteria>",
        objectID, startIndex, count);

    String resp = sendSOAP("ContentDirectory", "Browse", args);

    // Extract and decode DIDL
    String didl = extractXML(resp, "Result");
    didl.replace("&lt;", "<");
    didl.replace("&gt;", ">");
    didl.replace("&quot;", "\"");
    didl.replace("&amp;", "&");

    return didl;
}

bool SonosController::playURI(const char* uri, const char* metadata) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[PLAY] Device not available");
        return false;
    }

    String metaEncoded = String(metadata);
    metaEncoded.replace("&", "&amp;");
    metaEncoded.replace("<", "&lt;");
    metaEncoded.replace(">", "&gt;");
    metaEncoded.replace("\"", "&quot;");

    // Use static buffer to avoid String concatenation
    static char args[1024];
    snprintf(args, sizeof(args),
        "<InstanceID>0</InstanceID>"
        "<CurrentURI>%s</CurrentURI>"
        "<CurrentURIMetaData>%s</CurrentURIMetaData>",
        uri, metaEncoded.c_str());

    String resp = sendSOAP("AVTransport", "SetAVTransportURI", args);

    if (resp.length() > 0 && resp.indexOf("Fault") < 0) {
        // Auto-play after setting URI
        vTaskDelay(pdMS_TO_TICKS(200));
        play();
        return true;
    }

    return false;
}

bool SonosController::playPlaylist(const char* playlistID) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[PLAYLIST] Device not available");
        return false;
    }

    Serial.printf("[PLAYLIST] Loading playlist: %s\n", playlistID);

    sendSOAP("AVTransport", "RemoveAllTracksFromQueue", "<InstanceID>0</InstanceID>");
    vTaskDelay(pdMS_TO_TICKS(100));

    String playlistNum = String(playlistID);
    playlistNum.replace("SQ:", "");

    // Use static buffers to avoid String concatenation
    static char playlistURI[128];
    static char addArgs[512];
    snprintf(playlistURI, sizeof(playlistURI), "file:///jffs/settings/savedqueues.rsq#%s", playlistNum.c_str());
    snprintf(addArgs, sizeof(addArgs),
        "<InstanceID>0</InstanceID>"
        "<EnqueuedURI>%s</EnqueuedURI>"
        "<EnqueuedURIMetaData></EnqueuedURIMetaData>"
        "<DesiredFirstTrackNumberEnqueued>0</DesiredFirstTrackNumberEnqueued>"
        "<EnqueueAsNext>1</EnqueueAsNext>",
        playlistURI);

    Serial.printf("[PLAYLIST] Adding to queue: %s\n", playlistURI);
    String resp = sendSOAP("AVTransport", "AddURIToQueue", addArgs);

    if (resp.length() > 0 && resp.indexOf("Fault") < 0) {
        vTaskDelay(pdMS_TO_TICKS(200));

        static char queueURI[128];
        static char setArgs[256];
        snprintf(queueURI, sizeof(queueURI), "x-rincon-queue:%s#0", dev->rinconID.c_str());
        snprintf(setArgs, sizeof(setArgs),
            "<InstanceID>0</InstanceID>"
            "<CurrentURI>%s</CurrentURI>"
            "<CurrentURIMetaData></CurrentURIMetaData>",
            queueURI);

        Serial.println("[PLAYLIST] Playlist loaded and playing");
        sendSOAP("AVTransport", "SetAVTransportURI", setArgs);
        vTaskDelay(pdMS_TO_TICKS(100));

        sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
        vTaskDelay(pdMS_TO_TICKS(300));
        updateTrackInfo();
        updateQueue();
        return true;
    }

    Serial.println("[PLAYLIST] Failed to add playlist to queue");
    return false;
}

bool SonosController::playContainer(const char* containerURI, const char* metadata) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev || !dev->connected) {
        Serial.println("[CONTAINER] Device not available");
        return false;
    }

    Serial.printf("[CONTAINER] Loading container: %s\n", containerURI);

    String metaDecoded = String(metadata);
    metaDecoded.replace("&amp;", "&");
    metaDecoded.replace("&lt;", "<");
    metaDecoded.replace("&gt;", ">");
    metaDecoded.replace("&quot;", "\"");

    String metaEncoded = metaDecoded;
    metaEncoded.replace("&", "&amp;");
    metaEncoded.replace("<", "&lt;");
    metaEncoded.replace(">", "&gt;");
    metaEncoded.replace("\"", "&quot;");

    Serial.printf("[CONTAINER] Metadata: %s\n", metaDecoded.c_str());

    // Try SetAVTransportURI directly (works for YouTube Music containers)
    static char setArgs[1024];
    snprintf(setArgs, sizeof(setArgs),
        "<InstanceID>0</InstanceID>"
        "<CurrentURI>%s</CurrentURI>"
        "<CurrentURIMetaData>%s</CurrentURIMetaData>",
        containerURI, metaEncoded.c_str());

    Serial.printf("[CONTAINER] Using SetAVTransportURI with metadata\n");
    String resp = sendSOAP("AVTransport", "SetAVTransportURI", setArgs);

    if (resp.length() > 0 && resp.indexOf("Fault") < 0) {
        Serial.println("[CONTAINER] Container loaded and playing");
        vTaskDelay(pdMS_TO_TICKS(100));
        sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
        vTaskDelay(pdMS_TO_TICKS(300));
        updateTrackInfo();
        updateQueue();
        return true;
    }

    Serial.println("[CONTAINER] SetAVTransportURI failed, trying queue-based approach");

    // Fallback: Try AddURIToQueue (works for some other container types)
    static char addArgs[1024];
    snprintf(addArgs, sizeof(addArgs),
        "<InstanceID>0</InstanceID>"
        "<EnqueuedURI>%s</EnqueuedURI>"
        "<EnqueuedURIMetaData>%s</EnqueuedURIMetaData>"
        "<DesiredFirstTrackNumberEnqueued>0</DesiredFirstTrackNumberEnqueued>"
        "<EnqueueAsNext>1</EnqueueAsNext>",
        containerURI, metaEncoded.c_str());

    resp = sendSOAP("AVTransport", "AddURIToQueue", addArgs);

    if (resp.length() > 0 && resp.indexOf("Fault") < 0) {
        Serial.println("[CONTAINER] AddURIToQueue successful");
        vTaskDelay(pdMS_TO_TICKS(200));

        static char queueURI[128];
        static char queueArgs[256];
        snprintf(queueURI, sizeof(queueURI), "x-rincon-queue:%s#0", dev->rinconID.c_str());
        snprintf(queueArgs, sizeof(queueArgs),
            "<InstanceID>0</InstanceID>"
            "<CurrentURI>%s</CurrentURI>"
            "<CurrentURIMetaData></CurrentURIMetaData>",
            queueURI);

        sendSOAP("AVTransport", "SetAVTransportURI", queueArgs);
        vTaskDelay(pdMS_TO_TICKS(100));

        sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
        Serial.println("[CONTAINER] Container loaded and playing via queue");
        vTaskDelay(pdMS_TO_TICKS(300));
        updateTrackInfo();
        updateQueue();
        return true;
    }

    Serial.println("[CONTAINER] Both methods failed");
    return false;
}

String SonosController::listMusicServices() {
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return "";

    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400/MusicServices/Control", dev->ip.toString().c_str());

    const char* body = "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:ListAvailableServices xmlns:u=\"urn:schemas-upnp-org:service:MusicServices:1\">"
        "</u:ListAvailableServices></s:Body></s:Envelope>";

    const char* soapAction = "\"urn:schemas-upnp-org:service:MusicServices:1#ListAvailableServices\"";

    HTTPClient http;
    http.begin(url);
    http.setTimeout(3000);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", soapAction);

    int code = http.POST(body);
    String resp = "";

    if (code == 200) {
        resp = http.getString();
    } else {
        Serial.printf("[SERVICES] HTTP error %d\n", code);
    }

    http.end();
    return resp;
}

String SonosController::getCurrentTrackInfo() {
    String resp = sendSOAP("AVTransport", "GetPositionInfo", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return "";

    // Extract track URI
    String uri = extractXML(resp, "TrackURI");

    // Extract metadata
    String metadata = extractXML(resp, "TrackMetaData");

    // Decode HTML entities in metadata
    metadata.replace("&lt;", "<");
    metadata.replace("&gt;", ">");
    metadata.replace("&quot;", "\"");
    metadata.replace("&amp;", "&");

    // Format output for serial monitor
    String result = "===== TRACK URI =====\n" + uri +
                    "\n\n===== TRACK METADATA =====\n" + metadata +
                    "\n=====================";

    Serial.println("[CAPTURE] " + result);
    return result;
}

int SonosController::getVolume() {
    SonosDevice* d = getCurrentDevice();
    return d ? d->volume : 0;
}

bool SonosController::getMute() {
    SonosDevice* d = getCurrentDevice();
    return d ? d->isMuted : false;
}

// ============================================================================
// State Updates
// ============================================================================
void SonosController::notifyUI(UIUpdateType_e type) {
    UIUpdate_t upd = { type, "" };
    xQueueSend(uiUpdateQueue, &upd, 0);
}

bool SonosController::updateTrackInfo() {
    String resp = sendSOAP("AVTransport", "GetPositionInfo", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        // Get current track number
        String trackNum = extractXML(resp, "Track");
        if (trackNum.length() > 0) {
            dev->currentTrackNumber = trackNum.toInt();
        }
        
        dev->relTime = extractXML(resp, "RelTime");
        dev->relTimeSeconds = timeToSeconds(dev->relTime);
        
        dev->trackDuration = extractXML(resp, "TrackDuration");
        dev->durationSeconds = timeToSeconds(dev->trackDuration);
        
        // Get metadata and decode HTML entities
        String meta = extractXML(resp, "TrackMetaData");
        meta = decodeHTML(meta);

        String newTrack = decodeHTML(extractXML(meta, "dc:title"));
        String newArtist = decodeHTML(extractXML(meta, "dc:creator"));
        String newAlbum = decodeHTML(extractXML(meta, "upnp:album"));

        // Extract album art URL
        String art = extractXML(meta, "upnp:albumArtURI");
        art = decodeHTML(art);

        String newArtURL = "";
        if (art.length() > 0) {
            if (art.startsWith("/")) {
                // Local Sonos path - convert to full URL
                newArtURL = "http://" + dev->ip.toString() + ":1400" + art;
            } else {
                newArtURL = art;
            }
        }

        // Check if anything actually changed
        bool changed = (newTrack != dev->currentTrack) ||
                       (newArtist != dev->currentArtist) ||
                       (newAlbum != dev->currentAlbum) ||
                       (newArtURL != dev->albumArtURL);

        // Update values
        dev->currentTrack = newTrack;
        dev->currentArtist = newArtist;
        dev->currentAlbum = newAlbum;
        dev->albumArtURL = newArtURL;

        xSemaphoreGive(deviceMutex);

        // Only notify UI if something changed
        if (changed) {
            notifyUI(UPDATE_TRACK_INFO);
        }

        return true;
    }
    return false;
}

bool SonosController::updatePlaybackState() {
    String resp = sendSOAP("AVTransport", "GetTransportInfo", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        dev->isPlaying = (resp.indexOf("PLAYING") > 0);
        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_PLAYBACK_STATE);
        return true;
    }
    return false;
}

bool SonosController::updateVolume() {
    String resp = sendSOAP("RenderingControl", "GetVolume", 
        "<InstanceID>0</InstanceID><Channel>Master</Channel>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        String vol = extractXML(resp, "CurrentVolume");
        if (vol.length() > 0) dev->volume = vol.toInt();
        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_VOLUME);
        return true;
    }
    return false;
}

bool SonosController::updateTransportSettings() {
    String resp = sendSOAP("AVTransport", "GetTransportSettings", "<InstanceID>0</InstanceID>");
    if (resp.length() == 0) return false;
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
        String mode = extractXML(resp, "PlayMode");
        dev->shuffleMode = (mode.indexOf("SHUFFLE") >= 0);
        
        if (mode.indexOf("REPEAT_ONE") >= 0) dev->repeatMode = "ONE";
        else if (mode.indexOf("REPEAT") >= 0) dev->repeatMode = "ALL";
        else dev->repeatMode = "NONE";
        
        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_TRANSPORT);
        return true;
    }
    return false;
}

bool SonosController::updateQueue() {
    String resp = sendSOAP("ContentDirectory", "Browse",
        "<ObjectID>Q:0</ObjectID>"
        "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter>"
        "<StartingIndex>0</StartingIndex>"
        "<RequestedCount>50</RequestedCount>"
        "<SortCriteria></SortCriteria>");
    
    if (resp.length() == 0) {
        ESP_LOGW(TAG, "Queue response empty");
        return false;
    }
    
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return false;
    
    if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
        String total = extractXML(resp, "TotalMatches");
        if (total.length() > 0) {
            dev->totalTracks = total.toInt();
        }
        
        String numReturned = extractXML(resp, "NumberReturned");
        ESP_LOGI(TAG, "Queue: total=%d, returned=%s", dev->totalTracks, numReturned.c_str());
        
        // Get the Result which contains DIDL-Lite
        String result = extractXML(resp, "Result");
        result = decodeHTML(result);
        
        dev->queueSize = 0;
        int pos = 0;
        
        while (dev->queueSize < QUEUE_ITEMS_MAX && pos < (int)result.length()) {
            int itemStart = result.indexOf("<item", pos);
            if (itemStart < 0) break;
            
            int itemEnd = result.indexOf("</item>", itemStart);
            if (itemEnd < 0) break;
            
            String item = result.substring(itemStart, itemEnd + 7);
            
            String title = extractXML(item, "dc:title");
            String artist = extractXML(item, "dc:creator");
            String album = extractXML(item, "upnp:album");
            String artUrl = extractXML(item, "upnp:albumArtURI");
            
            dev->queue[dev->queueSize].title = decodeHTML(title);
            dev->queue[dev->queueSize].artist = decodeHTML(artist);
            dev->queue[dev->queueSize].album = decodeHTML(album);
            dev->queue[dev->queueSize].albumArtURL = decodeHTML(artUrl);
            dev->queue[dev->queueSize].trackNumber = dev->queueSize + 1;
            dev->queueSize++;
            
            pos = itemEnd + 7;
        }
        
        ESP_LOGI(TAG, "Parsed %d queue items", dev->queueSize);
        
        xSemaphoreGive(deviceMutex);
        notifyUI(UPDATE_QUEUE);
        return true;
    }
    return false;
}

// ============================================================================
// Command Processing
// ============================================================================
void SonosController::processCommand(CommandRequest_t* cmd) {
    SonosDevice* dev = getCurrentDevice();
    if (!dev) return;
    
    String args;
    
    switch (cmd->type) {
        case CMD_PLAY:
            sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isPlaying = true;
                xSemaphoreGive(deviceMutex);
            }
            notifyUI(UPDATE_PLAYBACK_STATE);
            break;
            
        case CMD_PAUSE:
            sendSOAP("AVTransport", "Pause", "<InstanceID>0</InstanceID>");
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isPlaying = false;
                xSemaphoreGive(deviceMutex);
            }
            notifyUI(UPDATE_PLAYBACK_STATE);
            break;
            
        case CMD_NEXT:
            sendSOAP("AVTransport", "Next", "<InstanceID>0</InstanceID>");
            vTaskDelay(pdMS_TO_TICKS(200));
            updateTrackInfo();
            break;
            
        case CMD_PREV:
            sendSOAP("AVTransport", "Previous", "<InstanceID>0</InstanceID>");
            vTaskDelay(pdMS_TO_TICKS(200));
            updateTrackInfo();
            break;
            
        case CMD_SET_VOLUME:
            args = "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>" + 
                   String(cmd->value) + "</DesiredVolume>";
            sendSOAP("RenderingControl", "SetVolume", args.c_str());
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->volume = cmd->value;
                xSemaphoreGive(deviceMutex);
            }
            break;
            
        case CMD_SET_MUTE:
            args = "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredMute>" + 
                   String(cmd->value) + "</DesiredMute>";
            sendSOAP("RenderingControl", "SetMute", args.c_str());
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isMuted = (cmd->value == 1);
                xSemaphoreGive(deviceMutex);
            }
            break;
            
        case CMD_SET_SHUFFLE: {
            String mode = (cmd->value == 1) ? "SHUFFLE" : "NORMAL";
            args = "<InstanceID>0</InstanceID><NewPlayMode>" + mode + "</NewPlayMode>";
            sendSOAP("AVTransport", "SetPlayMode", args.c_str());
            updateTransportSettings();
            break;
        }
            
        case CMD_SET_REPEAT: {
            String mode = "NORMAL";
            if (cmd->value == 1) mode = "REPEAT_ONE";
            else if (cmd->value == 2) mode = "REPEAT_ALL";
            args = "<InstanceID>0</InstanceID><NewPlayMode>" + mode + "</NewPlayMode>";
            sendSOAP("AVTransport", "SetPlayMode", args.c_str());
            updateTransportSettings();
            break;
        }
            
        case CMD_SEEK: {
            int h = cmd->value / 3600;
            int m = (cmd->value % 3600) / 60;
            int s = cmd->value % 60;
            char t[16];
            snprintf(t, sizeof(t), "%02d:%02d:%02d", h, m, s);
            args = "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>" + String(t) + "</Target>";
            sendSOAP("AVTransport", "Seek", args.c_str());
            break;
        }
        
        case CMD_PLAY_QUEUE_ITEM: {
            // Seek to queue position and play
            // Use TRACK_NR seek mode to jump to specific track
            args = "<InstanceID>0</InstanceID><Unit>TRACK_NR</Unit><Target>" + String(cmd->value) + "</Target>";
            sendSOAP("AVTransport", "Seek", args.c_str());
            vTaskDelay(pdMS_TO_TICKS(100));
            sendSOAP("AVTransport", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(50))) {
                dev->isPlaying = true;
                xSemaphoreGive(deviceMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            updateTrackInfo();
            break;
        }
            
        default:
            break;
    }
}

// ============================================================================
// Background Tasks - Faster polling
// ============================================================================
void SonosController::networkTaskFunction(void* param) {
    SonosController* ctrl = (SonosController*)param;
    CommandRequest_t cmd;
    
    ESP_LOGI(TAG, "Network task started");
    
    while (1) {
        if (xQueueReceive(ctrl->commandQueue, &cmd, pdMS_TO_TICKS(20))) {
            ctrl->processCommand(&cmd);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void SonosController::pollingTaskFunction(void* param) {
    SonosController* ctrl = (SonosController*)param;
    uint32_t tick = 0;
    
    ESP_LOGI(TAG, "Polling task started");
    
    // Initial queue load
    vTaskDelay(pdMS_TO_TICKS(1000));
    ctrl->updateQueue();
    
    while (1) {
        SonosDevice* dev = ctrl->getCurrentDevice();

        if (dev && dev->connected) {
            // Track info every cycle for instant updates when changing sources
            ctrl->updateTrackInfo();
            ctrl->updatePlaybackState();

            // Volume every 1.5 seconds (5 * 300ms)
            if (tick % 5 == 0) {
                ctrl->updateVolume();
            }

            // Transport settings every 3 seconds (10 * 300ms)
            if (tick % 10 == 0) {
                ctrl->updateTransportSettings();
            }

            // Queue every 15 seconds (50 * 300ms)
            if (tick % 50 == 0) {
                ctrl->updateQueue();
            }

            tick++;
        }

        vTaskDelay(pdMS_TO_TICKS(300));  // 300ms base interval (faster polling)
    }
}

void SonosController::handleNetworkError(const char* msg) {
    ESP_LOGE(TAG, "Error: %s", msg);
}

void SonosController::resetErrorCount() {
    SonosDevice* dev = getCurrentDevice();
    if (dev) dev->errorCount = 0;
}

// ============================================================================
// Group Management
// ============================================================================

bool SonosController::joinGroup(int deviceIndex, int coordinatorIndex) {
    if (deviceIndex < 0 || deviceIndex >= deviceCount) return false;
    if (coordinatorIndex < 0 || coordinatorIndex >= deviceCount) return false;
    if (deviceIndex == coordinatorIndex) return false;  // Can't join self

    SonosDevice* device = &devices[deviceIndex];
    SonosDevice* coordinator = &devices[coordinatorIndex];

    if (coordinator->rinconID.length() == 0) {
        Serial.println("[GROUP] Coordinator has no RINCON ID");
        return false;
    }

    // Build the x-rincon URI to join the coordinator
    char uri[128];
    snprintf(uri, sizeof(uri), "x-rincon:%s", coordinator->rinconID.c_str());

    // Send SetAVTransportURI to the device we want to join
    // This tells it to follow the coordinator's playback
    char args[256];
    snprintf(args, sizeof(args),
        "<InstanceID>0</InstanceID>"
        "<CurrentURI>%s</CurrentURI>"
        "<CurrentURIMetaData></CurrentURIMetaData>",
        uri);

    // We need to send this to the device being joined, not the current device
    // Save and restore current device
    int savedIndex = currentDeviceIndex;
    currentDeviceIndex = deviceIndex;

    String resp = sendSOAP("AVTransport", "SetAVTransportURI", args);

    currentDeviceIndex = savedIndex;

    bool success = (resp.length() > 0 && resp.indexOf("Fault") < 0);

    if (success) {
        Serial.printf("[GROUP] %s joined group with coordinator %s\n",
            device->roomName.c_str(), coordinator->roomName.c_str());

        // Update group info
        if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
            device->groupCoordinatorUUID = coordinator->rinconID;
            device->isGroupCoordinator = false;
            coordinator->isGroupCoordinator = true;
            xSemaphoreGive(deviceMutex);
        }

        notifyUI(UPDATE_GROUPS);
    } else {
        Serial.printf("[GROUP] Failed to join %s to group\n", device->roomName.c_str());
    }

    return success;
}

bool SonosController::leaveGroup(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= deviceCount) return false;

    SonosDevice* device = &devices[deviceIndex];

    // Save and restore current device
    int savedIndex = currentDeviceIndex;
    currentDeviceIndex = deviceIndex;

    // BecomeCoordinatorOfStandaloneGroup makes the device leave its group
    // and become a standalone player
    String resp = sendSOAP("AVTransport", "BecomeCoordinatorOfStandaloneGroup",
        "<InstanceID>0</InstanceID>");

    currentDeviceIndex = savedIndex;

    bool success = (resp.length() > 0 && resp.indexOf("Fault") < 0);

    if (success) {
        Serial.printf("[GROUP] %s left group (now standalone)\n", device->roomName.c_str());

        if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
            device->groupCoordinatorUUID = "";
            device->isGroupCoordinator = true;  // Standalone = coordinator of self
            device->groupMemberCount = 1;
            xSemaphoreGive(deviceMutex);
        }

        notifyUI(UPDATE_GROUPS);
    } else {
        Serial.printf("[GROUP] Failed to remove %s from group\n", device->roomName.c_str());
    }

    return success;
}

void SonosController::updateGroupInfo() {
    // Query each device for its group coordinator
    int savedIndex = currentDeviceIndex;

    for (int i = 0; i < deviceCount; i++) {
        currentDeviceIndex = i;
        SonosDevice* dev = &devices[i];

        // GetMediaInfo contains the current transport URI which tells us about grouping
        String resp = sendSOAP("AVTransport", "GetMediaInfo", "<InstanceID>0</InstanceID>");

        if (resp.length() > 0) {
            String currentURI = extractXML(resp, "CurrentURI");

            if (xSemaphoreTake(deviceMutex, pdMS_TO_TICKS(100))) {
                // Check if this device is following another (x-rincon:RINCON_xxx format)
                if (currentURI.startsWith("x-rincon:")) {
                    // Extract coordinator RINCON from URI
                    dev->groupCoordinatorUUID = currentURI.substring(9);  // Skip "x-rincon:"
                    dev->isGroupCoordinator = false;
                } else {
                    // Not following anyone - either standalone or is a coordinator
                    dev->groupCoordinatorUUID = dev->rinconID;  // Self
                    dev->isGroupCoordinator = true;
                }
                xSemaphoreGive(deviceMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // Small delay between queries
    }

    // Now count members for each coordinator
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].isGroupCoordinator) {
            int count = 1;  // Count self
            for (int j = 0; j < deviceCount; j++) {
                if (i != j && devices[j].groupCoordinatorUUID == devices[i].rinconID) {
                    count++;
                }
            }
            devices[i].groupMemberCount = count;
        } else {
            devices[i].groupMemberCount = 0;  // Non-coordinators don't have members
        }
    }

    currentDeviceIndex = savedIndex;
    notifyUI(UPDATE_GROUPS);
}

int SonosController::getGroupMemberCount(int coordinatorIndex) {
    if (coordinatorIndex < 0 || coordinatorIndex >= deviceCount) return 0;

    SonosDevice* coordinator = &devices[coordinatorIndex];
    if (!coordinator->isGroupCoordinator) return 0;

    int count = 1;  // Count coordinator itself
    for (int i = 0; i < deviceCount; i++) {
        if (i != coordinatorIndex &&
            devices[i].groupCoordinatorUUID == coordinator->rinconID) {
            count++;
        }
    }
    return count;
}

bool SonosController::isDeviceInGroup(int deviceIndex, int coordinatorIndex) {
    if (deviceIndex < 0 || deviceIndex >= deviceCount) return false;
    if (coordinatorIndex < 0 || coordinatorIndex >= deviceCount) return false;

    if (deviceIndex == coordinatorIndex) return true;  // Coordinator is in own group

    SonosDevice* device = &devices[deviceIndex];
    SonosDevice* coordinator = &devices[coordinatorIndex];

    return (device->groupCoordinatorUUID == coordinator->rinconID);
}