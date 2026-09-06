/**
 * Sonos Device Discovery - SSDP/UPnP Implementation
 * Handles multicast/broadcast discovery and device metadata fetching
 */

#include "sonos_controller.h"
#include "ui_common.h"
#include <esp_task_wdt.h>

// ============================================================================
// Discovery
// ============================================================================
int SonosController::discoverDevices() {
    if (!devices) { Serial.println("[SONOS] devices array not allocated"); return 0; }
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
                        // Identity MUST be cleared with the rest of the slot. deviceCount
                        // was reset to 0 at the top of this scan, so slot 0 is very often
                        // the fast-boot cached speaker — and getRoomName() below leaves
                        // rinconID untouched when the description fetch fails. Without this
                        // line an unidentified responder inherits the previous occupant's
                        // RINCON, two entries then claim the same id, and the Groups screen
                        // renders a phantom speaker named by its bare IP.
                        devices[deviceCount].rinconID = "";
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
        esp_task_wdt_reset();  // Each getRoomName() takes ~1-2s; 17 devices × 2s = 34s > 30s WDT
        getRoomName(&devices[i]);
        Serial.printf("[SONOS]   -> Room name: '%s'\n", devices[i].roomName.c_str());

        // Update UI while fetching room names
        lv_tick_inc(10);
        lv_refr_now(NULL);
    }

    // Drop anything that answered SSDP but never resolved into a real speaker.
    //
    // udp.begin(1900) binds the SSDP multicast port, so this socket also receives
    // unsolicited NOTIFY/ssdp:alive traffic from the whole LAN, not just replies to our
    // M-SEARCH. Anything whose payload merely contains "sonos" or "zoneplayer" is accepted
    // above. A real ZonePlayer answers /xml/device_description.xml with a room name and a
    // UDN; something that answers neither is not a speaker we can drive — every group,
    // queue and favourite URI is built from the RINCON id.
    //
    // Conservative on purpose: a device is only dropped when it has NO RINCON *and*
    // getRoomName() never replaced the placeholder name (roomName is still the raw IP).
    // A speaker that names itself but loses its UDN to a transient failure is kept.
    {
        int kept = 0;
        for (int i = 0; i < deviceCount; i++) {
            bool identified = devices[i].rinconID.length() > 0 ||
                              devices[i].roomName != devices[i].ip.toString();
            if (identified) {
                if (i != kept) devices[kept] = devices[i];
                kept++;
            } else {
                Serial.printf("[SONOS]   [DROPPED] %s answered SSDP but has no room name or "
                              "RINCON — not a controllable ZonePlayer\n",
                              devices[i].ip.toString().c_str());
            }
        }
        if (kept != deviceCount) {
            Serial.printf("[SONOS] Dropped %d unidentified responder(s)\n", deviceCount - kept);
            deviceCount = kept;
        }
        if (deviceCount == 0) {
            Serial.println("[SONOS] No identifiable Sonos devices after filtering");
            return 0;
        }
    }

    // Fetch zone group topology to identify coordinator IPs for stereo pairs / bonded zones.
    // Both speakers in a stereo pair respond to SSDP with the same room name, but only the
    // coordinator IP accepts commands. SSDP response order is non-deterministic, so we must
    // explicitly identify the coordinator rather than just keeping the first-seen IP.
    const int MAX_COORDINATORS = 16;
    String coordinatorRINCONs[MAX_COORDINATORS];
    int coordCount = 0;
    if (deviceCount > 0) {
        esp_task_wdt_reset();  // getRoomName loop may have consumed most of the WDT window
        Serial.printf("[SONOS] Fetching zone topology from %s...\n", devices[0].ip.toString().c_str());
        coordCount = fetchTopologyCoordinators(devices[0].ip, coordinatorRINCONs, MAX_COORDINATORS);
        if (coordCount == 0) {
            Serial.printf("[SONOS] Topology unavailable - falling back to first-seen deduplication\n");
        }
    }

    // Helper: returns true if dev's RINCON ID appears in the coordinator list
    auto isCoordinator = [&](const SonosDevice& dev) -> bool {
        if (dev.rinconID.length() == 0 || coordCount == 0) return false;
        String rinconLower = dev.rinconID;
        rinconLower.toLowerCase();
        for (int k = 0; k < coordCount; k++) {
            if (coordinatorRINCONs[k] == rinconLower) return true;
        }
        return false;
    };

    // Coordinator-aware deduplication by room name.
    // When two IPs share the same room name (stereo pair / bonded zone), keep the coordinator.
    Serial.printf("[SONOS] Starting coordinator-aware deduplication...\n");
    int uniqueCount = 0;
    for (int i = 0; i < deviceCount; i++) {
        String normalizedCurrent = devices[i].roomName;
        normalizedCurrent.trim();
        normalizedCurrent.toLowerCase();
        bool iIsCoord = isCoordinator(devices[i]);

        bool isDuplicate = false;
        int replaceIdx = -1;  // index in unique list to swap (if i is coordinator and j is not)

        for (int j = 0; j < uniqueCount; j++) {
            String normalizedExisting = devices[j].roomName;
            normalizedExisting.trim();
            normalizedExisting.toLowerCase();

            // Same room name OR same RINCON. Name alone was not enough: an entry that
            // failed its description fetch keeps the placeholder IP as its name, so it
            // never collided with the real speaker it was duplicating.
            bool sameName = (normalizedExisting == normalizedCurrent);
            bool sameUuid = devices[i].rinconID.length() > 0 &&
                            SonosController::uuidEquals(devices[j].rinconID, devices[i].rinconID);

            if (sameName || sameUuid) {
                if (sameUuid && !sameName) {
                    Serial.printf("[SONOS]   [SAME RINCON] '%s' (%s) and '%s' (%s) share %s\n",
                        devices[i].roomName.c_str(), devices[i].ip.toString().c_str(),
                        devices[j].roomName.c_str(), devices[j].ip.toString().c_str(),
                        devices[i].rinconID.c_str());
                }
                isDuplicate = true;
                if (iIsCoord && !isCoordinator(devices[j])) {
                    // Current (i) is the coordinator; existing (j) is the slave — swap
                    replaceIdx = j;
                    Serial.printf("[SONOS]   [COORD SWAP] '%s': slave %s → coordinator %s\n",
                        devices[i].roomName.c_str(),
                        devices[j].ip.toString().c_str(),
                        devices[i].ip.toString().c_str());
                } else {
                    Serial.printf("[SONOS]   [DUPLICATE] '%s' (%s) filtered (keeping %s%s)\n",
                        devices[i].roomName.c_str(), devices[i].ip.toString().c_str(),
                        devices[j].ip.toString().c_str(),
                        isCoordinator(devices[j]) ? " [coord]" : "");
                }
                break;
            }
        }

        if (!isDuplicate) {
            Serial.printf("[SONOS]   [UNIQUE] '%s' (%s)%s\n",
                devices[i].roomName.c_str(), devices[i].ip.toString().c_str(),
                iIsCoord ? " [coord]" : "");
            if (i != uniqueCount) {
                devices[uniqueCount] = devices[i];
            }
            uniqueCount++;
        } else if (replaceIdx >= 0) {
            devices[replaceIdx] = devices[i];  // Replace slave with coordinator
        }
    }

    int filteredCount = deviceCount - uniqueCount;
    if (filteredCount > 0) {
        Serial.printf("[SONOS] Filtered %d duplicate(s) from stereo pairs / bonded zones\n", filteredCount);
    } else {
        Serial.printf("[SONOS] No duplicates found - all %d devices are unique\n", uniqueCount);
    }
    deviceCount = uniqueCount;

    // Fetch playing state for each unique zone so the devices screen shows which rooms are active
    Serial.printf("[SONOS] Fetching playing state for %d zone(s)...\n", deviceCount);
    for (int i = 0; i < deviceCount; i++) {
        // Same reason as the room-name loop above: fetchDevicePlayingState() blocks
        // for up to its 3s HTTP timeout, and MAX_SONOS_DEVICES is 32 — 11 slow zones
        // already exceeds the 30s WDT, and this runs on the WDT-subscribed mainAppTask.
        esp_task_wdt_reset();
        devices[i].isPlaying = fetchDevicePlayingState(&devices[i]);
        Serial.printf("[SONOS]   '%s' (%s): %s\n",
            devices[i].roomName.c_str(), devices[i].ip.toString().c_str(),
            devices[i].isPlaying ? "PLAYING" : "stopped");
        // Keep UI alive during the SOAP calls
        lv_tick_inc(10);
        lv_refr_now(NULL);
    }

    if (deviceCount > 0) {
        prefs.putString("device_ip", devices[0].ip.toString());
    }

    Serial.printf("[SONOS] Discovery complete: %d visible zone(s)\n", deviceCount);
    return deviceCount;
}

// Fetch zone group topology via GetZoneGroupState SOAP and extract coordinator RINCON IDs.
// Returns the number of coordinators found (0 on failure).
// Coordinators are stored lowercase for case-insensitive comparison.
//
// NOTE: /status/topology was deprecated and no longer returns coordinator info on modern
// Sonos firmware. GetZoneGroupState (ZoneGroupTopology service) is the correct API.
// The ZoneGroupState inner XML may be HTML-entity-encoded inside the SOAP response.
int SonosController::fetchTopologyCoordinators(IPAddress ip, String* coordinatorRINCONs, int maxCount) {
    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400/ZoneGroupTopology/Control", ip.toString().c_str());

    static const char soapBody[] =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:GetZoneGroupState xmlns:u=\"urn:schemas-upnp-org:service:ZoneGroupTopology:1\">"
        "</u:GetZoneGroupState>"
        "</s:Body></s:Envelope>";

    http.begin(url);
    http.setTimeout(3000);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:ZoneGroupTopology:1#GetZoneGroupState\"");

    esp_task_wdt_reset();  // belt-and-suspenders: http.setTimeout(3000) bounds this call
                           // but WDT reset ensures no 30s window overrun even if timeout
                           // fires at the edge of the window (e.g. 17 × 2s = 34s loop)
    int count = 0;
    int code = http.POST((uint8_t*)soapBody, strlen(soapBody));
    if (code == 200) {
        String xml = http.getString();
        // The ZoneGroupState inner XML is typically HTML-entity-encoded inside the SOAP response
        // (e.g. &lt;ZoneGroups&gt; ... &lt;/ZoneGroups&gt;).  Decode then lowercase to handle
        // both "Coordinator=" and any case variation uniformly.
        xml.replace("&lt;", "<");
        xml.replace("&gt;", ">");
        xml.replace("&quot;", "\"");
        xml.replace("&amp;", "&");  // must be last to avoid double-decoding
        xml.toLowerCase();

        // Parse all Coordinator="RINCON_..." attributes from ZoneGroup elements.
        // After decoding + lowercase, they appear as: coordinator="rincon_..."
        int searchPos = 0;
        while (count < maxCount) {
            int pos = xml.indexOf("coordinator=\"", searchPos);
            if (pos < 0) break;
            pos += 13;  // skip past coordinator="
            int end = xml.indexOf("\"", pos);
            if (end <= pos) break;
            String rincon = xml.substring(pos, end);
            if (rincon.length() > 0) {
                coordinatorRINCONs[count++] = rincon;
                Serial.printf("[SONOS]   Topology coordinator: %s\n", rincon.c_str());
            }
            searchPos = end + 1;
        }
        Serial.printf("[SONOS] Topology: %d coordinator(s) found\n", count);
    } else {
        Serial.printf("[SONOS] GetZoneGroupState failed (HTTP %d)\n", code);
    }
    http.end();
    return count;
}

// Send a GetTransportInfo SOAP request directly to a device's IP and return true if playing.
// Used during discovery to populate isPlaying for all zones (not just the selected one).
bool SonosController::fetchDevicePlayingState(SonosDevice* dev) {
    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "http://%s:1400/MediaRenderer/AVTransport/Control",
             dev->ip.toString().c_str());

    static const char soapBody[] =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:GetTransportInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID></u:GetTransportInfo>"
        "</s:Body></s:Envelope>";

    http.begin(url);
    http.setTimeout(3000);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo\"");

    bool playing = false;
    int code = http.POST((uint8_t*)soapBody, strlen(soapBody));
    if (code == 200) {
        String resp = http.getString();
        playing = (resp.indexOf("PLAYING") >= 0);
    }
    http.end();
    return playing;
}

bool SonosController::getRoomName(SonosDevice* dev) {
    // Whether the description was actually fetched AND parsed. Returned so a
    // caller that persists hasLineIn can tell a real "no line-in" from a failed
    // request; see the cache write in tryLoadCachedDevice().
    bool parsed = false;
    dev->hasLineIn = false;   // default off: a failed fetch must not offer a dead row
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
            // Decode XML entities (&apos;) and UTF-8 smart punctuation (e.g. U+2019
            // curly apostrophe in "Raquel's Office") so they don't render as tofu in
            // the ASCII-only Montserrat label font.
            dev->roomName = decodeHTML(xml.substring(start + 10, end));
            Serial.printf("[SONOS]   Room name fetched successfully: '%s'\n", dev->roomName.c_str());

            // Line-in is not browsable content — ObjectID "AI:" returns nothing on
            // S2 — so the only way to know a player has a physical input is whether
            // it advertises the AudioIn service here. Five/Amp/Port/Beam do; One
            // and SL do not, and offering them a Line-In source would be a dead row.
            dev->hasLineIn = (xml.indexOf("AudioIn") >= 0);
            Serial.printf("[SONOS]   Line-in: %s\n", dev->hasLineIn ? "yes" : "no");
            parsed = true;
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
    return parsed;
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
    // Line-in capability is discovered from device_description.xml and is NOT
    // re-derivable on the fast-boot path, so it has to be cached with the rest
    // or the Sources screen loses its Line-In row on every reboot.
    prefs.putInt("cached_linein", dev->hasLineIn ? 1 : 0);

    Serial.printf("[SONOS] Cached device: %s (%s) [%s]\n",
                  dev->roomName.c_str(),
                  dev->ip.toString().c_str(),
                  dev->rinconID.c_str());
}

bool SonosController::tryLoadCachedDevice() {
    if (!devices) { Serial.println("[SONOS] devices array not allocated"); return false; }
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
    http.setTimeout(5000);  // 5s timeout - speaker may take a few seconds to respond after WiFi join

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

    // Device is reachable - add it to the device list.
    // decodeHTML the cached name so legacy NVS values written before the discovery-side
    // decode (with raw U+2019 / &apos; etc.) render cleanly on first boot after upgrade.
    deviceCount = 1;
    devices[0].ip = ip;
    devices[0].roomName = decodeHTML(cachedRoom);
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

    // -1 = cached by a build that did not store this yet. Resolve it once from
    // the device description (the device was just confirmed reachable above), and
    // cacheSelectedDevice() will persist it from then on.
    int cachedLineIn = prefs.getInt("cached_linein", -1);
    if (cachedLineIn < 0) {
        Serial.println("[SONOS]   Line-in unknown in cache - resolving once");
        // Persist ONLY on a successful fetch. getRoomName() defaults the flag to
        // false and only raises it on HTTP 200, so writing unconditionally would
        // cache one timed-out request as "no line-in" permanently — and the
        // sentinel below means it would never be retried. Leaving the key unset
        // costs one GET on the next boot instead.
        if (getRoomName(&devices[0]))
            prefs.putInt("cached_linein", devices[0].hasLineIn ? 1 : 0);
        else
            Serial.println("[SONOS]   Line-in fetch failed - not caching, will retry");
    } else {
        devices[0].hasLineIn = (cachedLineIn == 1);
    }

    Serial.println("========================================");
    Serial.println("[SONOS] ✓ FAST BOOT: Device loaded from NVS cache");
    Serial.printf("[SONOS]   Speaker: %s\n", cachedRoom.c_str());
    Serial.printf("[SONOS]   IP: %s\n", cachedIP.c_str());
    Serial.printf("[SONOS]   RINCON: %s\n", cachedRincon.c_str());
    Serial.printf("[SONOS]   Line-in: %s\n", devices[0].hasLineIn ? "yes" : "no");
    Serial.println("[SONOS]   Boot time saved: ~13 seconds");
    Serial.println("[SONOS]   To scan for other devices: Settings → Speakers → Scan");
    Serial.println("========================================");

    return true;
}
