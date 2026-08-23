#!/usr/bin/env python3
"""Grab a screenshot off a running SonosESP over the serial port.

    python tools/screenshot.py COM9 docs/public/shots/player.png
    python tools/screenshot.py COM9 out.png --scale 2

Sends "screenshot", collects the base64 frame the firmware dumps between the
[shot] and [/shot] markers, and writes a PNG.

Requires: pyserial, Pillow.  (pip install pyserial pillow)
"""

import argparse
import base64
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing:  pip install pyserial")
try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow missing:  pip install pillow")


# A valid payload line is nothing but base64. Log lines from other FreeRTOS
# tasks can land in the middle of the dump, so lines are matched rather than
# merely un-prefixed.
LINE_RE = re.compile(rb"^[A-Za-z0-9+/]+={0,2}$")

# Match the header by shape rather than splitting on "[shot]". The firmware also
# emits [shot-busy] and [shot-err] status lines, and a bare split on the marker
# will happily parse one of those as the header instead.
HDR_RE = re.compile(rb"\[shot\] (\d+) (\d+) (\d+)[^\n]*\n")
ERR_RE = re.compile(rb"\[shot-err\]([^\r\n]*)")

CMD = b"screenshot\n"


def capture(port_name, baud, timeout, progress=True):
    # baudrate is ignored by native USB-CDC but matters on the UART.
    with serial.Serial(port_name, baud, timeout=0.1) as port:
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(CMD)
        port.flush()

        # Two things matter in this loop.
        #
        # NEVER readline(). It returns a PARTIAL line when its timeout expires
        # mid-transfer, so a truncated base64 line silently drops bytes and you
        # get a corrupt tail with no error raised. Read raw chunks, split later.
        #
        # And the timeout is an IDLE timeout, not a total one. A frame is ~1MB
        # of base64 and how long that takes depends on the link; a fixed overall
        # deadline just truncates the transfer and then reports it as missing
        # bytes, which looks like corruption but is only impatience.
        chunks = []
        got = 0
        last = time.time()
        nudged = False
        while time.time() - last < timeout:
            chunk = port.read(65536)
            if chunk:
                chunks.append(chunk)
                got += len(chunk)
                last = time.time()
                if progress:
                    sys.stderr.write("\r  %d KB" % (got // 1024))
                    sys.stderr.flush()
                if b"[/shot]" in chunk:
                    break
            elif not nudged and not chunks:
                port.write(CMD)          # first one may have been missed
                port.flush()
                nudged = True
                time.sleep(0.3)

        buf = b"".join(chunks)
        if progress and got:
            sys.stderr.write("\r  %d KB received\n" % (got // 1024))

    err = ERR_RE.search(buf)
    if err:
        raise RuntimeError("firmware:" + err.group(1).decode(errors="replace"))

    m = HDR_RE.search(buf)
    if not m:
        if b"[shot" in buf:
            raise RuntimeError(
                "firmware acknowledged the command but sent no frame. If it "
                "reported 'already queued', an earlier request is still waiting "
                "on a redraw - power-cycle the board and retry."
            )
        raise RuntimeError(
            "no [shot] header seen - is the firmware built with screenshot "
            "support, and is anything else holding the port open?"
        )
    if b"[/shot]" not in buf:
        raise RuntimeError(
            "transfer stalled: %.0fs of silence with no [/shot] marker. It "
            "started arriving then stopped - close any serial monitor on the "
            "port, or raise --timeout." % timeout
        )

    w, h, expect = int(m.group(1)), int(m.group(2)), int(m.group(3))
    payload = buf[m.end():].split(b"[/shot]", 1)[0]

    b64 = b"".join(l for l in (x.strip() for x in payload.splitlines())
                   if l and LINE_RE.match(l))
    data = base64.b64decode(b64 + b"=" * (-len(b64) % 4))

    # The firmware sends its byte count for exactly this check. Without it a
    # short read produces a plausible-looking image with a corrupt tail.
    if len(data) != expect:
        raise RuntimeError(
            "incomplete transfer: got %d of %d bytes (%d missing). The board "
            "dropped serial writes - reflash with the current firmware, which "
            "flushes as it sends." % (len(data), expect, expect - len(data))
        )
    return data, w, h


def to_png(data, w, h, path, scale):
    # RGB565 little-endian (LV_COLOR_DEPTH 16, LV_COLOR_16_SWAP 0) -> RGB888.
    # The *255//31 form maps 31 to a true 255; a plain <<3 leaves white at 248
    # and the whole image reads faintly grey.
    out = bytearray(w * h * 3)
    for i in range(w * h):
        v = data[i * 2] | (data[i * 2 + 1] << 8)
        out[i * 3]     = ((v >> 11) & 0x1F) * 255 // 31
        out[i * 3 + 1] = ((v >> 5)  & 0x3F) * 255 // 63
        out[i * 3 + 2] = ( v        & 0x1F) * 255 // 31

    img = Image.frombytes("RGB", (w, h), bytes(out))
    if scale > 1:
        img = img.resize((w * scale, h * scale), Image.NEAREST)
    img.save(path, optimize=True)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. COM9 or /dev/ttyACM0")
    ap.add_argument("out", nargs="?", default="screenshot.png", help="output PNG")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=8.0,
                    help="seconds of SILENCE before giving up (not a total limit)")
    ap.add_argument("--scale", type=int, default=1,
                    help="nearest-neighbour upscale (2 = crisp 2x for docs)")
    ap.add_argument("-q", "--quiet", action="store_true", help="suppress progress")
    a = ap.parse_args()

    try:
        data, w, h = capture(a.port, a.baud, a.timeout, progress=not a.quiet)
    except Exception as e:
        print("error: %s" % e, file=sys.stderr)
        return 1

    to_png(data, w, h, a.out, a.scale)
    px = "%dx%d" % (w, h)
    if a.scale > 1:
        px += " -> %dx%d" % (w * a.scale, h * a.scale)
    print("%s  (%s)" % (a.out, px))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
