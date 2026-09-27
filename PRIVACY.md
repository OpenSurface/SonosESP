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
- **No location in the panel count.** The request above carries no coordinates,
  no postcode and no Wi-Fi survey. The analytics service records the country it
  arrived from, derived from the IP address at its end and then discarded.
  (The weather widget is separate and does look up an approximate location —
  see [The weather widget and your location](#the-weather-widget-and-your-location)
  below.)
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

## The weather widget and your location

This is the one other thing worth spelling out, because it is on by default
when you enable the weather widget and it is not part of the panel count.

The widget needs coordinates. Its location setting defaults to **Auto-detect**,
and in that mode the panel asks a third-party service to guess where it is from
its public IP address:

```
GET http://ip-api.com/json?fields=lat,lon,city
```

Three things to know about it:

- **It is plain HTTP, not HTTPS.** The reply — your approximate latitude,
  longitude and city — comes back unencrypted and is readable by anything on
  the path. The request itself contains nothing but the query above; your IP is
  simply where it came from.
- **It is a city-level guess, not your address.** IP geolocation typically
  resolves to a town or an ISP's regional hub. It is often wrong by miles.
- **It runs once per screensaver session**, and the result is cached for that
  session. The coordinates are then sent to Open-Meteo (over HTTPS) to get the
  forecast, and are never sent to us.

**To avoid it entirely:** pick your city, or enter your own coordinates, in
**Settings → Clock → Weather location**. Any setting other than Auto-detect
skips the lookup completely. Turning the weather widget off also skips it.

## Everything else the panel talks to

All of these are direct requests from the panel, on your own initiative, and
none of them involve us:

| Service | Why | When |
|---|---|---|
| Your Sonos speakers | The entire point of the device | Continuously, on your LAN |
| LRCLIB | Synced lyrics | When a track changes, if lyrics are enabled |
| Open-Meteo | Weather on the clock screensaver | Periodically, if the widget is on |
| ip-api.com | Approximate location for that weather lookup, from your IP — **plain HTTP** | Once per screensaver session, only on Auto-detect |
| Bing | Daily wallpaper photos for the clock screensaver background | On entry and every few minutes, if photo backgrounds are on |
| Album art hosts | Cover images, at whatever URL Sonos supplies | Per track |
| GitHub | Checking for and downloading firmware updates | On an update check, and once a day in the background |

Your Wi-Fi credentials are stored on the device and are never transmitted
anywhere. See [SECURITY.md](.github/SECURITY.md) for how they are stored and
what that means if you pass the hardware on.

## Questions

Open an issue. If something here is unclear or looks wrong, that is worth
fixing — say so.
