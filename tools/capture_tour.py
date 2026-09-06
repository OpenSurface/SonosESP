#!/usr/bin/env python3
"""Walk the UI, grab a PNG of every screen, and build the homepage GIF.

    python tools/capture_tour.py COM9
    python tools/capture_tour.py COM9 --gif docs/public/sonosESP.gif
    python tools/capture_tour.py COM9 --only player,queue,rooms

Prompts for each screen, waits for you to navigate there, then captures it over
the serial port using the same protocol as tools/screenshot.py. Press Enter to
shoot, 's' to skip a screen, 'r' to retake the last one.

Why it prompts rather than driving the UI: the firmware has no remote-navigation
command, and adding one would mean a serial control surface in production
firmware plus a reflash, to save a few minutes of tapping. Not worth it.

Requires: pyserial, Pillow.  (pip install pyserial pillow)
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from screenshot import capture, to_png
except ImportError:
    sys.exit("cannot import tools/screenshot.py - run this from the repo root")

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow missing:  pip install pillow")


# name, filename stem, what to do before shooting, how long it holds in the GIF
TOUR = [
    ("player",   "01-player",
     "The player, with a track playing and its artwork loaded", 2600),
    ("lyrics",   "02-lyrics",
     "Same screen, once synced lyrics appear in the shelf (LRC chip lights)", 2200),
    ("queue",    "03-queue",
     "Tap the queue chip - the drawer slides in from the right", 2000),
    ("rooms",    "04-rooms",
     "Dismiss the drawer, then tap the room pill - the Rooms card", 2000),
    ("general",  "05-settings-general",
     "Dismiss, tap the gear - Settings opens on General", 1800),
    ("speakers", "06-settings-speakers",
     "Settings > Speakers", 1600),
    ("groups",   "07-settings-groups",
     "Settings > Groups", 1600),
    ("sources",  "08-settings-sources",
     "Settings > Sources", 1600),
    ("display",  "09-settings-display",
     "Settings > Display", 1600),
    ("wifi",     "10-settings-wifi",
     "Settings > WiFi", 1600),
    ("clock",    "11-settings-clock",
     "Settings > Clock", 1600),
    ("update",   "12-settings-update",
     "Settings > Update", 1600),
    ("saver",    "13-screensaver",
     "Leave the panel idle until the clock face appears "
     "(Settings > Clock > Inactivity timeout = 1 min makes this quick)", 2600),
]


def shoot(port, baud, timeout, path, scale):
    """One capture. Returns True on success; prints and returns False on failure."""
    try:
        data, w, h = capture(port, baud, timeout, progress=False)
    except Exception as e:
        print("    capture failed: %s" % e)
        return False
    if not data:
        print("    capture returned nothing")
        return False
    to_png(data, w, h, path, scale)
    print("    saved %s  (%dx%d)" % (path, w, h))
    return True


def build_gif(frames, out, loop=0):
    """Assemble the PNGs into a GIF, holding each for its own duration.

    Per-frame durations matter here: the player and the screensaver are what the
    GIF is actually selling, and a settings page needs about half as long to read.
    A single global duration makes the whole thing feel either rushed or padded.
    """
    if not frames:
        print("no frames - nothing to build")
        return
    imgs, durations = [], []
    for path, ms in frames:
        im = Image.open(path).convert("RGB")
        imgs.append(im)
        durations.append(ms)

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    # ADAPTIVE palette per frame, not a shared web palette: these screens are
    # mostly one warm ramp plus a gold accent, and the 216-colour web palette
    # visibly banded the artwork and the progress fill.
    imgs[0].save(
        out,
        save_all=True,
        append_images=imgs[1:],
        duration=durations,
        loop=loop,
        optimize=True,
        disposal=2,
    )
    size_mb = os.path.getsize(out) / 1048576.0
    print("\n%s  %d frames  %.2f MB" % (out, len(imgs), size_mb))
    if size_mb > 8:
        print("  NOTE: over 8 MB. Drop a few screens with --only, or scale the")
        print("  PNGs down before assembling, if this is going on a landing page.")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port the panel is on, e.g. COM9")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=25.0)
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--shots", default="docs/public/shots",
                    help="where the PNGs go (default: docs/public/shots)")
    ap.add_argument("--gif", default=None,
                    help="also build a GIF here, e.g. docs/public/sonosESP.gif")
    ap.add_argument("--only", default=None,
                    help="comma-separated screen names to capture, e.g. player,queue")
    args = ap.parse_args()

    tour = TOUR
    if args.only:
        want = {s.strip() for s in args.only.split(",")}
        unknown = want - {t[0] for t in TOUR}
        if unknown:
            sys.exit("unknown screen(s): %s\nknown: %s"
                     % (", ".join(sorted(unknown)), ", ".join(t[0] for t in TOUR)))
        tour = [t for t in TOUR if t[0] in want]

    os.makedirs(args.shots, exist_ok=True)
    print("Capturing %d screen(s) from %s" % (len(tour), args.port))
    print("Enter = shoot, s = skip, r = retake the one just taken, q = stop\n")

    frames = []
    i = 0
    while i < len(tour):
        name, stem, instruction, hold = tour[i]
        path = os.path.join(args.shots, stem + ".png")
        print("[%d/%d] %s" % (i + 1, len(tour), name))
        print("    %s" % instruction)
        try:
            key = input("    > ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print("\nstopped")
            break

        if key == "q":
            break
        if key == "s":
            print("    skipped\n")
            i += 1
            continue
        if key == "r" and frames:
            frames.pop()
            i -= 1
            print("    dropped the previous shot, redo it\n")
            continue

        if shoot(args.port, args.baud, args.timeout, path, args.scale):
            frames.append((path, hold))
            i += 1
        else:
            print("    not counted - fix and press Enter to retry, or s to skip")
        print()

    print("captured %d screen(s)" % len(frames))
    if args.gif:
        build_gif(frames, args.gif)


if __name__ == "__main__":
    main()
