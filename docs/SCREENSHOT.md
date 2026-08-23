# Screenshots over serial

Real screenshots for the README and the docs site, instead of photographing the
panel with a phone.

```
pip install pyserial pillow
python tools/screenshot.py COM7 docs/public/shots/player.png
```

Put the screen in the state you want first, then run the command. The UI freezes
for a second or two while the frame transfers — that is the dump, not a crash.

`--scale 2` upscales with nearest-neighbour for a crisp 2× image on the docs
site. `--baud` only matters if you are capturing on the UART rather than native
USB-CDC, which ignores it.

## How it works

LVGL renders with `LV_DISPLAY_RENDER_MODE_FULL`, so `display_flush()` is handed
one contiguous RGB565 buffer holding the entire screen. Nothing is re-rendered
for a screenshot; the bytes already exist and are simply base64'd out of the
serial port.

Typing `screenshot` sets a flag. The next full flush copies the frame into a
PSRAM buffer, and the main task ships it between `[shot]` and `[/shot]` markers.

On the 4" the capture is taken from `px_map` — the landscape 800×480 frame LVGL
drew — and **not** from `rotate_buf`, which is the portrait version sent to the
panel and would come out sideways.

## Why it is built this way

**Nothing is copied until you ask.** The cost on the flush path when idle is one
`bool` test. Copying every frame unconditionally would be ~768 KB of `memcpy` at
frame rate on the 4" and 1.2 MB on the 7", permanently, for a feature used a
handful of times.

**A pointer to `px_map` is not kept.** The draw buffers are double-buffered, so a
stored pointer refers to a buffer that is being overwritten by the next render
while you are still dumping it — that is what produces torn screenshots. The
frame is copied once, on request, and the buffer is freed again afterwards
rather than holding PSRAM for a feature that is idle almost always.

**The byte count is in the header.** `[shot] <w> <h> <bytes>` lets the host prove
it received everything. Without it a short read decodes into a plausible-looking
image with a corrupt tail and no error at all.

## Two traps, if you reimplement this

**Do not use `readline()` on the host.** It returns a *partial* line when its
timeout expires mid-transfer. A truncated base64 line silently drops bytes and
you get a corrupt tail with no error raised. Read raw chunks and split on
newlines afterwards, which is what `tools/screenshot.py` does.

**Base64 lines must be a multiple of 4 characters.** A base64 group is 4
characters encoding 3 bytes; splitting a group across a newline shifts every
byte after it. The firmware uses 76.

Other FreeRTOS tasks can print into the middle of the dump. The host matches
payload lines against a strict base64 pattern rather than trusting position, so
an interleaved `[WIFI] reconnecting...` is rejected without corrupting the frame.

## Alternative considered

`lv_snapshot_take()` exists in LVGL, but allocates a second full framebuffer to
render into. Grabbing the frame that has already been drawn is cheaper and needs
no extra memory in the steady state.
