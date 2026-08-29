# Multi-Screen / Multi-Variant Support — Plan

> Tracking issue: **#89** (7″ screen support). Branch: `feat/multi-screen-support`.
> Status: **PLAN** — no firmware changes yet. This document is the single source of truth
> for how we add the 7″ panel (and future boards) without forking and without breaking the
> 4″ fleet.

---

## 1. Goal

Support multiple hardware variants (today: **4″ JC4880P433C**, **7″ JC1060P470C**; tomorrow:
more) from **one codebase**, with:

- one set of source/logic shared by all boards,
- a clean web-installer experience (pick your screen → flash),
- OTA that delivers the **correct** firmware per board,
- a UI that adapts to any resolution **without a hand-written layout per screen**.

### Why not just merge the existing fork?
[`CoopsInChina/SonosESP`](https://github.com/CoopsInChina/SonosESP) already did the 7″ panel
bring-up — **but it forked off our old `1.6.3`** (its version string is `1.6.3_DS_RC3`), so it is
**missing every 1.7.x/1.8.x reliability fix** (CR-1, H-4/5/7, SDIO defences, #85 WiFi-save, …).
Merging it would regress the project years. **We port its board-specific pieces onto current
`main`, not the other way around.** One codebase = the 7″ never drifts behind again.

---

## 2. Current state (single-variant, hardcoded)

| Layer | Today | File |
|---|---|---|
| Build | one env `[env:esp32-p4]` | `platformio.ini` |
| Display dims/pins | fixed 800×480, ST7701 | `include/display_driver.h`, `include/config.h` |
| UI layout | absolute pixels for 800×480 | `src/screens/*.cpp` |
| CI/release | copies `.pio/build/esp32-p4/firmware.bin` | `.github/workflows/build.yml` |
| Web installer | `manifest-4inch.json` / `manifest-7inch.json` | `web-installer/` |
| OTA | matches asset substring `firmware.bin` | `src/ui_handlers.cpp` (`checkForUpdates`) |

Nothing is variant-aware — this is greenfield.

---

## 3. Variant matrix

| | **4″ (current)** | **7″ (new)** |
|---|---|---|
| Board | GUITION JC4880P433C | GUITION JC1060P470C |
| SoC | ESP32-P4 + C6 | ESP32-P4 + C6 |
| Panel driver | **ST7701** (MIPI DSI) | **JD9165** (MIPI DSI) |
| Resolution (landscape) | **800 × 480** | **1024 × 600** |
| Touch | GT911 | GT911 (different pins) |
| Extra HW | — | **Ethernet** (future) |
| `SCREEN_SIZE` flag | `4` | `7` |
| Firmware asset | `firmware-4inch.bin` (+ legacy `firmware.bin` during transition) | `firmware-7inch.bin` |
| Manifest | `manifest.json` | `manifest-7inch.json` |

> The resolutions **differ** → the UI must adapt (see §6). This is the only substantial work; everything else is small plumbing.

---

## 4. Repo organization

```
platformio.ini            # [env] base + [env:esp32_4inch] + [env:esp32_7inch]
include/
  config.h                # #if SCREEN_SIZE → dims, pins, clock-bg sizes
  display_driver.h        # dims come from config.h; panel-agnostic API
  ui_scale.h              # NEW: SX()/SY() scale macros + font-tier selection
lib/
  st7701_lcd/             # 4″ panel (exists)
  jd9165_lcd/             # 7″ panel (PORT from fork)
  gt911_lcd/pins_config.h # touch pins per SCREEN_SIZE (PORT from fork)
src/
  display_driver.cpp      # selects ST7701 vs JD9165 init by SCREEN_SIZE
  screens/*.cpp           # use SX()/SY() instead of literal pixels
web-installer/
  index.html              # screen-selector dropdown → per-variant manifest
  manifest-4inch.json     # 4″ → firmware-4inch.bin
  manifest-7inch.json     # 7″
assets/7inchScreensavers/ # optional, 7″ screensaver photos
scripts/embed_photos.py   # optional, build-time photo embed
docs/MULTI_SCREEN_SUPPORT.md  # this file
```

**Principle:** board-specific knowledge lives in exactly three places — `platformio.ini`
(which env), `config.h` (dims/pins via `#if SCREEN_SIZE`), and `lib/*_lcd` (panel driver).
Everything else (app logic, screens) is resolution-relative and board-agnostic.

---

## 5. Build system — `platformio.ini`

Use a shared `[env]` base + a small per-variant section, per
[PlatformIO `extends` docs](https://docs.platformio.org/en/stable/projectconf/sections/env/options/advanced/extends.html).

> ⚠️ **Caveat (confirmed):** `build_flags` and `lib_deps` **do not auto-merge** across
> `extends`/`[env]` — you must re-include the parent with interpolation
> (`${env.build_flags}`). Plan for this so the 4″ flags don't silently drop.

```ini
[env]                       ; shared by all variants
platform = .../55.03.38/platform-espressif32.zip
board = esp32-p4
framework = arduino
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
board_build.psram_type = opi
; ... all the common flash/psram/cpu settings ...
build_flags =
    -DBOARD_HAS_PSRAM
    ; ... all the shared flags ...
lib_deps =
    lvgl/lvgl@^9.5.0
    bblanchon/ArduinoJson@^7.4.3
    bitbank2/PNGdec@^1.1.6
    bitbank2/JPEGDEC@^1.8.4
    tamctec/TAMC_GT911@^1.0.2

[env:esp32_4inch]
build_flags = ${env.build_flags} -DSCREEN_SIZE=4
lib_ignore  = jd9165_lcd

[env:esp32_7inch]
build_flags = ${env.build_flags} -DSCREEN_SIZE=7
lib_ignore  = st7701_lcd
```

`config.h` resolves the rest:

```c
#ifndef SCREEN_SIZE
#define SCREEN_SIZE 4          // default 4″ if unset
#endif
#if SCREEN_SIZE == 7
  #define DISPLAY_WIDTH  1024
  #define DISPLAY_HEIGHT 600
  #define DISPLAY_MODEL  "JD9165 7\" (1024x600)"
#elif SCREEN_SIZE == 4
  #define DISPLAY_WIDTH  800
  #define DISPLAY_HEIGHT 480
  #define DISPLAY_MODEL  "ST7701 4\" (800x480)"
#else
  #error "Unsupported SCREEN_SIZE (use 4 or 7)"
#endif
```

> 🔒 **Guardrail:** after the refactor the **4″ binary must be byte-for-byte identical** to
> 1.8.4 (same flags, same code path). Verify with a diff of the built `firmware.bin` before/after.

---

## 6. Responsive UI — the only sizeable work

The current UI uses absolute pixels for 800×480. The 7″ is 1024×600, so we make the UI
**resolution-relative once** instead of maintaining a layout per screen.

### Chosen approach: scale-factor macros + font tiers (`include/ui_scale.h`)

```c
// Coordinates are authored in the 800×480 "design space" and scaled to the panel.
#define SX(v) ((lv_coord_t)((v) * DISPLAY_WIDTH  / 800))
#define SY(v) ((lv_coord_t)((v) * DISPLAY_HEIGHT / 480))
// e.g. lv_obj_set_size(panel_art, SX(450), SY(480));

// Fonts are discrete → pick a tier by width.
#if DISPLAY_WIDTH >= 1024
  #define FONT_TITLE &lv_font_montserrat_32
  #define FONT_BODY  &lv_font_montserrat_20
#else
  #define FONT_TITLE &lv_font_montserrat_24
  #define FONT_BODY  &lv_font_montserrat_16
#endif
```

**Why this one:**
- Keeps the existing absolute-positioning style — a *mechanical wrap*, not a rewrite.
- On 4″, `SX/SY` with the 800/480 base = **identity → zero visual change** (protects the fleet).
- Auto-adapts to any future resolution; coords computed once at screen build → **no runtime cost** (important: PPA/GPU is disabled, all rendering is software).

**Honest caveats:** fonts step between tiers (not continuous); some elements may need a manual
nudge after scaling; pixel-perfect mockups need spot-checking on hardware.

**Font status (7″).** The body-text tiers above use LVGL **built-in** Montserrat fonts and
already step up on the 1024-wide panel (32/20/16 vs the 4″'s 24/16/14) — no generation needed.
The **custom** icon/clock fonts (`lv_font_mdi_*`, `lv_font_weathericons_*`,
`lv_font_montserrat_140`) are pre-generated `.c` files shared by both builds; the 7″ links and
renders them at their native px (clear on 1024×600). Generating *larger* icon/clock variants is a
**hardware-validation polish step** — it needs the physical panel to judge sizing, and the big
clock digits can't be regenerated until `Montserrat-Medium.ttf` is added to the repo (only the
MDI + FontAwesome TTFs ship in `node_modules`). Deliberately **not** fabricated blind, to avoid
flash bloat and unvalidated assets.

### Alternatives considered (and why not)
| Option | Verdict |
|---|---|
| **Flex/Grid (`lv_pct`)** | Truly responsive, cleanest long-term — but a full rewrite of every screen. Revisit later. |
| **Transform-scale whole canvas** | ❌ Expensive in software (PPA off), needs touch-coord inverse-mapping, softens text. |
| **Letterbox 800×480 in a centred box** | OK *stopgap* to ship "7″ beta" fast; wastes screen space. |

> Adopt `SX()/SY()` **now** and the upcoming themes (#87) are born resolution-independent —
> we never hand-write a per-size layout again.

---

## 7. OTA — variant-aware asset selection (trivial logic, careful rollout)

OTA already works. The **only** firmware change: each build fetches **its own** asset
(otherwise a 7″ would grab the 4″ binary → wrong panel → black screen). Canonical names are
**symmetric**: `firmware-4inch.bin` and `firmware-7inch.bin`.

```c
#if SCREEN_SIZE == 7
  const char* WANT_ASSET = "firmware-7inch.bin";
#else
  const char* WANT_ASSET = "firmware-4inch.bin";   // canonical 4" name going forward
#endif
// in checkForUpdates(): if (name.indexOf(WANT_ASSET) >= 0) download_url = ...
```

### ⚠️ Backward-compatibility — the deployed fleet (must not skip this)
The **already-shipped v1.8.4 units search the substring `firmware.bin`**, and
`"firmware-4inch.bin"` does **NOT** contain `"firmware.bin"`. So renaming the 4″ asset to
`firmware-4inch.bin` without care would **silently cut OTA for the entire deployed 4″ fleet.**

**Transition plan (publish BOTH names for the 4″ during migration):**

| Release asset | Found by | Keep until |
|---|---|---|
| `firmware.bin` (legacy alias = the 4″ binary) | **old** units (≤ v1.8.4, search `firmware.bin`) | the fleet has moved to a build that searches `firmware-4inch.bin` |
| `firmware-4inch.bin` (canonical) | **new** 4″ builds (search `firmware-4inch.bin`) | forever |
| `firmware-7inch.bin` (canonical) | 7″ builds | forever |

Rollout order:
1. Ship a 4″ release that **publishes both** `firmware.bin` + `firmware-4inch.bin` (identical
   binary) **and** switches the *firmware's* search string to `firmware-4inch.bin`.
2. After a few cycles (fleet migrated) → **drop `firmware.bin`**; keep only the `-4inch`/`-7inch`
   canonical pair.

So: ~3 lines in firmware, plus CI publishing the legacy `firmware.bin` duplicate during the
transition window. *(Note: the local PlatformIO build output is always
`.pio/build/<env>/firmware.bin` — CI renames it to the release-asset name; "byte-identical"
checks elsewhere in this doc refer to that build output, not the release asset.)*

---

## 8. Web installer

`esp-web-tools` picks a build by `chipFamily`, but **both variants are ESP32-P4** → it can't
auto-distinguish. So:

- One **screen-selector dropdown** on `index.html` (4″ / 7″) that swaps the manifest the
  `<esp-web-install-button>` points at (the fork does exactly this — "Move screen selector to top").
- `manifest-4inch.json` → `firmware-4inch.bin`; `manifest-7inch.json` → `firmware-7inch.bin`.
- Show the selected board's specs (resolution/model) for confidence.

---

## 9. CI / CD automation

Today `build.yml` is hardwired to `.pio/build/esp32-p4/`. Move to a **matrix build →
artifacts → single release job** ([GitHub Actions matrix strategy](https://runs-on.com/github-actions/the-matrix-strategy/),
[multi-platform release pattern](https://dev.to/eugenebabichenko/automated-multi-platform-releases-with-github-actions-1abg)):

```yaml
jobs:
  build:
    strategy:
      matrix:
        include:
          - env: esp32_4inch
            asset: firmware-4inch.bin
          - env: esp32_7inch
            asset: firmware-7inch.bin
    steps:
      - run: pio run -e ${{ matrix.env }}
      - run: cp .pio/build/${{ matrix.env }}/firmware.bin ${{ matrix.asset }}
      # transition: also publish the 4" under the legacy name so ≤v1.8.4 units keep updating
      - if: matrix.env == 'esp32_4inch'
        run: cp .pio/build/esp32_4inch/firmware.bin firmware.bin
      - uses: actions/upload-artifact@v7
        with: { name: ${{ matrix.env }}, path: "*.bin" }

  release:
    needs: build
    if: github.event_name == 'release'
    steps:
      - uses: actions/download-artifact@v7      # pulls every variant
      - uses: softprops/action-gh-release@v2     # firmware-4inch.bin + firmware-7inch.bin
        #                                          + legacy firmware.bin + bootloader/partitions
```

- **Matrix** keeps the workflow DRY and builds variants in parallel.
- A single **release job** depends on all matrix builds (`needs:`) and uploads **all**
  per-variant assets to the GitHub release (so OTA §7 finds them).
- `deploy-pages.yml`: build both, copy `firmware-4inch?.bin`/`firmware-7inch.bin` into
  `web-installer/`, `sed` the version into both manifests. (The fork's `deploy-pages.yml` is a
  good reference — it builds both envs and updates both manifests; we'll do it via matrix.)
- Keep the **nightly skip** guard so nightly versions don't auto-release.

---

## 10. Versioning & release strategy

- One version across all variants (`version.json` stays the single source of truth).
- **4″ canonical asset is `firmware-4inch.bin`**, but a **legacy `firmware.bin` (same binary)
  is published during the transition** so deployed ≤v1.8.4 units keep updating (see §7).
- Cut the first 7″ build as a **nightly** so John / fork users validate before stable.
- After 7″ is confirmed on hardware → promote to a stable release containing **both** assets.

---

## 11. Phased action plan

| Phase | Work | Needs 7″ HW? | Risk |
|---|---|---|---|
| **0. Decisions** | get a 7″ unit; confirm OTA = auto-update; confirm scale-macro approach | — | — |
| **1. Build scaffold** | base `[env]` + `esp32_4inch`/`esp32_7inch`; `config.h` `#if SCREEN_SIZE`; **verify 4″ byte-identical** | no | low |
| **2. Panel bring-up** | port `jd9165_lcd` + `gt911` pins; `display_driver.cpp` panel select; light up 7″ + touch | **yes** | med (HW) |
| **3. Responsive UI** | `ui_scale.h` (`SX/SY` + font tiers); convert screens, main player first; validate 4″ unchanged + 7″ fills | partial | med (broad but mechanical) |
| **4. Distribution** | OTA variant pick (~3 lines); CI matrix + per-variant assets; `manifest-7inch.json` + selector | yes | low |
| **5. Optional** | 7″ screensaver assets + `embed_photos.py` | yes | low |
| **6. Future** | **Ethernet** for 7″ — needs a network abstraction; NOT in the fork | yes | high (separate effort) |

**Phases 1 & 3 can start now, hardware-free**, shipping value to 4″ as a verified-identical
refactor. **Phase 2 gates on owning a 7″ unit** — none of the panel work can be validated here.

---

## 12. Risks & guardrails

- **4″ regression** from the build/UI refactor → byte-identical `firmware.bin` check at Phases 1 & 3.
- **Cross-flash brick** (7″ pulling 4″ firmware) → variant-aware OTA (§7) + per-variant manifests.
- **`extends` flag drop** → use `${env.build_flags}` interpolation; diff the resolved config.
- **Panel bring-up** can only be done with the physical 7″ unit on the bench.
- **Ethernet** touches the SDIO/WiFi crash-defence layer (all WiFi-specific) → keep it a
  separate, later effort behind a clean network abstraction.

---

## 13. Open decisions (for the owner)

1. Acquire a 7″ JC1060P470C? (hard dependency for Phases 2+)
2. 7″ via OTA (auto-update, recommended) **or** web-install-only?
3. Ship 7″ as **nightly-first** then stable? (recommended)
4. Scale-macros now (recommended) vs letterbox beta first?
5. Port the 7″ screensaver assets, or skip?

---

## 14. References

- Fork (7″ bring-up, old base): https://github.com/CoopsInChina/SonosESP
  - `lib/jd9165_lcd/`, `lib/gt911_lcd/pins_config.h`, `web-installer/manifest-7inch.json`,
    `.github/workflows/deploy-pages.yml` (builds both envs), `scripts/embed_photos.py`
- PlatformIO `extends` / shared `[env]`: https://docs.platformio.org/en/stable/projectconf/sections/env/options/advanced/extends.html
- GitHub Actions matrix strategy: https://runs-on.com/github-actions/the-matrix-strategy/
- Multi-platform release pattern: https://dev.to/eugenebabichenko/automated-multi-platform-releases-with-github-actions-1abg
