# Music sources

The Sources screen lists what your household actually has. It asks your speaker
at startup rather than showing a fixed list, so you do not get an empty "Music
Library" if you have no music share, and anything Sonos adds later turns up
without a firmware update.

| Source | What is in it |
|---|---|
| **Music Library** | Artists, album artists, albums, genres, composers, tracks and imported playlists from your network share |
| **Music Shares** | The NAS or SMB shares Sonos has indexed |
| **Sonos Playlists** | Your saved queues |
| **Favorites** | Everything saved in the Sonos app |
| **Internet Radio** | Radio stations and radio shows |
| **Queue** | What is queued right now |
| **Line-In** | The analogue input, on speakers that have one |

## Streaming services

Each service linked in the Sonos app — Spotify, Apple Music, Amazon Music,
TuneIn and others — has its own tile under Sources. Open it to browse the
service the way the Sonos app does: your library, playlists, albums, artists
and radio. Tap a track to play it, or open an album or playlist and choose
**Play all**.

You never sign into anything on the device. The panel uses the accounts your
speakers already hold — see [Privacy](https://github.com/OpenSurface/SonosESP/blob/main/PRIVACY.md#streaming-service-accounts).
The tiles appear a few seconds after you first open Sources, while the panel
asks a speaker which services are linked.

Anything you have saved in the Sonos app keeps playing from **Favorites** as
well, whatever service it came from.

::: warning Relies on an undocumented part of the Sonos protocol
A Sonos update can change it. If the service tiles stop appearing, Favorites
and Sonos Playlists still work.
:::

::: info Searching a service's catalogue is not supported yet
Browse to what you want, or save it in the Sonos app.
:::

## Getting around

Browsing goes as deep as you need, with a back arrow and a breadcrumb showing
where you are. Long lists load in pages rather than stopping at an arbitrary
cut-off, so a 500-track queue is entirely reachable.
