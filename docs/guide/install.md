# Install

Flashing takes about a minute. You need a USB-C cable that carries data, and
Chrome, Edge or Opera on a desktop — those are the browsers that support Web
Serial. Firefox and Safari cannot flash.

<ClientOnly>
  <InstallPanel />
</ClientOnly>

## If nothing appears when you connect

**Try the other USB-C port.** The board has two and only one of them talks to a
computer. This is the single most common reason a board does not show up.

**Hold BOOT while connecting.** Hold the BOOT button down, plug the cable in
while still holding, then release after a couple of seconds. That forces the
board into its flashing mode.

Still stuck? See [Troubleshooting](/TROUBLESHOOTING).

## After it finishes

1. Unplug and replug the board
2. The Wi-Fi setup screen appears
3. Pick your network and enter the password on the on-screen keyboard
4. Go to **Settings → Speakers → Scan** to find your Sonos

**Wi-Fi is 2.4 GHz only.** If your router uses one name for both 2.4 and 5 GHz,
that is a common reason setup fails.

Your settings live in flash and survive both reboots and firmware updates, so
you only do this once.

## Flashing manually

The browser installer is the supported route. These are for when it is not an
option — no Chromium browser, a locked-down machine, or scripting a batch of
panels.

Grab `bootloader.bin`, `partitions.bin`, `boot_app0.bin` and the firmware for
your panel (`firmware-4inch.bin` or `firmware-7inch.bin`) from the
[latest release](https://github.com/OpenSurface/SonosESP/releases/latest).

```bash
pip install esptool

esptool.py --chip esp32p4 --port COM9 write_flash   0x2000  bootloader.bin   0x8000  partitions.bin   0xe000  boot_app0.bin   0x10000 firmware-4inch.bin
```

::: warning Do not skip boot_app0.bin
The part at `0xe000` resets the OTA data. Without it, a panel that has already
taken an over-the-air update keeps booting the OTA slot — so the flash appears
to succeed and the old firmware still runs.
:::

Replace the port with yours: `COM9` on Windows, `/dev/ttyUSB0` or
`/dev/cu.usbserial-*` on Linux and macOS. On Linux, add yourself to `dialout`
(`sudo usermod -a -G dialout `) and log back in if you get a permission
error. If flashing keeps failing, `esptool.py --chip esp32p4 --port COM9
erase_flash` first — that also wipes your Wi-Fi credentials and settings.

From a clone, PlatformIO does the same thing in one step:

```bash
pio run -e esp32_4inch --target upload    # or -e esp32_7inch
```

## Updating later

You do not need the browser again. The panel updates itself from
**Settings → Firmware Update**. See [Updating](/guide/updates).
