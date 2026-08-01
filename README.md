<div align="center">

# SonosESP — Touchscreen Sonos Controller for ESP32-P4

**A DIY wall-mount / desktop remote for Sonos speakers.** Album art, synced lyrics, multi-room control, weather and four screensaver clock faces — on a 4″ or 7″ touchscreen, with over-the-air updates.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Ready-blue.svg)](https://platformio.org/)
[![GitHub Downloads (all releases)](https://img.shields.io/github/downloads/OpenSurface/SonosESP/total?style=flat-square&logo=github&label=Downloads)](https://github.com/OpenSurface/SonosESP/releases)
[![GitHub Release](https://img.shields.io/github/v/release/OpenSurface/SonosESP?style=flat-square&logo=github&label=Latest%20Release)](https://github.com/OpenSurface/SonosESP/releases/latest)
[![GitHub Stars](https://img.shields.io/github/stars/OpenSurface/SonosESP?style=flat-square&logo=github&label=Stars)](https://github.com/OpenSurface/SonosESP/stargazers)

### [⚡ Install in your browser — no toolchain needed](https://opensurface.github.io/SonosESP/)

[Features](#-features) • [Themes](#-player-themes) • [Screensaver](#-screensaver-themes) • [Hardware](#-hardware) • [Install](#-installation) • [Setup](#-first-time-setup) • [Troubleshooting](docs/TROUBLESHOOTING.md) • [Contributing](#-contributing)

## ☕ Support

If you find this project helpful, consider supporting me on Ko-fi!

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-ff5e5b?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/pizzapasta)

</div>

---

![SonosESP running on a GUITION ESP32-P4 touchscreen, showing album art and playback controls](assets/image1.gif)

## ✨ Features

**Playback**
- **Full transport control** — play/pause, skip, previous, shuffle, repeat, volume and mute
- **Queue & library browsing** — walk your Sonos library, playlists and favourites, jump to any track
- **Multi-room** — switch between Sonos zones, with live indicators showing which rooms are playing
- **Speaker groups** — create and break groups straight from the panel
- **Line-in & TV audio** — dedicated screens when a soundbar is on TV input or a device is on analogue line-in

**Display**
- **Album art** — ESP32-P4 hardware JPEG decoder, plus PNG and progressive-JPEG support, bilinear scaling and automatic dominant-colour extraction
- **Synced lyrics** — time-synced from [LRCLIB](https://lrclib.net/), with auto-hide and colour matching
- **Full accent support** — titles, artists and lyrics render accented characters correctly (Beyoncé, Björk, Sigur Rós) instead of substituting plain letters
- **Three player themes** — see [below](#-player-themes)
- **Four screensaver themes** — see [below](#-screensaver-themes)
- **Weather** — current conditions plus a 6-hour forecast from [Open-Meteo](https://open-meteo.com/) (no API key)
- **Auto-dim** — configurable idle timeout and dimmed brightness level

**System**
- **Two screen sizes, one codebase** — 4″ and 7″ builds from the same source
- **OTA updates** — install new firmware from the panel, with Stable and Nightly channels
- **Browser installer** — flash over USB from Chrome/Edge/Opera, no toolchain required

## 🎨 Player Themes

Switch anytime in **Settings → General → Theme**. Adding another is a single registry entry — see [`src/ui_theme.cpp`](src/ui_theme.cpp).

| Theme | Look |
|---|---|
| **SonosESP** *(default)* | The original — blurred album art fills the screen behind the player |
| **Ambient** | Backdrop tinted from the artwork, artwork left with lyrics beneath it, pill-shaped room selector |
| **Immersive** | Full-bleed colour, compact header, and a large animated lyric stage where each line fades in |

## 🌙 Screensaver Themes

The panel falls back to a clock after an idle timeout. Switch faces in
**Settings → Clock → Theme**; each supports the optional photo background and the
weather overlay. Adding another is a single registry entry — see
[`src/clock_face.cpp`](src/clock_face.cpp).

| Theme | Look |
|---|---|
| **Horizon** *(default)* | Centred clock over an ambient glow, one-line weather summary, 6-hour forecast as pill chips |
| **Orbit** | Clock alongside a live sun-path arc that tracks the real sunrise/sunset, with the forecast drawn as a temperature curve |
| **Monolith** | Hours stacked over minutes, a details column for humidity, wind, UV and sun times, and a forecast rail |
| **StandBy** | Oversized overlapping digits tinted from the current album art |

Tap the screen at any time to return to the player.

## 🖥 Hardware

SonosESP runs on **GUITION ESP32-P4 + ESP32-C6 touchscreen boards**. Two screen sizes build from the **same codebase** — the installer and OTA pick the right firmware automatically (`firmware-4inch.bin` / `firmware-7inch.bin`).

![GUITION JC4880P433C ESP32-P4 touchscreen development board](assets/image.png)

| | **4″ — stable** | **7″ — BETA** |
|---|---|---|
| **Board** | GUITION JC4880P433C | GUITION JC1060P470C |
| **Display** | 800×480, ST7701 (MIPI DSI) | 1024×600, JD9165 (MIPI DSI) |
| **Touch** | GT911 capacitive (I²C) | GT911 capacitive (I²C) |
| **MCU** | ESP32-P4 (400 MHz dual-core RISC-V) | ESP32-P4 (400 MHz dual-core RISC-V) |
| **Wi-Fi** | ESP32-C6 (via ESP-Hosted) | ESP32-C6 (via ESP-Hosted) |
| **Flash / PSRAM** | 16 MB / 32 MB OPI | 16 MB / 32 MB OPI |
| **Interface** | USB-C | USB-C |

> **Note:** This firmware targets these specific GUITION boards and won't run on other ESP32 boards without significant changes.
>
> The **4″** is the production target. The **7″** is **BETA** — it builds from the same source and has been run on hardware, but has had far less testing. See [docs/MULTI_SCREEN_SUPPORT.md](docs/MULTI_SCREEN_SUPPORT.md).

## 📦 Installation

### Web installer (recommended)

1. Open the [**Web Installer**](https://opensurface.github.io/SonosESP/)
2. **Choose your screen** — 4″ (stable) or 7″ (BETA)
3. Connect the board over USB-C
4. Click **Install** and pick the serial port
5. Unplug/replug when it finishes, then set up Wi-Fi on screen

> Requires Chrome, Edge or Opera (desktop) — they support Web Serial. Firefox and Safari don't.

### Build from source

```bash
git clone https://github.com/OpenSurface/SonosESP.git
cd SonosESP
pio run -e esp32_4inch -t upload      # 4" board
pio run -e esp32_7inch -t upload      # 7" board
```

### OTA updates

Once installed, the panel updates itself: **Settings → Firmware Update → Check for Updates**. Pick **Stable** or **Nightly** in the channel dropdown. The device chooses the correct build for its own screen size.

Interrupted downloads **resume** rather than restarting: if the transfer stalls, the
panel reconnects and continues from the byte it reached, so a flaky connection no
longer means starting the whole image again. Seeing `Resuming from 47%…` is the
recovery working — let it run. See [Troubleshooting](docs/TROUBLESHOOTING.md#updates-fail-or-stop-partway) if it still fails.

## 🚀 First-time setup

1. **Power on** — the Wi-Fi setup screen appears if nothing is configured
2. **Wi-Fi** — tap *Scan*, pick your network, enter the password with the on-screen keyboard
3. **Find speakers** — *Settings → Speakers → Scan*
4. **Play** — select a room and you're controlling music

Wi-Fi credentials and all settings are stored in NVS and survive reboots and firmware updates.

## 🔧 Troubleshooting

Device not showing up over USB? Update stopping partway? Blank screen after an update?

**→ [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)**

Two that catch people out most often:
- **The board has two USB-C ports** and only one talks to a computer — if nothing appears on your PC, try the other port first.
- **Wi-Fi is 2.4 GHz only.** A combined 2.4/5 GHz network name is a common reason setup fails.

## 🏗 Architecture

- **UI framework** — LVGL 9.5, with resolution-relative scaling (`ui_scale.h`) so one layout serves both panels
- **FreeRTOS tasks** — separate tasks for UI, album art, lyrics, Sonos polling and the clock background
- **Thread safety** — mutex-protected shared state; all LVGL work happens on the UI thread
- **Memory** — PSRAM for artwork, lyrics and photo buffers; internal DMA SRAM carefully reserved for Wi-Fi/TLS
- **Network** — SOAP over HTTP for Sonos control, HTTPS for lyrics and weather, SSDP for discovery
- **Image pipeline** — hardware JPEG decode, software PNG and progressive-JPEG fallback, fixed-point bilinear scaling
- **Reliability** — layered SDIO crash defences serialise network access; see [`ARCHITECTURE.md`](ARCHITECTURE.md)

Full reference: [**ARCHITECTURE.md**](ARCHITECTURE.md) · Release process: [**RELEASE.md**](RELEASE.md)

## 🤝 Contributing

Contributions are welcome — please read [CONTRIBUTING.md](CONTRIBUTING.md).

Found a bug or want a feature? [Open an issue](https://github.com/OpenSurface/SonosESP/issues).

## 📸 Community builds

Real SonosESP installs in the wild — kitchens, offices, studios, dorm rooms.

**Share yours:** open a [🖼 Show off your build](https://github.com/OpenSurface/SonosESP/issues/new?template=showcase.yml) issue with a photo.

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

More builds and casual sharing in [**Show & Tell**](https://github.com/OpenSurface/SonosESP/discussions/categories/show-and-tell).

## 👥 Contributors

Thanks to everyone who has contributed to this project:

<a href="https://github.com/OpenSurface/SonosESP/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=OpenSurface/SonosESP" alt="SonosESP contributors" />
</a>

- [@BaileyLawson](https://github.com/BaileyLawson)
- [@johnhenrick3-cpu](https://github.com/johnhenrick3-cpu)
- [@freeformz](https://github.com/freeformz)

## 📄 License

MIT — see [LICENSE](LICENSE).

## 🙏 Acknowledgments

- [LVGL](https://lvgl.io/) — the embedded graphics library behind the whole UI
- [PlatformIO](https://platformio.org/) — build system and toolchain
- [LRCLIB](https://lrclib.net/) — free synced-lyrics API
- [Open-Meteo](https://open-meteo.com/) — free weather API, no key required
- [LoremFlickr](https://loremflickr.com/) — random photo backgrounds for the clock screensaver
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/) — browser-based installer
- The Sonos UPnP/SOAP community for documenting the control API

---

<div align="center">

**Built with ❤️ and vibes** • [Troubleshooting](docs/TROUBLESHOOTING.md) • [Report a bug](https://github.com/OpenSurface/SonosESP/issues) • [Request a feature](https://github.com/OpenSurface/SonosESP/issues) • [Install now](https://opensurface.github.io/SonosESP/)

<sub>Keywords: Sonos controller · ESP32-P4 touchscreen · DIY Sonos remote · smart home wall panel · LVGL · ESP32 music controller · Sonos display · album art · synced lyrics</sub>

</div>
