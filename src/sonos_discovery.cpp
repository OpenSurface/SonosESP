/**
 * Sonos Device Discovery - SSDP/UPnP Implementation
 * Handles multicast/broadcast discovery and device metadata fetching
 */

#include "sonos_controller.h"
#include "ui_common.h"

// ============================================================================
// Discovery
// ============================================================================
int SonosController::discoverDevices() {
    Serial.printf("[SONOS] Starting discovery...\n");
    deviceCount = 0;

    udp.stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    IPAddress multicast(239, 255, 255, 250);
    // Bind a UDP socket to receive unicast SSDP responses (replies go to the sender's source port)
    if (!udp.begin(1900)) {
        Serial.printf("[SONOS] UDP begin failed on port 1900\n");
        return 0;
    }

    const char* msg =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n";

    // Send discovery to both multicast (239.255.255.250) AND broadcast (255.255.255.255)
    // This ensures discovery works even if routers don't properly handle multicast
    // Send 5 bursts with 500ms spacing for balance between coverage and speed
    IPAddress broadcast(255, 255, 255, 255);

    for (int burst = 0; burst < 5; burst++) {
        // Send to multicast address (standard UPnP)
        udp.beginPacket(multicast, 1900);
        udp.write((const uint8_t*)msg, strlen(msg));
        udp.endPacket();

        // Also send to broadcast address (for networks with multicast issues)
        udp.beginPacket(broadcast, 1900);
        udp.write((const uint8_t*)msg, strlen(msg));
        udp.endPacket();

        Serial.printf("[SONOS] Sent discovery burst %d/5 (multicast + broadcast)\n", burst + 1);

        if (burst < 4) {
            vTaskDelay(pdMS_TO_TICKS(500));  // 500ms between bursts - balance between coverage and speed
        }
    }

    int rawDeviceCount = 0;  // Count of IPs found before dedup
    unsigned long start = millis();
    unsigned long lastUIUpdate = 0;
    while (millis() - start < 15000) {  // 15 seconds total (5 bursts * 0.5s = 2.5s send + 12.5s listen)
        int size = udp.parsePacket();
        if (size > 0) {
            char buf[1025];  // 1024 + 1 for null terminator
            int len = udp.read(buf, sizeof(buf) - 1);
            if (len > 0 && len < (int)sizeof(buf)) {  // Safety check
                buf[len] = 0;
                String resp = buf;
                resp.toLowerCase();
                if (resp.indexOf("sonos") >= 0 || resp.indexOf("zoneplayer") >= 0) {
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

                        Serial.printf("[SONOS] SSDP Response #%d: %s\n", deviceCount + 1, ip.toString().c_str());
                        deviceCount++;
                        rawDeviceCount++;
                    } else if (exists) {
                        Serial.printf("[SONOS] Ignoring duplicate SSDP response from: %s\n", ip.toString().c_str());
                    }

                    if (deviceCount >= MAX_SONOS_DEVICES) {
                        Serial.printf("[SONOS] Reached MAX_SONOS_DEVICES limit (%d)\n", MAX_SONOS_DEVICES);
                    }
                }
            }
        }

        // Update UI periodically to keep spinner animating
        if (millis() - lastUIUpdate > 20) {
            lv_tick_inc(20);
            lv_timer_handler();
            lv_refr_now(NULL);  // Force display refresh
            lastUIUpdate = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    udp.stop();

    Serial.printf("[SONOS] Discovery window closed. Found %d raw IP(s) before deduplication.\n", rawDeviceCount);

    if (deviceCount == 0) {
        Serial.printf("[SONOS] No Sonos devices responded to discovery. Check network connectivity and ensure devices are powered on.\n");
        return 0;
    }

    if (deviceCount == 1 && rawDeviceCount == 1) {
        Serial.printf("[SONOS] Only 1 device found. If you have more speakers, try scanning again or check:\n");
        Serial.printf("[SONOS]   - All speakers are powered on and connected to WiFi\n");
        Serial.printf("[SONOS]   - ESP32 and Sonos devices are on the same network/VLAN\n");
        Serial.printf("[SONOS]   - Router allows multicast/UPnP traffic\n");
    }

    // Fetch room names for all discovered devices
    Serial.printf("[SONOS] Fetching room names for %d device(s)...\n", deviceCount);
    for (int i = 0; i < deviceCount; i++) {
        Serial.printf("[SONOS] Fetching room name %d/%d from %s\n", i + 1, deviceCount, devices[i].ip.toString().c_str());
        getRoomName(&devices[i]);
        Serial.printf("[SONOS]   -> Room name: '%s'\n", devices[i].roomName.c_str());

        // Update UI while fetching room names
        lv_tick_inc(10);
        lv_timer_handler();
        lv_refr_now(NULL);
    }

    // Deduplicate by room name to handle stereo pairs
    // In stereo pairs, both speakers respond to SSDP but have the same room name
    // Keep the first occurrence (usually the primary/left speaker)
    Serial.printf("[SONOS] Starting deduplication process...\n");
    int uniqueCount = 0;
    for (int i = 0; i < deviceCount; i++) {
        // Normalize room name: trim whitespace and convert to lowercase for comparison
        String normalizedCurrent = devices[i].roomName;
        normalizedCurrent.trim();
        normalizedCurrent.toLowerCase();

        bool isDuplicate = false;
        for (int j = 0; j < uniqueCount; j++) {
            String normalizedExisting = devices[j].roomName;
            normalizedExisting.trim();
            normalizedExisting.toLowerCase();

            if (normalizedExisting == normalizedCurrent) {
                Serial.printf("[SONOS]   [DUPLICATE] '%s' (%s) matches existing '%s' (%s) - filtering out\n",
                    devices[i].roomName.c_str(), devices[i].ip.toString().c_str(),
                    devices[j].roomName.c_str(), devices[j].ip.toString().c_str());
                isDuplicate = true;
                break;
            }
        }

        if (!isDuplicate) {
            Serial.printf("[SONOS]   [UNIQUE] '%s' (%s) - keeping\n",
                devices[i].roomName.c_str(), devices[i].ip.toString().c_str());
            if (i != uniqueCount) {
                // Move device to compact position
                devices[uniqueCount] = devices[i];
            }
            uniqueCount++;
        }
    }

    int filteredCount = deviceCount - uniqueCount;
    if (filteredCount > 0) {
        Serial.printf("[SONOS] Filtered %d duplicate(s) from stereo pairs\n", filteredCount);
    } else {
        Serial.printf("[SONOS] No duplicates found - all %d devices are unique\n", uniqueCount);
    }
    deviceCount = uniqueCount;

    if (deviceCount > 0) {
        prefs.putString("device_ip", devices[0].ip.toString());
    }

    Serial.printf("[SONOS] Discovery complete: %d visible zone(s)\n", deviceCount);
    return deviceCount;
}

void SonosController::getRoomName(SonosDevice* dev) {
    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400/xml/device_description.xml", dev->ip.toString().c_str());

    http.begin(url);
    http.setTimeout(3000);  // Increased from 2s to 3s for slower networks

    int code = http.GET();
    if (code == 200) {
        String xml = http.getString();  // Keep String for XML parsing (indexOf/substring)
        int start = xml.indexOf("<roomName>");
        int end = xml.indexOf("</roomName>");
        if (start > 0 && end > start) {
            dev->roomName = xml.substring(start + 10, end);
            Serial.printf("[SONOS]   Room name fetched successfully: '%s'\n", dev->roomName.c_str());
        } else {
            Serial.printf("[SONOS]   Failed to parse room name from XML for %s\n", dev->ip.toString().c_str());
        }

        start = xml.indexOf("<UDN>uuid:");
        end = xml.indexOf("</UDN>", start);
        if (start > 0 && end > start) {
            dev->rinconID = xml.substring(start + 10, end);
            Serial.printf("[SONOS]   RINCON ID: %s\n", dev->rinconID.c_str());
        } else {
            Serial.printf("[SONOS]   Failed to parse RINCON ID from XML for %s\n", dev->ip.toString().c_str());
        }
    } else {
        Serial.printf("[SONOS]   HTTP GET failed with code %d for %s (keeping IP as name)\n", code, dev->ip.toString().c_str());
    }
    http.end();
}

String SonosController::getCachedDeviceIP() {
    return prefs.getString("device_ip", "");
}

void SonosController::cacheDeviceIP(String ip) {
    prefs.putString("device_ip", ip);
}

void SonosController::cacheSelectedDevice() {
    SonosDevice* dev = getCurrentDevice();
    if (!dev) {
        Serial.println("[SONOS] Cannot cache - no device selected");
        return;
    }

    prefs.putString("cached_ip", dev->ip.toString());
    prefs.putString("cached_room", dev->roomName);
    prefs.putString("cached_rincon", dev->rinconID);

    Serial.printf("[SONOS] Cached device: %s (%s) [%s]\n",
                  dev->roomName.c_str(),
                  dev->ip.toString().c_str(),
                  dev->rinconID.c_str());
}

bool SonosController::tryLoadCachedDevice() {
    String cachedIP = prefs.getString("cached_ip", "");
    String cachedRoom = prefs.getString("cached_room", "");
    String cachedRincon = prefs.getString("cached_rincon", "");

    if (cachedIP.length() == 0 || cachedRoom.length() == 0) {
        Serial.println("========================================");
        Serial.println("[SONOS] No cached device in NVS");
        Serial.println("[SONOS] Running full SSDP discovery (~15 seconds)...");
        Serial.println("========================================");
        return false;
    }

    Serial.printf("[SONOS] Found cached device: %s (%s)\n", cachedRoom.c_str(), cachedIP.c_str());

    // Parse IP address
    IPAddress ip;
    if (!ip.fromString(cachedIP)) {
        Serial.printf("[SONOS] Invalid cached IP: %s - will run discovery\n", cachedIP.c_str());
        return false;
    }

    // Quick HTTP check to verify device is reachable (with short timeout)
    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400/xml/device_description.xml", cachedIP.c_str());

    http.begin(url);
    http.setTimeout(2000);  // 2s timeout - fast check

    Serial.printf("[SONOS] Verifying cached device is reachable at %s...\n", cachedIP.c_str());
    int code = http.GET();
    http.end();

    if (code != 200) {
        Serial.println("========================================");
        Serial.printf("[SONOS] Cached device '%s' unreachable (HTTP %d)\n", cachedRoom.c_str(), code);
        Serial.println("[SONOS] Running full SSDP discovery (~15 seconds)...");
        Serial.println("========================================");
        return false;
    }

    // Device is reachable - add it to the device list
    deviceCount = 1;
    devices[0].ip = ip;
    devices[0].roomName = cachedRoom;
    devices[0].rinconID = cachedRincon;
    devices[0].isPlaying = false;
    devices[0].volume = 50;
    devices[0].isMuted = false;
    devices[0].shuffleMode = false;
    devices[0].repeatMode = "NONE";
    devices[0].connected = false;
    devices[0].errorCount = 0;
    devices[0].currentTrackNumber = 0;
    devices[0].totalTracks = 0;
    devices[0].queueSize = 0;
    devices[0].groupCoordinatorUUID = "";
    devices[0].isGroupCoordinator = true;
    devices[0].groupMemberCount = 1;

    Serial.println("========================================");
    Serial.println("[SONOS] ✓ FAST BOOT: Device loaded from NVS cache");
    Serial.printf("[SONOS]   Speaker: %s\n", cachedRoom.c_str());
    Serial.printf("[SONOS]   IP: %s\n", cachedIP.c_str());
    Serial.printf("[SONOS]   RINCON: %s\n", cachedRincon.c_str());
    Serial.println("[SONOS]   Boot time saved: ~13 seconds");
    Serial.println("[SONOS]   To scan for other devices: Settings → Speakers → Scan");
    Serial.println("========================================");

    return true;
}
