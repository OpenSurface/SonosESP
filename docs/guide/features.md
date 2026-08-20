# Everything it does

## Playback

- **Full transport control** — play and pause, skip, previous, shuffle, repeat, volume and mute
- **Multi-room** — switch between Sonos zones, with live indicators showing which rooms are playing
- **Speaker groups** — create and break groups from the panel
- **Complete library browsing** — every source your speaker exposes, not a fixed list. See [Music sources](/guide/sources)
- **Favourites from any service** — anything saved in the Sonos app plays from the panel, Spotify and YouTube Music included, with no login on the device
- **Line-in and TV audio** — dedicated screens when a soundbar is on TV input or something is plugged into the analogue input

## On screen

- **Album art** — decoded by the ESP32-P4's hardware JPEG unit, with PNG and progressive-JPEG support, and the dominant colour pulled out to tint the rest of the screen
- **Synced lyrics** — time-synced from [LRCLIB](https://lrclib.net/), following the track, hiding themselves when there are none, and colour-matched to the artwork
- **Accented characters everywhere** — titles, artists, lyrics, menus, dropdowns and the on-screen keyboard all render Latin-1 and Latin Extended-A properly, so Beyoncé, Björk and Sigur Rós look right instead of losing their accents
- **Weather** — current conditions and a 6-hour forecast from [Open-Meteo](https://open-meteo.com/), no API key needed
- **Three player themes and four clock faces** — see [Themes](/guide/themes)
- **Auto-dim** — configurable idle timeout and dimmed brightness level
- **Clock screensaver** — takes over when idle, with an optional photo background

## Under it

- **Two panel sizes, one codebase** — the 4-inch and 7-inch builds come from the same source. Type and spacing scale to the panel rather than being written twice
- **Updates over the air** — from the panel itself, on a stable or nightly channel, resuming rather than restarting if a download is interrupted
- **Browser installer** — flash over USB from Chrome, Edge or Opera, no toolchain to install
- **Settings survive updates** — Wi-Fi credentials and every preference live in flash and are kept across reboots and firmware upgrades
- **Open source** — MIT licensed, [on GitHub](https://github.com/OpenSurface/SonosESP)

## Not supported

Worth being straight about the edges:

- **Searching a streaming service's catalogue.** You cannot browse all of Spotify from the panel. Save what you want in the Sonos app and it appears here.
- **Other ESP32 boards.** The firmware targets specific GUITION hardware, see [Hardware](/guide/hardware).
- **5 GHz Wi-Fi.** The radio is 2.4 GHz only.
