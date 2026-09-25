# Privacy

SonosESP runs on your own network and talks to your own speakers. This page
covers the one thing it sends anywhere else, and what it deliberately does not.

## The panel count

Once per startup, about ninety seconds in, the panel makes a single request:

```
GET https://pizza.goatcounter.com/count?p=/boot/2.1.2/4in
User-Agent: SonosESP/2.1.2 (esp32-p4)
```

That is the whole thing. **Two facts: which firmware version it is running, and
whether it is a 4-inch or 7-inch panel.** It exists so the project can answer
"how many of these are actually out there" — download counts show how many
people fetched a file, not how many panels are switched on.

### What is never sent

- **No MAC address, serial number, or device ID.** Nothing that identifies your
  particular panel, now or across reboots.
- **No location.** No coordinates, no postcode, no Wi-Fi survey. The analytics
  service records the country your request arrived from, derived from the IP
  address at its end and then discarded — the panel never knows or sends it.
- **No IP address is stored.** GoatCounter uses it to work out the country and
  does not keep it.
- **Nothing about your music.** No room names, no speaker names, no track,
  artist, album, playlist or queue. No Wi-Fi network name. No Sonos account
  information — the firmware never has any.
- **No cookies, no advertising, no third-party trackers.**

### Turning it off

**Settings → General → Anonymous stats → "Count this panel"**

It is on by default and the switch is remembered across reboots and updates.
When it is off the panel makes no request at all — nothing is queued, retried or
sent later.

If you would rather it never be compiled in, build with `-DANALYTICS_ENABLED=0`.

### Who can see the results

The aggregate numbers are published on the
[project homepage](https://opensurface.github.io/SonosESP/) — a total, a country
breakdown and a firmware-version split. That is the same data you would see; it
is not shared with anyone else, sold, or used for advertising.

## Everything else the panel talks to

All of these are direct requests from the panel, on your own initiative, and
none of them involve us:

| Service | Why | When |
|---|---|---|
| Your Sonos speakers | The entire point of the device | Continuously, on your LAN |
| LRCLIB | Synced lyrics | When a track changes, if lyrics are enabled |
| Open-Meteo | Weather on the clock screensaver | Periodically, if the widget is on |
| Album art hosts | Cover images, at whatever URL Sonos supplies | Per track |
| GitHub | Checking for and downloading firmware updates | On an update check |

Your Wi-Fi credentials are stored on the device and are never transmitted
anywhere. See [SECURITY.md](.github/SECURITY.md) for how they are stored and
what that means if you pass the hardware on.

## Questions

Open an issue. If something here is unclear or looks wrong, that is worth
fixing — say so.
