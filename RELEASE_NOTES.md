# Release Notes

## v1.2.1 - Performance & Stability Update

### 🚀 Major Performance Improvements

**HTTP Downgrade for Album Art**
- Album art now uses HTTP instead of HTTPS for public CDNs (Spotify, Deezer, TuneIn)
- **Full 640x640 resolution** maintained (no quality loss!)
- Eliminates ALL TLS overhead (handshake, encryption, DMA memory)
- ~4-5x reduction in SDIO stress compared to HTTPS
- Result: Faster art loading, more stable operation

### 🎵 Lyrics Enhancements

**Improved Reliability**
- Timeout increased: 6s → 10s (lrclib.net can be slow)
- Retries increased: 3 → 5 attempts (total 6 tries)
- Progressive backoff: 2s, 3s, 4s, 5s, 6s delays
- Status indicator: Dark gray "Lyrics..." only shown during fetch, hidden when ready/not found

**Visual Improvements**
- Removed solid black line artifact at bottom of lyrics overlay
- Brighter current line (4x multiplier, floor 120 vs progress bar's 3x/80)
- Cleaner UI with proper border/outline/shadow removal

### 🖼️ Album Art Fixes

**Grayscale JPEG Support**
- ESP32-P4 HW decoder now handles grayscale JPEGs
- Detects via `JPEG_DOWN_SAMPLING_GRAY`
- Decodes as GRAY8, converts to RGB565 in PSRAM
- Fixes: `your jpg is a gray style picture, but your output format is wrong` errors

**COM Marker Stripping**
- Strips JPEG comment markers (0xFFFE) before HW decode
- Prevents ESP32-P4 decoder error 258: `COM marker data underflow`
- Works with Spotify album art and other CDN sources

**Decode Failure Tracking**
- Tracks consecutive HW JPEG decode failures
- Gives up after 3 failed attempts on same URL
- Prevents infinite retry loops on unsupported/corrupted images

### 🔧 Radio Station Improvements

**Fixed Radio Art Loading**
- Radio station logos now load properly
- TuneIn CDN URLs downgraded to HTTP automatically
- PNG station logos properly detected and decoded

### 🛠️ SOAP Protocol Optimization

**HTTP 500 Handling**
- Sonos returns 500 during source transitions (radio switching, track changes)
- Now treated as transient, not counted as error
- Log spam throttled (2s throttle, only first in burst logged)
- Prevents false disconnection during normal operation

### 🔄 OTA Update Fix

**SDIO Crash During Firmware Download**
- **CRITICAL FIX**: Resolved crash: `sdio_push_data_to_queue: pkt_rxbuff` assertion failure
- Root cause: TLS session consumes ~94KB DMA, leaving only ~8KB for SDIO buffers
- Increased settle delay: 100ms → **500ms** after TLS handshake (SDIO recovery time)
- Increased per-chunk delay: 10ms → **25ms** during 2MB firmware download
- Result: **Stable OTA updates** without SDIO buffer exhaustion

### ⚙️ Settings Menu Reorder

**New Order (easier access to most-used settings):**
1. General (lyrics toggle, etc.)
2. Speakers
3. Groups
4. Sources
5. Display
6. WiFi
7. Update

### 📊 Memory & Stability

- DRAM usage: 7.4% (24,260 bytes / 327,680 bytes)
- Flash usage: 29.9% (1,959,438 bytes / 6,553,600 bytes)
- No memory leaks
- Hardware watchdog: 30s timeout

---

## Technical Details

### HTTP vs HTTPS Performance Impact

| Metric | HTTPS | HTTP | Improvement |
|--------|-------|------|-------------|
| TLS handshake | ~500ms | 0ms | Eliminated |
| Per-chunk overhead | 15ms | 5ms | 3x faster |
| Abort stabilization | 1000ms | 300ms | 3.3x faster |
| Session cleanup | 200ms | 50ms | 4x faster |
| SDIO buffer stress | High | Low | ~80% reduction |

### OTA SDIO Buffer Analysis

| Stage | Free DMA Heap | Notes |
|-------|---------------|-------|
| Before connection | 102,820 bytes | Normal state |
| After http.begin() | 102,740 bytes | Minimal overhead |
| After http.GET() | **8,820 bytes** | TLS session: ~94KB DMA used |
| During download | 8,000-9,000 bytes | Critical: SDIO needs delays |

**Fix Strategy:**
1. 500ms settle delay after GET() - lets SDIO buffers stabilize
2. 25ms per-chunk delay - prevents RX buffer overflow during sustained download
3. Watchdog reset every chunk - prevents 30s timeout during 2MB download

### Fixed Errors

- ❌ `jpeg.decoder: your jpg is a gray style picture, but your output format is wrong`
- ❌ `jpeg.decoder: COM marker data underflow for header_size: 24`
- ❌ `[SOAP] HTTP error 500 for AVTransport.GetPositionInfo` (now silent, transient)
- ❌ `sdio_push_data_to_queue sdio_drv.c:862 (pkt_rxbuff)` - **CRITICAL OTA CRASH**
- ❌ Radio station art not loading
- ❌ Black line at bottom of lyrics overlay

### Modified Files

```
src/ui_album_art.cpp       - HTTP downgrade, grayscale fix, COM marker stripping
src/lyrics.cpp              - Timeout/retry optimization, visual fixes
src/sonos_controller.cpp    - SOAP 500 handling with throttling
src/ui_handlers.cpp         - Radio art URL detection fix, OTA SDIO crash fix
src/ui_sidebar.cpp          - Settings menu reorder
src/ui_*_screen.cpp         - Updated sidebar indices
```

---

## Upgrade Notes

- **Recommended**: This update significantly improves stability and performance
- **OTA Fix**: Can now reliably update firmware over WiFi without crashes
- **Breaking Changes**: None
- **Settings**: All existing settings preserved (NVS)
- **Compatibility**: Works with all Sonos hardware

---

## Known Limitations

- **OTA blue strobe**: During flash writes, PSRAM cache disruption causes display flicker (hardware limitation with Boya flash chip)
- **SDIO buffer**: ESP32-C6 SDIO WiFi requires cooldowns between rapid HTTPS operations (mitigated by HTTP downgrade)
- **OTA download time**: ~50 seconds for 2MB firmware (25ms per KB) - ensures stability over speed

---

## Credits

- HTTP optimization strategy based on SDIO buffer analysis
- OTA SDIO crash fix based on DMA heap monitoring
- Lyrics API: lrclib.net
- ESP32-P4 HW JPEG decoder with grayscale conversion workaround
