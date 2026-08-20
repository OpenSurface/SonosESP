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

## Updating later

You do not need the browser again. The panel updates itself from
**Settings → Firmware Update**. See [Updating](/guide/updates).
