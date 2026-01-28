# Sonos Radio Station Support - Research Findings

**Issue**: https://github.com/OpenSurface/SonosESP/issues/15
**Research Date**: January 2026

## 1. How Sonos Radio Works (UPnP/SOAP API)

### GetPositionInfo vs GetMediaInfo

**GetPositionInfo** (`AVTransport` service):
- Returns information about current position in queue and time in current song
- For radio: TrackMetaData often contains URL junk or incomplete data
- Contains `r:streamContent` element with current song info (when available)
- Fields: Track, RelTime, TrackDuration, TrackMetaData, TrackURI

**GetMediaInfo** (`AVTransport` service):
- Returns information about the current playing media (queue/station)
- For radio: CurrentURIMetaData contains the actual station name
- More reliable for getting station identification
- Fields: CurrentURI, CurrentURIMetaData

**Key Insight**: Use **both** methods:
- GetMediaInfo → Station name from CurrentURIMetaData
- GetPositionInfo → Current song from r:streamContent in TrackMetaData

### Radio URI Patterns

Radio stations are identified by URI prefixes:
- `x-sonosapi-stream:` - Sonos streaming services
- `x-rincon-mp3radio:` - TuneIn radio
- `x-sonosapi-radio:` - Sonos Radio service
- `aac://` - AAC streams
- `hls-radio:` - HLS radio streams

### DIDL-Lite Metadata for Radio

When playing radio, metadata uses class: `object.item.audioItem.audioBroadcast`

Example structure:
```xml
<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/"
           xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"
           xmlns:r="urn:schemas-rinconnetworks-com:metadata-1-0/"
           xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/">
  <item id="R:0/0/0" parentID="R:0/0" restricted="true">
    <dc:title>Station Name</dc:title>
    <upnp:class>object.item.audioItem.audioBroadcast</upnp:class>
    <r:streamContent>TITLE Song Name|ARTIST Artist Name</r:streamContent>
    <upnp:albumArtURI>http://station-logo.png</upnp:albumArtURI>
  </item>
</DIDL-Lite>
```

## 2. The r:streamContent Element

**Purpose**: Contains current song information for radio streams

**Namespace**: `urn:schemas-rinconnetworks-com:metadata-1-0/`

### Format Variations by Service

**SiriusXM**:
```
BR P|TYPE=SNG|TITLE Talk To Me|ARTIST Kopecky
```

**Apple Music / RadioApp**:
```
TYPE=SNG|TITLE Green Day - Time Of Your Life|ARTIST |ALBUM
```

**TuneIn (simple format)**:
```
Soft Cell - Tainted Love
```

**Spotify/Local files**:
- Field is present but blank
- Use standard DIDL-Lite fields (dc:title, dc:creator) instead

### Parsing Strategy

1. Check if `r:streamContent` exists and has content
2. If pipe-delimited format (`TYPE=`, `TITLE `, `ARTIST `):
   - Extract values after keywords
   - Split on `|` delimiter
3. If simple "Artist - Title" format:
   - Split on ` - `
4. If empty or unavailable:
   - Fall back to dc:title and dc:creator

## 3. Radio vs Music Track Differences

| Feature | Music Track | Radio Station |
|---------|-------------|---------------|
| Duration | Fixed length | No duration (streaming) |
| Progress | Seekable | Not seekable |
| Next/Previous | Available | Not available |
| Queue | Part of queue | No queue concept |
| Metadata | Structured (dc:title, dc:creator, upnp:album) | Mixed (station in CurrentURIMetaData, song in streamContent) |
| Album Art | Track art | Station logo (may change per song) |

## 4. UI Adaptations for Radio

**Hide for Radio Mode**:
- Next/Previous track buttons
- Shuffle and Repeat controls
- Progress slider
- Time elapsed / remaining labels
- Next track info
- Queue button

**Show for Radio Mode**:
- Play/Pause
- Volume controls
- Station name (from GetMediaInfo)
- Current song (from streamContent if available)

## 5. ESP32 HTTP Client Best Practices

### The Problem: Memory Exhaustion

ESP32 WiFi has limited packet buffers. Large HTTP downloads (album art ~100KB) can exhaust available RAM and cause:
- Buffer overflow crashes
- Connection drops
- Assert failures in WiFi driver

**Symptoms**:
```
assert failed: sdio_push_data_to_queue sdio_drv.c:862 (pkt_rxbuff)
assert failed: transport_drv_sta_tx transport_drv.c:274 (copy_buff)
```

### Best Practices

1. **Chunked Reading**:
   - Default buffer size: 512 bytes
   - WiFi MTU: ~1500 bytes
   - Read in small chunks (1-4KB)
   - Process immediately (decode, write to PSRAM)

2. **Memory Management**:
   - Allocate buffers in PSRAM, not DRAM
   - Free buffers immediately after use
   - Don't accumulate data in RAM

3. **Concurrent Connections**:
   - **Critical**: ESP32 can't handle multiple simultaneous HTTP connections well
   - If doing continuous polling (SOAP), minimize overlap with large downloads
   - Consider queueing downloads instead of immediate fetch

4. **Task Delays**:
   - `vTaskDelay()` between chunks to yield to WiFi task
   - Allow other tasks (SOAP polling) to execute

### ESP32-P4 Specific Note

ESP32-P4 does **NOT** have built-in WiFi! It uses:
- Native Ethernet support (built-in)
- WiFi via external module ("esp hosted")

This explains why WiFi performance may be more limited than ESP32/ESP32-S3.

## 6. Implementation Strategy

### Phase 1: Radio Detection & Basic Metadata
1. Detect radio by URI pattern
2. Call GetMediaInfo to get station name
3. Parse CurrentURIMetaData for station info
4. Store in SonosDevice fields

### Phase 2: Current Song Info
1. Parse r:streamContent from GetPositionInfo
2. Handle multiple formats (pipe-delimited, simple)
3. Update UI with current song when available
4. Fall back to station name when song info unavailable

### Phase 3: UI Adaptation
1. Create radio mode detection function
2. Hide/show appropriate controls
3. Update labels with station/song info
4. Handle transitions between radio and music

### Phase 4: Album Art (Careful!)
1. Station logos are often PNG (not just JPEG)
2. May need to disable or queue art downloads during active SOAP polling
3. Consider: Download only when track changes, not continuously
4. Test thoroughly for WiFi stability

### Phase 5: Edge Cases
1. Unicode characters in station names (common!)
2. URL junk detection and filtering
3. Graceful degradation when metadata unavailable
4. Network error handling

## 7. Key Lessons from Other Implementations

### SoCo (Python Library)
- Uses `get_current_media_info()` for station info
- Separate from `get_current_track_info()` for track details
- Generates DIDL-Lite with `audioBroadcast` class for radio
- Handles Unicode issues explicitly

### node-sonos
- Parses r:streamContent when standard fields empty
- Falls back to standard DIDL fields when streamContent blank
- Issue #106 documents the exact SiriusXM parsing solution

### Common Pitfalls
1. **Don't rely only on GetPositionInfo** - it returns junk for many radio stations
2. **Check r:streamContent exists** - not all services populate it
3. **Handle multiple formats** - streamContent varies by service
4. **Unicode support critical** - station names frequently have special characters

## Sources

- [TravelMarx: Exploring Sonos via UPnP](https://blog.travelmarx.com/2010/06/exploring-sonos-via-upnp.html)
- [Sonos API Documentation - AVTransport](https://github.com/svrooij/sonos-api-docs/blob/main/docs/services/av-transport.md)
- [SoCo Documentation - Core Module](https://docs.python-soco.com/en/latest/api/soco.core.html)
- [node-sonos Issue #106: No artist or title for radio stream (SiriusXM)](https://github.com/bencevans/node-sonos/issues/106)
- [OpenHAB Issue #13208: currenttrack not updated for RadioApp](https://github.com/openhab/openhab-addons/issues/13208)
- [ESP32 Forum: HTTPClient read fails on large chunk](https://www.esp32.com/viewtopic.php?t=18491)
- [ESP32 Forum: How to improve http client time to download image](https://esp32.com/viewtopic.php?t=44942)
- [GitHub: fast WiFi file download](https://github.com/espressif/arduino-esp32/issues/4529)
- [ESP32 Forum: http_server multiple concurrent requests](https://esp32.com/viewtopic.php?t=25751)

## Next Steps

1. Review this research with user
2. Get user input on any additional requirements
3. Start implementation incrementally (one phase at a time)
4. Test each phase thoroughly before moving to next
5. **DO NOT GUESS** - refer back to this research when implementing
