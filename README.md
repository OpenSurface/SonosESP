<div align="center">

# SonosESP | ESP32-P4 Sonos Controller

**A modern, touchscreen controller for Sonos speakers built with ESP32-P4**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Ready-blue.svg)](https://platformio.org/)
[![GitHub Downloads (all releases)](https://img.shields.io/github/downloads/OpenSurface/SonosESP/total?style=flat-square&logo=github&label=Downloads)](https://github.com/OpenSurface/SonosESP/releases)
[![GitHub Release](https://img.shields.io/github/v/release/OpenSurface/SonosESP?style=flat-square&logo=github&label=Latest%20Release)](https://github.com/OpenSurface/SonosESP/releases/latest)
[![GitHub Stars](https://img.shields.io/github/stars/OpenSurface/SonosESP?style=flat-square&logo=github&label=Stars)](https://github.com/OpenSurface/SonosESP/stargazers)

[Features](#features) • [Hardware](#hardware) • [Installation](#installation) •  [Contributing](#contributing)

## ☕ Support

If you find this project helpful, consider supporting me on Ko-fi!

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-ff5e5b?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/pizzapasta)

</div>

---

##  Features

- **Full Playback Control** - Play, pause, skip, volume, shuffle, and repeat
- **Queue Management** - Browse and manage your playback queue
- **Album Art Display** - Hardware JPEG decoder + PNG support with bilinear scaling and automatic dominant color extraction
- **Synced Lyrics Display** - Time-synced lyrics from LRCLIB overlaid on album art with smart auto-hide, scroll effects, and color matching
- **Clock Screensaver** - Full-screen clock activates after inactivity with random ambient background images, tap to dismiss
- **Music Browsing** - Navigate your Sonos library, playlists, and favorites
- **Multi-Room** - Switch between Sonos zones with live playing indicators showing which rooms are active
- **OTA Updates** - Firmware updates from GitHub with Stable and Nightly release channel selection, auto-retry on low memory

![SonosESP Demo](assets/image1.gif)

##  Hardware

SonosESP runs on **GUITION ESP32-P4 + ESP32-C6 touchscreen boards**. Two screen
sizes build from the **same codebase** — the web installer and OTA pick the right
firmware automatically (`firmware-4inch.bin` / `firmware-7inch.bin`).

![GUITION JC4880P433C](assets/image.png)

| | **4″ — stable** | **7″ — BETA** |
|---|---|---|
| **Board** | GUITION JC4880P433C | GUITION JC1060P470C |
| **Display** | 800×480, ST7701 (MIPI DSI) | 1024×600, JD9165 (MIPI DSI) |
| **Touch** | GT911 capacitive (I²C) | GT911 capacitive (I²C) |
| **MCU** | ESP32-P4 (400 MHz dual-core) | ESP32-P4 (400 MHz dual-core) |
| **WiFi** | ESP32-C6 (via ESP-Hosted) | ESP32-C6 (via ESP-Hosted) |
| **Flash / PSRAM** | 16 MB / 32 MB OPI | 16 MB / 32 MB OPI |
| **Interface** | USB-C | USB-C |

> **Note:** This firmware targets these specific GUITION boards and won't run on
> other ESP32 boards without significant changes.
>
> The **4″** is the production target (thousands of installs in the wild). The
> **7″** is **BETA** — code-complete and building from the same source, but not
> yet validated on physical hardware. Flash it only if you own the board and can
> report back. See [docs/MULTI_SCREEN_SUPPORT.md](docs/MULTI_SCREEN_SUPPORT.md).

## Installation

### Web Installer (Recommended)

1. Visit the [Web Installer](https://opensurface.github.io/SonosESP/)
2. **Choose your screen** — 4″ (stable) or 7″ (BETA)
3. Connect your ESP32-P4 board via USB-C
4. Click "Install Firmware" and select the COM port
5. Wait for installation to complete
6. Configure WiFi using the on-screen keyboard after reboot

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

- **FreeRTOS Tasks** - Separate tasks for UI, album art, lyrics, and Sonos polling
- **Thread Safety** - Mutex protection for shared resources
- **Memory Management** - PSRAM for album art and lyrics, heap monitoring
- **Network Layer** - HTTPClient for SOAP requests, HTTPS for lyrics/art, UDP for SSDP discovery
- **UI Framework** - LVGL 9.5 with custom theme; resolution-relative scaling (`ui_scale.h`) so one layout fits both the 4″ and 7″ panels
- **Image Processing** - ESP32-P4 hardware JPEG decoder + software PNG decoder, custom bilinear scaling with fixed-point math
- **Lyrics System** - Time-synced LRC parsing with HTTPS fetching, auto-hide, and retry logic
- **Clock Screensaver** - Inactivity-triggered fullscreen clock with random Unsplash backgrounds
- **OTA Updates** - Stable and Nightly channels, 3-attempt retry loop with live countdown UI

## Configuration

WiFi credentials are stored persistently in NVS (Non-Volatile Storage). Once configured via the UI, they survive reboots and power cycles.

### Firmware Updates
- Automatic OTA updates from GitHub releases
- Version checking on demand
- Progress indication during download
- Safe rollback on failure


## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Community Builds

Real SonosESP installs in the wild — kitchens, offices, studios, dorm rooms.

**How to share yours:** open a [🖼 Show off your build](https://github.com/OpenSurface/SonosESP/issues/new?template=showcase.yml) issue with a photo. Selected builds get featured in the mosaic below.

<!-- showcase-start -->

<table>
  <tr>
    <td align="center" width="50%">
      <a href="https://github.com/OpenSurface/SonosESP/issues/97"><img src="https://github.com/user-attachments/assets/26036636-6b42-44ce-bb5c-498b7dfd2936" width="380" alt="4-inch SonosESP with a Brennan B3 jukebox"/></a><br>
      <sub><b>Living room · Brennan B3 Jukebox</b><br>Sonos Beam 2 + 2 Symfonisk frames · <a href="https://github.com/johnhenrick3-cpu">@johnhenrick3-cpu</a></sub>
    </td>
    <td align="center" width="50%">
      <a href="https://github.com/OpenSurface/SonosESP/issues/96"><img src="https://github.com/user-attachments/assets/ec17fe9a-abac-4a81-bd77-8aa3f73e2f0c" width="380" alt="4-inch SonosESP with a Brennan B2 jukebox"/></a><br>
      <sub><b>Living room · Brennan B2 Jukebox</b><br>Sonos Era 300 · <a href="https://github.com/johnhenrick3-cpu">@johnhenrick3-cpu</a></sub>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <a href="https://github.com/OpenSurface/SonosESP/issues/95"><img src="https://github.com/user-attachments/assets/d0f0d7e4-73a2-46f6-aeab-50cc429ea9e4" width="380" alt="7-inch SonosESP variant on a kitchen table"/></a><br>
      <sub><b>Kitchen table · 7&quot; variant <em>(beta)</em></b><br>Sonos Move 2 · <a href="https://github.com/johnhenrick3-cpu">@johnhenrick3-cpu</a></sub>
    </td>
    <td align="center" width="50%">
      <a href="https://github.com/OpenSurface/SonosESP/issues/94"><img src="https://github.com/user-attachments/assets/b7f5e037-3ea3-4802-8b7d-62adacd9078d" width="380" alt="4-inch SonosESP on a bedside table"/></a><br>
      <sub><b>Bedside table</b><br>Sonos Ray + 2 Symfonisk lamps · <a href="https://github.com/johnhenrick3-cpu">@johnhenrick3-cpu</a></sub>
    </td>
  </tr>
  <tr>
    <td align="center" colspan="2"><em>Want yours featured?</em> <a href="https://github.com/OpenSurface/SonosESP/issues/new?template=showcase.yml">Share a photo →</a></td>
  </tr>
</table>

<!-- showcase-end -->

More builds and casual sharing in [**Show & Tell**](https://github.com/OpenSurface/SonosESP/discussions/categories/show-and-tell) (Discussions tab).

## Contributors

Thanks to these wonderful people who have contributed to this project:

<a href="https://github.com/OpenSurface/SonosESP/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=OpenSurface/SonosESP" />
</a>

- [@BaileyLawson](https://github.com/BaileyLawson)
- [@johnhenrick3-cpu](https://github.com/johnhenrick3-cpu)
- [@freeformz](https://github.com/freeformz)


## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built with [LVGL](https://lvgl.io/) - Amazing embedded graphics library
- [PlatformIO](https://platformio.org/) - Best embedded development platform
- [LRCLIB](https://lrclib.net/) - Free synced lyrics API
- [Unsplash](https://unsplash.com/) - Beautiful random background photos for the clock screensaver
- Sonos UPnP/SOAP API documentation and community


---

<div align="center">

**Built with ❤️ and vibes** • [Report Bug](https://github.com/OpenSurface/SonosESP/issues) • [Request Feature](https://github.com/OpenSurface/SonosESP/issues)

</div>