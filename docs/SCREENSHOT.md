# Screenshots over serial

Real screenshots for the README and the docs site, instead of photographing the
panel with a phone.

```
pip install pyserial pillow
python tools/screenshot.py COM9 docs/public/shots/player.png
```

Put the screen in the state you want first, then run the command. `--scale 2`
upscales with nearest-neighbour for a crisp 2× image. `--baud` only matters if
you are capturing on the UART rather than native USB-CDC, which ignores it.

The UI freezes for a second or two while the frame transfers. That is the dump,
not a crash.

A frame is ~1.1 MB of base64, so at 115200 baud the transfer alone is around 90
seconds. That is the line rate, not overhead. If a capture fails mid-stream it is
almost always another task logging into the same port and corrupting a line —
just run it again.

## A whole tour at once

`tools/capture_tour.py` walks every screen, saves a PNG each, and assembles the
homepage GIF:

```
python tools/capture_tour.py COM9 --gif docs/public/sonosESP.gif
```

It prompts before each shot so you can navigate there: Enter to capture, `s` to
skip, `r` to retake the last one. `--only player,queue,rooms` captures a subset.

It prompts rather than driving the UI on purpose. The firmware has no
remote-navigation command, and adding one would mean a serial control surface in
production firmware plus a reflash, to save a few minutes of tapping.

## How it works

LVGL renders with `LV_DISPLAY_RENDER_MODE_FULL`, so `display_flush()` is handed
one contiguous RGB565 buffer holding the entire screen. Nothing is re-rendered
for a screenshot; the bytes already exist and are base64'd out of the port.

Typing `screenshot` sets a flag **and invalidates the active screen**. That
second part is essential: LVGL only flushes when something has been invalidated,
so on a static screen no flush ever happens and the request would sit pending
forever. The next flush copies the frame into a PSRAM buffer, and the main task
ships it between `[shot]` and `[/shot]` markers.

On the 4" the capture is taken from `px_map` — the landscape 800×480 frame LVGL
drew — and **not** from `rotate_buf`, which is the portrait version sent to the
panel and would come out sideways.

### Wire format

```
[shot] <width> <height> <bytes> <lines>
0000:<76 base64 chars>
0001:<76 base64 chars>
...
[/shot]
```

Status lines are `[shot-busy]` and `[shot-err]`. They deliberately do **not**
start with `[shot]`, because the host locates the header by pattern and an
earlier version parsed a status line as the header.

Commands: `screenshot`, `shotline <hex index>` (resend one line), `shotfree`
(release the capture buffer).

## Why it is built this way

**Nothing is copied until you ask.** The cost on the flush path when idle is one
`bool` test. Copying every frame would be ~768 KB of `memcpy` at frame rate on
the 4" and 1.2 MB on the 7", permanently, for a feature used occasionally.

**No pointer to `px_map` is kept.** The draw buffers are double-buffered, so a
stored pointer refers to a buffer being overwritten by the next render while you
are still dumping it. That is what produces torn screenshots.

**Every line carries its index.** Other FreeRTOS tasks print to the same port
and nothing stops one landing inside a payload line. Without an index there is
no way to know *which* line was affected, and a damaged line silently shifts
every byte after it — so a frame could decode to the right length and still be
wrong from that point on.

**The byte and line counts are in the header** so the host can prove it received
everything, rather than writing a plausible-looking image with a corrupt tail.

## Traps, if you reimplement this

**A log line stuck on the end is not corruption.** This is the one that caused
the most trouble. Lines routinely arrive as:

```
1B24:wxjDGMMY...wxjD[SOAP/DMA] #3040: pre=136KB post=136KB
```

The base64 in front is complete and correct — another task's output simply
landed between the payload and `println`'s newline. Requiring the whole line to
match base64 throws away good data, and because the interfering log repeats
constantly, re-requesting the line hits exactly the same problem. Match the
*leading* base64 run and ignore what follows. A log spliced into the middle
truncates that run and is genuinely damaged, so it still gets re-requested.

**Do not use `readline()` on the host.** It returns a *partial* line when its
timeout expires mid-transfer. A truncated base64 line silently drops bytes with
no error raised. Read raw chunks and split afterwards.

**Use an idle timeout, not a total one.** A frame is ~1 MB of base64; how long
that takes depends on the link. A fixed overall deadline truncates the transfer
and then reports it as missing bytes, which looks like corruption but is only
impatience.

**Base64 lines must be a multiple of 4 characters.** A group is 4 characters
encoding 3 bytes; splitting one across a newline shifts everything after it.
The firmware uses 76.

**USB-CDC silently discards writes** it cannot fit in the TX buffer — it does
not block and does not retry. The dump raises the TX timeout so `write()` waits
for space, and flushes periodically so the buffer never fills.

**Do not yield inside the dump loop.** An earlier version called `vTaskDelay(1)`
every 32 lines to be polite about CPU. That hands the core to exactly the tasks
that then print into the middle of a line. The watchdog needs its reset, not a
yield.

**Batch the re-requests.** The board's CDC receive buffer is a few hundred
bytes, so 92 `shotline` commands in one burst overflows it and most are never
read.

## Alternative considered

`lv_snapshot_take()` exists in LVGL but allocates a second full framebuffer to
render into. Grabbing the frame already drawn is cheaper and needs no extra
memory in the steady state.

Serial is also not the ideal transport. The board has WiFi and already speaks
HTTP, and an endpoint serving the raw framebuffer over TCP would need none of
the framing, base64 or retransmit machinery above — TCP handles reliability, and
binary is 33% smaller. Everything here exists to work around a shared, unframed
byte stream. Worth doing if screenshots become a regular chore.
