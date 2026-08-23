#!/usr/bin/env python3
"""Grab a screenshot off a running SonosESP over the serial port.

    python tools/screenshot.py COM7 docs/public/shots/player.png
    python tools/screenshot.py COM7 out.png --scale 2

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


# A valid payload line is nothing but base64 and, on this firmware, always 76
# characters except the last. Log lines from other FreeRTOS tasks can land in
# the middle of the dump, so lines are matched rather than merely un-prefixed.
LINE_RE = re.compile(rb"^[A-Za-z0-9+/]+={0,2}$")


def capture(port_name: str, baud: int, timeout: float) -> tuple[bytes, int, int]:
    # baudrate is ignored by native USB-CDC but matters if you are on the UART.
    with serial.Serial(port_name, baud, timeout=0.1) as port:
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(b"screenshot\n")
        port.flush()

        # NEVER readline() here. It returns a partial line when its timeout
        # expires mid-transfer, and a truncated base64 line silently drops
        # bytes — you get an image with a corrupt tail and no error. Read raw
        # chunks and split on newlines afterwards.
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = port.read(4096)
            if chunk:
                buf += chunk
                if b"[/shot]" in buf:
                    break
            elif b"[shot]" not in buf:
                port.write(b"screenshot\n")   # missed the first one; nudge
                port.flush()
                time.sleep(0.3)

    if b"[shot]" not in buf:
        raise RuntimeError(
            "no [shot] marker seen — is the firmware built with screenshot "
            "support, and is anything else holding the port open?"
        )
    if b"[/shot]" not in buf:
        raise RuntimeError(f"transfer did not finish within {timeout:.0f}s")

    body = buf.split(b"[shot]", 1)[1].split(b"[/shot]", 1)[0]
    header, _, payload = body.partition(b"\n")

    parts = header.split()
    if len(parts) < 3:
        raise RuntimeError(f"bad header: {header!r}")
    w, h, expect = int(parts[0]), int(parts[1]), int(parts[2])

    b64 = b"".join(l for l in (x.strip() for x in payload.splitlines())
                   if l and LINE_RE.match(l))
    data = base64.b64decode(b64)

    # The firmware sends its byte count for exactly this check. Without it a
    # short read produces a plausible-looking image with a corrupt tail.
    if len(data) != expect:
        raise RuntimeError(
            f"incomplete transfer: got {len(data)} of {expect} bytes "
            f"({expect - len(data)} missing). Re-run; if it persists, close "
            f"any serial monitor sharing the port."
        )
    return data, w, h


def to_png(data: bytes, w: int, h: int, path: str, scale: int) -> None:
    # RGB565 little-endian (LV_COLOR_DEPTH 16, LV_COLOR_16_SWAP 0) -> RGB888.
    # The *255//31 form maps 31 to a true 255; a plain <<3 leaves white at 248
    # and everything looks faintly grey.
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. COM7 or /dev/ttyACM0")
    ap.add_argument("out", nargs="?", default="screenshot.png", help="output PNG")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--scale", type=int, default=1,
                    help="nearest-neighbour upscale (2 = crisp 2x for docs)")
    a = ap.parse_args()

    try:
        data, w, h = capture(a.port, a.baud, a.timeout)
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    to_png(data, w, h, a.out, a.scale)
    px = f"{w}x{h}" if a.scale == 1 else f"{w}x{h} -> {w*a.scale}x{h*a.scale}"
    print(f"{a.out}  ({px})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
