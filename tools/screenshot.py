#!/usr/bin/env python3
"""Grab a screenshot off a running SonosESP over the serial port.

    python tools/screenshot.py COM9 docs/public/shots/player.png
    python tools/screenshot.py COM9 out.png --scale 2

Sends "screenshot", collects the indexed base64 frame the firmware dumps between
the [shot] and [/shot] markers, re-requests any line that arrived damaged, and
writes a PNG.

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


# Payload lines are "XXXX:<base64>". The index is what makes recovery possible:
# other FreeRTOS tasks print to the same port and can land inside a line, and
# without an index there is no way to know which line was damaged.
PAY_RE = re.compile(rb"^([0-9A-Fa-f]{4}):([A-Za-z0-9+/]+={0,2})$")

# Match the header by shape rather than splitting on "[shot]". The firmware also
# emits [shot-busy] and [shot-err] status lines, and a bare split on the marker
# will happily parse one of those as the header instead.
HDR_RE = re.compile(rb"\[shot\] (\d+) (\d+) (\d+) (\d+)[^\n]*\n")
ERR_RE = re.compile(rb"\[shot-err\]([^\r\n]*)")

CMD = b"screenshot\n"
PER_LINE = 57          # source bytes encoded per line (76 base64 chars)


def read_until_quiet(port, timeout, progress=False, seen=0):
    """Read raw chunks until the port goes quiet for `timeout` seconds.

    NEVER readline() here: it returns a PARTIAL line when its timeout expires
    mid-transfer, so a truncated base64 line silently drops bytes.

    The timeout is an IDLE timeout, not a total one. A frame is ~1MB of base64
    and how long that takes depends on the link; a fixed overall deadline just
    truncates the transfer and reports it as missing bytes.
    """
    chunks = []
    got = 0
    last = time.time()
    while time.time() - last < timeout:
        chunk = port.read(65536)
        if chunk:
            chunks.append(chunk)
            got += len(chunk)
            last = time.time()
            if progress:
                sys.stderr.write("\r  %d KB" % ((seen + got) // 1024))
                sys.stderr.flush()
            if b"[/shot]" in chunk:
                break
    return b"".join(chunks), got


def harvest(buf, lines):
    """Pull every intact indexed payload line out of `buf` into `lines`."""
    for raw in buf.splitlines():
        m = PAY_RE.match(raw.strip())
        if m:
            lines[int(m.group(1), 16)] = m.group(2)


def capture(port_name, baud, timeout, progress=True, retries=3):
    with serial.Serial(port_name, baud, timeout=0.1) as port:
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(CMD)
        port.flush()

        buf, got = read_until_quiet(port, timeout, progress)
        if not buf.strip():
            port.write(CMD)          # first one may have been missed
            port.flush()
            more, n = read_until_quiet(port, timeout, progress, got)
            buf += more
            got += n

        err = ERR_RE.search(buf)
        if err:
            raise RuntimeError("firmware:" + err.group(1).decode(errors="replace"))

        m = HDR_RE.search(buf)
        if not m:
            if b"[shot" in buf:
                raise RuntimeError(
                    "firmware acknowledged the command but sent no frame. If it "
                    "reported 'already queued', an earlier request is still "
                    "waiting on a redraw - power-cycle the board and retry.")
            raise RuntimeError(
                "no [shot] header seen - is the firmware built with screenshot "
                "support, and is anything else holding the port open?")

        w, h, expect, n_lines = (int(m.group(i)) for i in (1, 2, 3, 4))

        lines = {}
        harvest(buf[m.end():], lines)

        # Re-request only what is missing. A log line printed by another task
        # mid-dump corrupts exactly the line it landed in; asking for that one
        # again costs milliseconds instead of a whole 1MB recapture.
        for attempt in range(retries):
            missing = [i for i in range(n_lines) if i not in lines]
            if not missing:
                break
            if progress:
                sys.stderr.write("\r  recovering %d damaged line%s\n"
                                 % (len(missing), "" if len(missing) == 1 else "s"))
            for idx in missing:
                port.write(b"shotline %04X\n" % idx)
            port.flush()
            more, _ = read_until_quiet(port, max(2.0, timeout / 3))
            harvest(more, lines)

        port.write(b"shotfree\n")     # let the board free its 768KB capture buffer
        port.flush()

    missing = [i for i in range(n_lines) if i not in lines]
    if missing:
        raise RuntimeError(
            "%d of %d lines could not be recovered (first: %d). Re-run; if it "
            "persists, close any serial monitor sharing the port."
            % (len(missing), n_lines, missing[0]))

    b64 = b"".join(lines[i] for i in range(n_lines))
    data = base64.b64decode(b64 + b"=" * (-len(b64) % 4))

    if len(data) != expect:
        raise RuntimeError("length mismatch: decoded %d, header said %d"
                           % (len(data), expect))
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
    ap.add_argument("--retries", type=int, default=3,
                    help="rounds of re-requesting damaged lines")
    ap.add_argument("-q", "--quiet", action="store_true", help="suppress progress")
    a = ap.parse_args()

    try:
        data, w, h = capture(a.port, a.baud, a.timeout,
                             progress=not a.quiet, retries=a.retries)
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
