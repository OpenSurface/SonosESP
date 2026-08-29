# Contributing to SonosESP

First off, thank you for considering contributing to SonosESP! It's people like you that make this project better.

## How Can I Contribute?

### Reporting Bugs

Before creating bug reports, please check existing issues to avoid duplicates. When you create a bug report, include as many details as possible:

**Bug Report Template:**
- **Device**: GUITION JC4880P433C (ESP32-P4 + ESP32-C6) or your specific board
- **Firmware Version**: (e.g., v1.2.1)
- **Description**: Clear description of the issue
- **Steps to Reproduce**: Numbered list of steps
- **Expected Behavior**: What you expected to happen
- **Actual Behavior**: What actually happened
- **Serial Logs**: Include relevant serial output (use code blocks)
- **Screenshots**: If applicable

### Suggesting Enhancements

Enhancement suggestions are tracked as GitHub issues. When creating an enhancement suggestion, include:

- **Use Case**: Why is this enhancement useful?
- **Proposed Solution**: How would you implement it?
- **Alternatives**: What other approaches have you considered?
- **Additional Context**: Screenshots, mockups, etc.

### Pull Requests

1. **Fork** the repository
2. **Create a branch** from `main` (e.g., `feature/add-spotify-connect` or `fix/ota-crash`)
3. **Make your changes** following the coding guidelines below
4. **Test thoroughly** on actual hardware (not just compilation)
5. **Commit** with clear, descriptive messages
6. **Push** to your fork
7. **Open a Pull Request** with:
   - Clear title and description
   - Reference any related issues (e.g., "Fixes #123")
   - List of changes made
   - Test results (serial logs, screenshots)

## Development Guidelines

### Hardware Requirements

- **Board**: GUITION JC4880P433C (ESP32-P4 + ESP32-C6 via SDIO)
- **Display**: ST7701 MIPI DSI (480x800 portrait, rendered 800x480 landscape)
- **PSRAM**: 32MB OPI at 200MHz
- **Sonos System**: For testing

### Build System

- **Platform**: `pioarduino` (NOT official espressif32)
- **Framework**: Arduino
- **IDE**: VSCode with PlatformIO extension

### Code Style

**General:**
- Use 4 spaces for indentation (NOT tabs)
- Keep lines under 120 characters where possible
- Use descriptive variable names (e.g., `album_art_url` not `url`)

**Comments:**
- Add comments for complex logic
- Use `// CRITICAL:` for critical sections (SDIO, mutex, memory)
- Document hardware limitations and workarounds

**Memory Management:**
- Always check `malloc`/`heap_caps_malloc` return values
- Free resources in reverse order of allocation
- Use PSRAM for large buffers (>4KB)
- Monitor DMA heap usage for SDIO operations

**SDIO-Specific (CRITICAL):**
```cpp
// GOOD: HTTP with proper cleanup
if (xSemaphoreTake(network_mutex, pdMS_TO_TICKS(5000))) {
    // Wait for cooldowns...

    HTTPClient http;
    http.begin(client, url);
    int code = http.GET();

    // Process response...

    // CRITICAL: cleanup INSIDE mutex, BEFORE xSemaphoreGive
    http.end();
    if (use_https) secure_client.stop();
    vTaskDelay(pdMS_TO_TICKS(use_https ? 200 : 50));

    xSemaphoreGive(network_mutex);
}

// BAD: cleanup after releasing mutex (causes SDIO crashes)
xSemaphoreGive(network_mutex);
http.end();  // ❌ WRONG - overlaps with next network op
```

**Mutex Usage:**
```cpp
// GOOD: Always check return value
if (xSemaphoreTake(my_mutex, pdMS_TO_TICKS(1000))) {
    // Critical section
    xSemaphoreGive(my_mutex);
} else {
    // Handle timeout
}

// BAD: No timeout, can deadlock
xSemaphoreTake(my_mutex, portMAX_DELAY);  // ❌ WRONG
```

**LVGL UI Updates:**
```cpp
// GOOD: UI updates in main thread
void updateUI() {
    if (lbl_status) {
        lv_label_set_text(lbl_status, "Updated");
    }
}

// BAD: LVGL calls from background thread
void backgroundTask(void* param) {
    lv_label_set_text(lbl_status, "Updated");  // ❌ WRONG - crashes
}
```

### Testing Requirements

Before submitting a PR, test on **actual hardware**:

1. **Basic Functionality**
   - Device discovery
   - Playback control (play/pause/skip)
   - Volume control
   - Album art loading (Spotify, Deezer, TuneIn, local library)

2. **Edge Cases**
   - Source switching (music → radio → TV)
   - WiFi reconnection
   - Rapid track skipping
   - Long playback sessions (>1 hour)

3. **Memory & Stability**
   - Monitor serial output for crashes
   - Check for memory leaks (watch free heap)
   - Test OTA updates if you modified network code

4. **Logs**
   - Include serial logs showing your changes work
   - Include error cases if applicable

### Known Limitations to Consider

When developing, be aware of these hardware constraints:

- **SDIO Buffer Overflow**: ESP32-C6 SDIO WiFi crashes with rapid HTTPS. Use HTTP for public CDNs, add delays between network ops
- **OTA Blue Strobe**: PSRAM cache disruption during flash writes (unfixable with Boya flash)
- **Grayscale JPEG**: ESP32-P4 HW decoder can't output RGB565 for grayscale - must convert via GRAY8 format
- **JPEG COM Markers**: ESP32-P4 decoder chokes on comment markers - strip them before decode

See `memory/MEMORY.md` for detailed workarounds.

## Project Structure

```
SonosESP/
├── src/
│   ├── main.cpp                 # Entry point
│   ├── sonos_controller.cpp     # Sonos SOAP API
│   ├── ui_*.cpp                 # LVGL UI screens
│   ├── ui_album_art.cpp         # Album art download & decode
│   ├── lyrics.cpp               # Synced lyrics from lrclib.net
│   └── ...
├── include/
│   ├── config.h                 # Build configuration
│   └── ui_common.h              # Shared UI declarations
├── memory/                      # Project notes
├── platformio.ini               # Build configuration
└── README.md
```

## Git Commit Messages

- Use present tense ("Add feature" not "Added feature")
- Use imperative mood ("Move cursor to..." not "Moves cursor to...")
- Limit first line to 72 characters
- Reference issues and pull requests after the first line

**Examples:**
```
Fix SDIO crash during OTA downloads

Increased TLS settle delay from 100ms to 500ms and per-chunk
delay from 10ms to 25ms. This prevents SDIO RX buffer exhaustion
during 2MB firmware downloads.

Fixes #123
```

```
Add HTTP downgrade for Spotify album art

Uses HTTP instead of HTTPS for public CDNs (Spotify, Deezer, TuneIn).
Eliminates TLS overhead and reduces SDIO stress by ~80%.

Related to #45
```

## Questions?

Feel free to:
- Open a **Discussion** for general questions
- Open an **Issue** for bug reports or feature requests
- Check existing issues/discussions before posting

## License

By contributing, you agree that your contributions will be licensed under the same license as the project (see LICENSE file).

---

Thank you for your contributions! 🎵
