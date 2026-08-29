"""
Pre-build patch: clamp the GT911 touch-point loop to the size of its own array.

WHY THIS EXISTS
---------------
TAMC_GT911 1.0.2 reads the touch-point count straight out of the controller's
GT911_POINT_INFO register and uses it, unchecked, as a loop bound:

    touches = pointInfo & 0xF;          // 0..15
    ...
    if (bufferStatus == 1 && isTouched) {
        for (uint8_t i = 0; i < touches; i++) {
            readBlockData(data, GT911_POINT_1 + i * 8, 7);
            points[i] = readPoint(data);        // TP_Point points[5];
        }
    }

`points` holds 5 entries. A low nibble above 5 — which any I2C glitch, NAK,
brown-out or flexing ribbon can produce (0xFF reads as bufferStatus=1,
touches=15) — writes up to 10 TP_Point structs past the end of the array, into
whatever `.bss` follows the GT911 object. In this project that neighbourhood
holds the touch driver's own state and the `lv_indev_t*` LVGL dereferences on
every frame, so the symptom is a StoreProhibited/LoadProhibited panic whose
backtrace points at LVGL rather than at touch.

The overflow happens INSIDE the library, so it cannot be guarded from our call
site: by the time ts.read() returns, the damage is done. The library is pulled
from the registry by lib_deps, so editing it in place would be undone by the
next `pio pkg update` — hence a build-time patch, following the same pattern as
remove_lvgl_asm.py.

The patch is idempotent and verifies its own result. If upstream ever fixes this
(or restructures the loop), the marker check makes this a no-op rather than a
silent corruption of their source.
"""

import glob
import os

try:
    Import("env")  # type: ignore  # PlatformIO SCons global
except Exception:
    env = None  # type: ignore

VULNERABLE = "for (uint8_t i=0; i<touches; i++) {"
PATCHED = "for (uint8_t i=0; i<touches && i<5; i++) {  /* SonosESP: clamp to sizeof(points) */"


def patch_file(path):
    try:
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
            src = f.read()
    except OSError as e:
        print("[gt911-patch] cannot read %s: %s" % (path, e))
        return False

    if PATCHED in src:
        return False  # already done on a previous build
    if VULNERABLE not in src:
        # Upstream changed this code. Say so loudly rather than pretending we
        # patched something — the bound needs re-checking by hand.
        print("[gt911-patch] WARNING: expected loop not found in %s" % path)
        print("[gt911-patch] the upstream source changed — RE-VERIFY the points[] bound")
        return False

    src = src.replace(VULNERABLE, PATCHED, 1)
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(src)
    print("[gt911-patch] clamped touch-point loop in %s" % path)
    return True


def main():
    project_dir = env.get("PROJECT_DIR") if env else os.getcwd()
    pattern = os.path.join(project_dir, ".pio", "libdeps", "*", "TAMC_GT911", "TAMC_GT911.cpp")
    targets = glob.glob(pattern)

    if not targets:
        # Nothing to do on a clean tree before deps are fetched; PlatformIO runs
        # this again once they are present.
        return

    for path in targets:
        patch_file(path)


main()
