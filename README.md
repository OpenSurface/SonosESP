<div align="center">

# SonosESP | ESP32-P4 Sonos Controller

**A modern, touchscreen controller for Sonos speakers built with ESP32-P4**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Ready-blue.svg)](https://platformio.org/)

[Features](#features) • [Hardware](#hardware) • [Installation](#installation) • [Building](#building) • [Contributing](#contributing)

## ☕ Support

If you find this project helpful, consider buying me a coffee!

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Support-orange?style=for-the-badge&logo=buy-me-a-coffee)](https://www.buymeacoffee.com/pizzapasta)

</div>

---

##  Features

- **Full Playback Control** - Play, pause, skip, volume, shuffle, and repeat
- **Queue Management** - Browse and manage your playback queue
- **Album Art Display** - High-quality JPEG rendering with automatic dominant color extraction
- **Music Browsing** - Navigate your Sonos library, playlists, and favorites
- **Multi-Room** - Switch between multiple Sonos devices
- **WiFi Configuration** - Easy on-screen WiFi setup with built-in keyboard
- **OTA Updates** - Automatic firmware updates from GitHub releases
- **Thread-Safe** - FreeRTOS tasks with proper synchronization
- **Performance Optimized** - PSRAM allocation, efficient string handling, robust networking

##  Hardware

This project requires the **GUITION JC4880P433C** development board:

| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-P4 (400 MHz dual-core) |
| **WiFi Module** | ESP32-C6 (via ESP-Hosted) |
| **Display** | 800×480 RGB LCD with ST7701 driver |
| **Touch** | GT911 capacitive touch (I2C) |
| **Flash** | 16 MB |
| **PSRAM** | OPI PSRAM |
| **Interface** | USB-C |

> **Note:** This firmware is specifically designed for the GUITION JC4880P433C board. It will not work on other ESP32 boards without significant modifications.

## Installation

### Web Installer (Recommended)

1. Visit the [Web Installer](https://opensurface.github.io/SonosESP/)
2. Connect your ESP32-P4 via USB-C
3. Click "Install Firmware" and select the COM port
4. Wait for installation to complete
5. Configure WiFi using the on-screen keyboard after reboot

> Requires Chrome, Edge, or Opera browser with Web Serial support


## OTA Updates (After Initial Install)

The device supports automatic Over-The-Air (OTA) firmware updates from GitHub releases:

1. Connect to WiFi via Settings
2. Navigate to Settings → Firmware Update
3. Tap "Check for Updates"
4. If an update is available, tap "Install Update"
5. Device will automatically download and install from GitHub releases

##  First-Time Setup

1. **Power on** - Device will show WiFi setup if not configured
2. **WiFi Setup** - Tap "Scan" to find networks, select yours, enter password
3. **Sonos Discovery** - Navigate to Settings → Speakers and tap "Scan"
4. **Start Playing** - Select a device and start controlling your music!


### Key Components

- **FreeRTOS Tasks** - Separate tasks for UI, album art, and Sonos polling
- **Thread Safety** - Mutex protection for shared resources
- **Memory Management** - PSRAM for album art, heap monitoring
- **Network Layer** - HTTPClient for SOAP requests, UDP for discovery
- **UI Framework** - LVGL 9.4.0 with custom theme
- **OTA Updates** - Automatic firmware updates from GitHub releases

## Configuration

WiFi credentials are stored persistently in NVS (Non-Volatile Storage). Once configured via the UI, they survive reboots and power cycles.

### Firmware Updates
- Automatic OTA updates from GitHub releases
- Version checking on demand
- Progress indication during download
- Safe rollback on failure

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built with [LVGL](https://lvgl.io/) - Amazing embedded graphics library
- [PlatformIO](https://platformio.org/) - Best embedded development platform
- Sonos UPnP/SOAP API documentation and community


---

<div align="center">

**Built with ❤️ and vibes** • [Report Bug](https://github.com/OpenSurface/SonosESP/issues) • [Request Feature](https://github.com/OpenSurface/SonosESP/issues)

*This project was vibe-coded for fun!* 🎉

</div>