# Radio Station Support - Implementation Reference

This document summarizes the attempted implementation in the `feat/radio` branch that had WiFi driver crashes on ESP32-P4.

**Issue**: https://github.com/OpenSurface/SonosESP/issues/15

## What Was Attempted (feat/radio branch)

### 1. Radio Detection & Metadata
**Files**: `src/sonos_controller.cpp`

- **Radio URI Detection**: Detect radio by URI patterns
  ```cpp
  x-sonosapi-stream:, x-rincon-mp3radio:, x-sonosapi-radio:, aac://, hls-radio:
  ```

- **Added SonosDevice fields**:
  ```cpp
  bool isRadioStation;          // Radio detection flag
  String radioStationName;      // Station name from GetMediaInfo
  String radioStationArtURL;    // Station logo URL
  String streamContent;         // Current song from r:streamContent
  ```

- **Metadata Sources**:
  - `GetPositionInfo` → TrackMetaData (often URL junk for radio)
  - `GetMediaInfo` → CurrentURIMetaData (actual station name)
  - `r:streamContent` → Current playing song (parsed formats: "Artist - Title" or "TYPE=SNG|TITLE xxx|ARTIST xxx")

- **Unicode Support**: Added special space characters to `decodeHTML()` for Apple Music station names

### 2. UI Adaptation
**Files**: `src/ui_radio_mode.cpp`, `src/ui_radio_mode.h`

- Created dedicated radio UI module
- Hide controls for radio:
  - Next/Previous buttons (no queue)
  - Shuffle/Repeat (not applicable)
  - Progress slider (no duration)
  - Time labels (no track length)
  - Next track info (no queue)

### 3. Album Art Handling
**Files**: `src/ui_album_art.cpp`, `src/ui_handlers.cpp`

- **PNG Support**: Added `PNGdec` library for station logos (many are PNG format)
- **Art Fallback Logic**: Song art → Station logo → Placeholder
- **Radio Art Throttling**: Max once per 5 seconds for radio stations
- **Clear on Switch**: Clear display when switching to track with no art

### 4. Download Abort Logic (FAILED - Caused Crashes)
**Problem**: Rapid source switching caused WiFi driver crashes

**What We Tried**:
- Download-in-progress flag
- Current download URL tracking
- Abort detection during chunk reads
- Stream draining on abort
- Various delays (1ms, 3ms, 5ms, 20ms)
- Chunk size reduction (4KB → 1KB)
- Startup delays (2s, 5s, 10s)
- SOAP polling throttling

**Result**: All approaches failed - ESP32-P4 WiFi driver crashes with:
```
assert failed: sdio_push_data_to_queue sdio_drv.c:862 (pkt_rxbuff)
assert failed: transport_drv_sta_tx transport_drv.c:274 (copy_buff)
```

## Why It Failed

The ESP32-P4 WiFi driver has **limited packet buffers**. When album art downloads (large HTTP GET) overlap with SOAP polling requests (continuous every 500ms-1s), the driver buffers fill up and assert.

**Key Insight**: The abort logic doesn't help because the crash happens on the FIRST download, not during rapid switching. The issue is concurrent HTTP connections, not the switching itself.

## What Worked

- ✅ Radio detection by URI
- ✅ Station name from GetMediaInfo
- ✅ streamContent parsing
- ✅ UI adaptation (hiding controls)
- ✅ PNG logo support
- ✅ Unicode character handling

## What Didn't Work

- ❌ Download abort logic (didn't prevent crashes)
- ❌ Network throttling (too slow, still crashed)
- ❌ Startup delays (didn't help)
- ❌ Chunk size reduction (too slow, still crashed)

## Lessons Learned

1. **ESP32-P4 WiFi driver limitation** is fundamental - can't be worked around with delays/throttling
2. **Need different approach**: Either disable album art for radio, or find a way to serialize all HTTP requests
3. **The abort logic was unnecessary**: Original issue was just crashes, not abort functionality
4. **Don't add complexity without understanding root cause**: We added abort tracking, flags, delays without fixing the real problem

## Next Steps for feat/sonos-radio

1. Keep the good parts: radio detection, metadata parsing, UI adaptation, PNG support
2. **Simple approach for art**: Accept that art downloads might fail during heavy network use - don't try to be too clever
3. **Test incrementally**: Add one feature at a time and test thoroughly before adding more
4. Consider: Disable album art downloads during active SOAP polling periods
5. Consider: Queue art downloads instead of immediate fetch
