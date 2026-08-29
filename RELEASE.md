# Release Guide

This guide explains how to create nightly and stable releases for SonosESP.

## Table of Contents

- [Release Channels](#release-channels)
- [Prerequisites](#prerequisites)
- [Creating a Nightly Release](#creating-a-nightly-release)
- [Creating a Stable Release](#creating-a-stable-release)
- [Transitioning from Nightly to Stable](#transitioning-from-nightly-to-stable)
- [OTA Update Channels](#ota-update-channels)
- [Troubleshooting](#troubleshooting)

---

## Release Channels

SonosESP has two release channels:

- **Stable**: Production-ready releases for end users
  - Auto-triggered when pushing a stable version to `main`
  - Marked as regular releases (not prerelease)
  - Accessible via OTA "Stable" channel

- **Nightly**: Development/testing builds
  - Manually triggered workflow
  - Marked as prereleases
  - Accessible via OTA "Nightly" channel
  - Version format: `X.Y.Z-nightly.COMMITHASH`

### Screen variants (one codebase, two builds)

Since the multi-screen work (#89), every release is built for **both** screens from
the same source via PlatformIO envs (see `platformio.ini`):

| Variant | Env | Board | App binary |
|---|---|---|---|
| **4″** (shipping) | `esp32_4inch` | GUITION JC4880P433C (ST7701, 800×480) | `firmware-4inch.bin` |
| **7″** (BETA) | `esp32_7inch` | GUITION JC1060P470C (JD9165, 1024×600) | `firmware-7inch.bin` |

- `bootloader.bin` / `partitions.bin` are **board-level and identical** across both, so a release ships one shared copy of each.
- **Legacy `firmware.bin` / `firmware-merged.bin` were removed in v1.15.1.** Both showed 0 downloads on every release from v1.13.0 onward while the per-screen assets in those same releases saw real traffic, so the ≤v1.8.4 fleet they existed for is gone. The merged images were also malformed (see below).
- The **7″ is not yet hardware-validated**: it is offered as **BETA via the web installer**, not pushed to the stable OTA fleet. Don't gate a 7″ rollout on `auto-release` until a physical 7″ unit confirms panel/touch bring-up.

---

## Prerequisites

### Required Tools

1. **Git** - Version control
2. **Python 3.11+** - For version management scripts
3. **GitHub CLI (`gh`)** - For triggering workflows
   - Install from: https://cli.github.com/
   - Authenticate: `gh auth login`

### Verify Installation

```bash
git --version
python --version
gh --version
```

---

## Creating a Nightly Release

Nightly releases are for testing new features before they're ready for production.

### Step 1: Make Your Changes

```bash
# Make code changes
git add .
git commit -m "feat: Your feature description"
```

### Step 2: Bump to Nightly Version

```bash
python scripts/bump_version.py nightly
```

**What this does:**
- Reads current version from `version.json` (e.g., `1.1.6`)
- Appends `-nightly.COMMITHASH` (e.g., `1.1.6-nightly.abc1234`)
- Updates all version files:
  - `version.json`
  - `web-installer/manifest-4inch.json` and `web-installer/manifest-7inch.json`
  - `include/ui_common.h`

**Output:**
```
Current version: 1.1.6
New version: 1.1.6-nightly.abc1234

[SUCCESS] Version bumped to 1.1.6-nightly.abc1234

[NIGHTLY] Next steps:
  1. Build and test locally: pio run
  2. git add -A && git commit -m "chore: Nightly build 1.1.6-nightly.abc1234"
  3. git push origin main
  4. python scripts/create_nightly.py  # Creates GitHub nightly release
```

### Step 3: Test Locally (Optional but Recommended)

```bash
pio run
```

Verify the build succeeds and test on your device if possible.

### Step 4: Commit and Push

```bash
git add -A
git commit -m "chore: Nightly build 1.1.6-nightly.abc1234"
git push origin main
```

**Important:** Pushing a nightly version will **NOT** trigger automatic releases. The following workflows will skip:
- Auto Release (skipped - nightly detected)
- Build Firmware (skipped - nightly detected)
- Deploy Web Installer (skipped - nightly detected)

### Step 5: Trigger Nightly Release Workflow

```bash
python scripts/create_nightly.py
```

**What this does:**
1. Reads current version from `version.json`
2. Generates nightly tag: `X.Y.Z-nightly.COMMITHASH`
3. Shows commit info for confirmation
4. Triggers `nightly-release.yml` workflow via GitHub CLI

**Interactive Prompt:**
```
[CONFIRM] This will create a nightly prerelease:
  - Tag: v1.1.6-nightly.abc1234
  - Commit: chore: Nightly build 1.1.6-nightly.abc1234
  - Author: Your Name
  - Marked as: Prerelease (unstable)

Continue? [y/N]: y
```

**Output:**
```
[SUCCESS] Nightly release workflow triggered!

[INFO] Monitor progress:
  gh run list --workflow=nightly-release.yml
  gh run watch

[INFO] Or visit: https://github.com/OpenSurface/SonosESP/actions
```

### Step 6: Monitor Workflow

```bash
# List recent workflow runs
gh run list --workflow=nightly-release.yml

# Watch the running workflow
gh run watch
```

**Workflow Steps:**
1. Validate version tag format
2. Check if release already exists
3. Build firmware with PlatformIO
4. Create merged firmware binary
5. Create release ZIP
6. Create GitHub prerelease with binaries

**Expected Duration:** ~5 minutes

### Step 7: Verify Release

```bash
# View the release
gh release view v1.1.6-nightly.abc1234

# Or visit GitHub
https://github.com/OpenSurface/SonosESP/releases
```

**Release should contain:**
- (`firmware-merged.bin` was removed in v1.15.1 — see the note on merged images below)
- `sonos-controller-nightly-v1.1.6-nightly.abc1234.zip` - Individual binaries
- `firmware.bin` - Application binary
- `bootloader.bin` - Bootloader
- `partitions.bin` - Partition table

### Step 8: Test OTA Update

1. On your device, go to **Settings → Update**
2. Select **Nightly** channel from dropdown
3. Tap **Check for Updates**
4. Should show: `Latest (Nightly): v1.1.6-nightly.abc1234 (prerelease)`
5. Tap **Install Update** to test OTA

---

## Creating a Stable Release

Stable releases are for production deployment and trigger automatically.

### When to Create a Stable Release

- Feature is complete and tested
- All tests pass
- Ready for end users
- After successful nightly testing (optional)

### Step 1: Bump to Stable Version

Choose the appropriate version bump:

```bash
# Patch release (1.1.6 -> 1.1.7) - Bug fixes
python scripts/bump_version.py patch

# Minor release (1.1.6 -> 1.2.0) - New features (backward compatible)
python scripts/bump_version.py minor

# Major release (1.1.6 -> 2.0.0) - Breaking changes
python scripts/bump_version.py major

# Or specify exact version
python scripts/bump_version.py 1.2.0
```

**Output:**
```
Current version: 1.1.6-nightly.abc1234
New version: 1.2.0

[SUCCESS] Version bumped to 1.2.0

[STABLE] Next steps:
  1. Build and test locally
  2. git add -A && git commit -m "v1.2.0: <description>"
  3. git push origin main
  4. Auto-release workflow will trigger automatically

[NOTE] Pushing version.json without '-nightly' triggers stable release
```

### Step 2: Test Locally (Recommended)

```bash
pio run
```

### Step 3: Commit and Push

```bash
git add -A
git commit -m "v1.2.0: Add dual OTA channels and fast boot"
git push origin main
```

**What happens automatically:**
1. **Auto Release** workflow triggers
2. **Build Firmware** workflow triggers
3. **Deploy Web Installer** workflow triggers

All workflows detect stable version and proceed with build and deployment.

### Step 4: Monitor Auto-Release Workflow

```bash
# Watch the auto-release workflow
gh run list --workflow=auto-release.yml
gh run watch
```

**Workflow Steps:**
1. Read version from `version.json`
2. Check if nightly (skip if nightly)
3. Check if release already exists
4. Build firmware with PlatformIO
5. Create merged firmware binary
6. Create release ZIP
7. Create GitHub release (NOT marked as prerelease)

**Expected Duration:** ~5 minutes

### Step 5: Verify Release

```bash
# View the release
gh release view v1.2.0

# List all releases
gh release list
```

**Release should contain (both screens — built by `auto-release.yml`):**
- `firmware-4inch.bin`, `firmware-7inch.bin` — per-screen app binaries
- ~~`firmware-merged-*.bin`~~ — **removed in v1.15.1.** Built by `esptool merge-bin` at `0x0 bootloader / 0x8000 partitions / 0x10000 firmware`, which is wrong twice: the P4 ROM reads the bootloader at **0x2000**, and **boot_app0.bin at 0xe000 was omitted** — the same omission fixed for the web installer, and which `deploy-pages.yml` now guards against. Nothing consumed them (the installer flashes the multi-part manifest) and they had 0 downloads on every release. If a single-file image is needed again, do **not** reintroduce esptool: PlatformIO already emits `firmware.factory.bin` with the correct layout on every build.
- `bootloader.bin`, `partitions.bin` — shared (board-level)
- `sonos-controller-firmware-4inch-v1.2.0.zip`, `…-7inch-v1.2.0.zip`
- ~~`firmware.bin`, `firmware-merged.bin`~~ — legacy 4″ aliases, **removed in v1.15.1** (transition complete; 0 downloads). Note `firmware.bin` in a **nightly** release is not legacy: it is the asset the Nightly OTA channel installs.
- Auto-generated release notes

### Step 6: Test OTA Update

1. On your device, go to **Settings → Update**
2. Select **Stable** channel from dropdown
3. Tap **Check for Updates**
4. Should show: `Latest (Stable): v1.2.0`
5. Tap **Install Update**

---

## Transitioning from Nightly to Stable

Common workflow: Test with nightly, then promote to stable.

### Scenario: You've tested v1.1.6-nightly.abc1234 and want to release v1.2.0

**Current state:**
- `version.json`: `1.1.6-nightly.abc1234`
- Last commit: "chore: Nightly build 1.1.6-nightly.abc1234"

**Steps:**

1. **Bump to stable version:**
   ```bash
   python scripts/bump_version.py 1.2.0
   ```

2. **Commit with release notes:**
   ```bash
   git add -A
   git commit -m "v1.2.0: Add dual OTA channels and NVS device caching

   Features:
   - Dual OTA channels (Stable/Nightly) with UI selector
   - Fast boot via NVS caching (~2s vs ~15s)
   - Dark theme dropdown styling
   - Commit SHA based nightly versioning

   Fixes:
   - Workflow filtering for nightly releases
   - OTA channel tag validation
   "
   ```

3. **Push to trigger auto-release:**
   ```bash
   git push origin main
   ```

4. **Monitor release:**
   ```bash
   gh run watch
   ```

5. **Verify both channels work:**
   - **Stable channel:** Should show `v1.2.0`
   - **Nightly channel:** Should still show last nightly or "No nightly releases found"

---

## OTA Update Channels

### How Channels Work

**Stable Channel:**
- Uses GitHub API: `/releases/latest`
- Filters out versions with `-nightly` in tag name
- Only shows production releases

**Nightly Channel:**
- Uses GitHub API: `/releases?per_page=1`
- Filters to only show versions with `-nightly` in tag name
- Shows most recent prerelease

### User Experience

Users can switch channels in **Settings → Update → Release Channel** dropdown:
- **Stable** (default): Production releases only
- **Nightly**: Latest test builds

Channel preference is saved to NVS (persists across reboots).

---

## Troubleshooting

### Problem: Nightly release shows in Stable channel

**Cause:** Release was created with `prerelease: false`

**Fix:**
```bash
# Delete the incorrectly marked release
gh release delete v1.1.6-nightly.abc1234 --yes

# Recreate with correct settings
python scripts/create_nightly.py
```

### Problem: `scripts/create_nightly.py` fails - "gh not found"

**Cause:** GitHub CLI not installed or not in PATH

**Fix:**
1. Install from https://cli.github.com/
2. Authenticate: `gh auth login`
3. Verify: `gh --version`

### Problem: Stable release not triggering

**Check:**
1. Version doesn't contain `-nightly`
   ```bash
   cat version.json
   # Should be: {"version": "1.2.0"}
   # NOT: {"version": "1.2.0-nightly.abc1234"}
   ```

2. Workflow logs:
   ```bash
   gh run list --workflow=auto-release.yml
   gh run view <run-id>
   ```

### Problem: Release already exists error

**Cause:** Trying to create duplicate release

**Fix:**
```bash
# Delete existing release
gh release delete v1.2.0 --yes

# Push again to re-trigger
git commit --amend --no-edit
git push -f origin main
```

### Problem: OTA shows "No stable releases found"

**Cause:** No stable releases exist yet, only nightly releases

**Solution:** Create your first stable release following the [Creating a Stable Release](#creating-a-stable-release) guide.

---

## Quick Reference

### Version Bump Commands

```bash
python scripts/bump_version.py patch      # 1.1.6 -> 1.1.7
python scripts/bump_version.py minor      # 1.1.6 -> 1.2.0
python scripts/bump_version.py major      # 1.1.6 -> 2.0.0
python scripts/bump_version.py nightly    # 1.1.6 -> 1.1.6-nightly.abc1234
python scripts/bump_version.py 2.0.0      # Set specific version
```

### Release Creation

```bash
# Nightly (manual)
python scripts/bump_version.py nightly
git add -A && git commit -m "chore: Nightly build X.Y.Z-nightly.HASH"
git push origin main
python scripts/create_nightly.py

# Stable (automatic)
python scripts/bump_version.py patch
git add -A && git commit -m "vX.Y.Z: Description"
git push origin main
# Auto-release workflow triggers automatically
```

### Monitoring

```bash
gh run list                              # List all runs
gh run list --workflow=nightly-release   # List nightly runs
gh run list --workflow=auto-release      # List stable runs
gh run watch                             # Watch current run
gh release list                          # List all releases
gh release view vX.Y.Z                   # View specific release
```

---

## File Structure

```
SonosESP/
├── version.json                          # Version source of truth
├── bump_version.py                       # Version bump script (updates both manifests)
├── create_nightly.py                     # Nightly release trigger
├── platformio.ini                        # esp32_4inch + esp32_7inch envs
├── web-installer/
│   ├── manifest-4inch.json               # 4" installer manifest (canonical)
│   ├── manifest-7inch.json               # 7" installer manifest (BETA)
├── include/
│   └── ui_common.h                       # Firmware version constant
└── .github/workflows/
    ├── auto-release.yml                  # Stable release (auto) — builds BOTH screens, attaches all assets
    ├── nightly-release.yml               # Nightly release (manual, 4" only)
    ├── build.yml                         # CI verification (push/PR) — matrix builds both screens
    └── deploy-pages.yml                  # Web installer deployment — builds both, publishes both binaries
```

---

## Workflow Decision Tree

```
Push to main
    │
    ├─ version.json contains "-nightly"?
    │   │
    │   ├─ YES → All auto workflows SKIP
    │   │         Manual: python scripts/create_nightly.py
    │   │         Result: Nightly prerelease created
    │   │
    │   └─ NO → All auto workflows RUN
    │            Result: Stable release created
    │                    Web installer deployed
    │                    Build artifacts uploaded
```

---

## Support

For issues or questions:
- GitHub Issues: https://github.com/OpenSurface/SonosESP/issues
- Check workflow logs: `gh run list` → `gh run view <id>`
