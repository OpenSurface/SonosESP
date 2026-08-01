# Troubleshooting

Common problems and how to fix them. If none of this helps, [open an issue](https://github.com/OpenSurface/SonosESP/issues) — and please include the [serial log](#how-to-get-a-serial-log), it usually identifies the cause immediately.

**Jump to:** [Computer can't see the device](#the-computer-cant-see-the-device) · [Updates fail](#updates-fail-or-stop-partway) · [Blank screen](#blank-screen-after-an-update) · [Wi-Fi](#wi-fi-wont-connect) · [No speakers](#no-sonos-speakers-found) · [Album art](#album-art-doesnt-appear) · [Lyrics](#lyrics-dont-appear) · [Screen flickers](#the-screen-flickers-during-an-update) · [Full reset](#start-completely-fresh) · [Serial log](#how-to-get-a-serial-log)

---

## The computer can't see the device

Nothing appears in Device Manager / no new serial port when you plug it in. **Usually this is not a broken board.** Work through these in order.

### 1. Use the other USB-C port ⭐

**This is the most common cause.** The board has **two USB-C ports** and only one of them talks to a computer:

| Port | What it's for |
|---|---|
| **Full-speed Type-C** | Flashing and serial — **use this one** |
| **High-speed Type-C** | USB host/OTG — looks completely dead to a PC |

They look identical. If you're on the wrong one there is no beep, no Device Manager entry, nothing — exactly like a dead board. **Move the cable to the other port and try again.**

### 2. Try a different USB-C cable

Many USB-C cables are **charge-only** and carry no data. If a cable charges your phone that proves nothing. Use one you know does data transfer.

### 3. Force recovery (bootloader) mode

Works even if the wrong firmware was flashed or the firmware crashes at boot.

There are **two small buttons next to each other** on the board (typically marked `BOOT`/`IO0` and `RST`/`EN`):

1. Hold **one** of them down
2. Keeping it held, plug in the USB-C cable
3. Hold ~2 more seconds, then release

If nothing changes, repeat with **the other button**. One of them is BOOT — that's the one that forces recovery mode.

> On some builds of this board the buttons aren't fitted and only solder pads are present. If you can't find any buttons, that's normal — rely on steps 1 and 2.

### 4. Check the driver

The ESP32-P4 uses its **built-in USB serial** — no driver is normally required on Windows 10/11, macOS or Linux. If Device Manager shows an *unknown device* rather than nothing at all, the board is alive and it's a driver/cable issue, not damage.

### Still nothing?

If the board doesn't appear **in recovery mode, on the correct port, with a known-good data cable, on more than one computer**, then it may genuinely be damaged.

---

## Updates fail or stop partway

Symptoms: the progress bar reaches 30–50 % and stops, or the update needs many attempts.

**What to do now:** retry from **Settings → Firmware Update**. If it repeatedly fails, install the latest firmware with the [web installer](https://opensurface.github.io/SonosESP/) instead — that path is much more robust than OTA. Your Wi-Fi and settings are preserved unless you tick the erase option.

**What the messages mean** (visible on screen, and in the serial log):

| Message | Meaning |
|---|---|
| `Download stalled - network timeout` | No data arrived for 30 s. Usually the server or Wi-Fi dropped the transfer. |
| `Download timeout - try again` | The whole download exceeded its time limit. |
| `Download failed (HTTP …)` | The server rejected the request — check internet access. |
| `Not enough space for OTA` | The firmware won't fit the OTA partition. |
| `No firmware URL — check for updates first` | Tap *Check for Updates* before *Install*. |

**Things that genuinely help:**
- Move the device closer to the router — the download is long and a weak signal is the usual culprit
- Avoid updating while music is streaming to the same device
- Retry at a quieter time; the release files are served by GitHub's CDN

---

## Blank screen after an update

**Most likely cause: the wrong screen variant was installed.** A 4″ build on a 7″ panel (or the reverse) initialises the wrong display driver, so the panel stays dark even though the device is running normally.

This affected 7″ units updating **from v1.9.0 or earlier**, whose updater could only find one firmware file — the 4″ one. Fixed in v1.10.0+, but the fix only helps once it's on the device, so affected units need one manual reflash.

**Fix:** install over USB with the [web installer](https://opensurface.github.io/SonosESP/) and **pick the correct screen size**. From then on updates choose the right build automatically.

---

## Wi-Fi won't connect

| Message | What it means |
|---|---|
| `Authentication failed - check password` | Wrong password. Re-enter it carefully — it's case-sensitive. |
| `Network not found` | The SSID wasn't seen. Check it's in range and broadcasting. |
| `Connection timeout` | Router didn't complete the connection. |

Points to check:
- **2.4 GHz only.** The Wi-Fi radio does not support 5 GHz. If your router uses one name for both bands, the device may be trying the 5 GHz one — temporarily splitting the bands is the reliable test.
- **Hidden networks** aren't selectable from the scan list.
- **Special characters** in the password are supported, but check the on-screen keyboard entered exactly what you intended.
- **Captive portals** (guest networks needing a web login) will not work.

Credentials are saved in NVS and survive reboots and firmware updates.

---

## No Sonos speakers found

1. Confirm the device is on Wi-Fi (the settings screen shows the connection state)
2. **Sonos and the display must be on the same network/subnet.** Guest networks, VLANs and some mesh setups block the discovery traffic used to find speakers.
3. Go to **Settings → Speakers → Scan** and allow a few seconds
4. Check **AP isolation / client isolation** is off on your router — this silently blocks device-to-device traffic
5. Confirm the Sonos app on your phone sees the speakers from the same network

---

## Album art doesn't appear

- Some sources simply don't provide artwork — radio stations often have none
- Very large images are rejected on purpose to protect memory
- Occasional misses after long uptime are usually memory pressure; a reboot restores it
- A placeholder music note means "no artwork available", not a fault

---

## Lyrics don't appear

- Enable them in **Settings → General → Show synced lyrics**
- Lyrics come from [LRCLIB](https://lrclib.net/) and **only time-synced lyrics are shown**. Many tracks have none — this is the usual reason.
- Matching relies on the artist/title reported by Sonos; live or remastered titles often fail to match
- Radio and TV/line-in sources have no lyrics

---

## The screen flickers during an update

**This is a known hardware limitation, not a bug we can currently fix.**

While firmware is written to flash, the processor's cache must be switched off. The display's image lives in external memory reached through that cache, so for those moments the panel is fed invalid data and flickers.

The feature that avoids this (flash "auto-suspend") is [not supported on the ESP32-P4](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/spi_flash/spi_flash_optional_feature.html) — Espressif's documentation says support "may be added in the future". It's harmless: let the update finish and the device reboots normally.

---

## Start completely fresh

To wipe all settings (Wi-Fi, speaker choice, theme, clock and display preferences):

1. Open the [web installer](https://opensurface.github.io/SonosESP/)
2. Pick your screen size
3. Tick the **erase device** option when prompted
4. After flashing, set up Wi-Fi again from scratch

---

## How to get a serial log

The log almost always names the exact failure, and it's the single most useful thing to attach to an issue.

1. Connect the device to your computer (see [the USB section](#the-computer-cant-see-the-device) if it isn't detected)
2. Open any serial monitor at **115200 baud** — Arduino IDE, PuTTY, `screen`, or:
   ```bash
   pio device monitor -b 115200
   ```
3. Reproduce the problem
4. Copy the output into your issue

Useful lines to look for: `[OTA]`, `[WIFI]`, `[SONOS]`, `[ART]`, `[CLOCK]`.

---

## Still stuck?

[Open an issue](https://github.com/OpenSurface/SonosESP/issues) with:

- What you were doing and what happened
- Your screen size (4″ or 7″) and firmware version (**Settings → Firmware Update**)
- A serial log if you can get one
- A photo of the screen, if the problem is visual
