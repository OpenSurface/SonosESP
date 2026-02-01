#!/usr/bin/env python3
"""
Create Nightly Release for SonosESP
Triggers GitHub Actions workflow to build and release a nightly version.

Usage:
    python create_nightly.py                 # Use current version from version.json
    python create_nightly.py --version 1.2.0 # Specify base version explicitly
"""

import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path

def get_current_version():
    """Read current version from version.json"""
    with open('version.json', 'r') as f:
        data = json.load(f)
        return data['version']

def generate_nightly_tag(base_version):
    """Generate nightly tag: X.Y.Z-nightly.YYYYMMDD"""
    # Remove any existing -nightly suffix
    if '-nightly' in base_version:
        base_version = base_version.split('-nightly')[0]

    # Get today's date in YYYYMMDD format
    date_suffix = datetime.now().strftime("%Y%m%d")

    return f"{base_version}-nightly.{date_suffix}"

def check_gh_cli():
    """Check if GitHub CLI (gh) is installed"""
    try:
        result = subprocess.run(['gh', '--version'],
                              capture_output=True,
                              text=True,
                              check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def trigger_workflow(nightly_tag):
    """Trigger the nightly-release workflow using GitHub CLI"""
    print(f"\n[INFO] Triggering nightly release workflow...")
    print(f"[INFO] Tag: v{nightly_tag}")

    try:
        # Trigger workflow with version_tag input
        result = subprocess.run([
            'gh', 'workflow', 'run', 'nightly-release.yml',
            '-f', f'version_tag={nightly_tag}'
        ], capture_output=True, text=True, check=True)

        print(f"\n[SUCCESS] Nightly release workflow triggered!")
        print(f"\n[INFO] Monitor progress:")
        print(f"  gh run list --workflow=nightly-release.yml")
        print(f"  gh run watch")
        print(f"\n[INFO] Or visit: https://github.com/OpenSurface/SonosESP/actions")

        return True
    except subprocess.CalledProcessError as e:
        print(f"\n[ERROR] Failed to trigger workflow:")
        print(f"  {e.stderr}")
        return False

def main():
    print("=" * 60)
    print("SonosESP Nightly Release Creator")
    print("=" * 60)

    # Parse command line args
    base_version = None
    if len(sys.argv) > 1:
        if sys.argv[1] in ['-h', '--help']:
            print(__doc__)
            sys.exit(0)
        if sys.argv[1] == '--version' and len(sys.argv) > 2:
            base_version = sys.argv[2]

    # Get base version
    if not base_version:
        base_version = get_current_version()
        print(f"\n[INFO] Using version from version.json: {base_version}")
    else:
        print(f"\n[INFO] Using specified version: {base_version}")

    # Generate nightly tag
    nightly_tag = generate_nightly_tag(base_version)
    print(f"[INFO] Generated nightly tag: v{nightly_tag}")

    # Check for GitHub CLI
    if not check_gh_cli():
        print(f"\n[ERROR] GitHub CLI (gh) not found!")
        print(f"[INFO] Install it from: https://cli.github.com/")
        print(f"\n[MANUAL] To create nightly release manually:")
        print(f"  1. Go to: https://github.com/OpenSurface/SonosESP/actions/workflows/nightly-release.yml")
        print(f"  2. Click 'Run workflow'")
        print(f"  3. Enter version tag: {nightly_tag}")
        print(f"  4. Click 'Run workflow'")
        sys.exit(1)

    # Confirm
    print(f"\n[CONFIRM] This will create a nightly prerelease:")
    print(f"  - Tag: v{nightly_tag}")
    print(f"  - Branch: (current)")
    print(f"  - Marked as: Prerelease (unstable)")
    print(f"\nContinue? [y/N]: ", end='')

    response = input().strip().lower()
    if response not in ['y', 'yes']:
        print("[CANCELLED] Nightly release creation cancelled.")
        sys.exit(0)

    # Trigger workflow
    if trigger_workflow(nightly_tag):
        print(f"\n[NEXT STEPS]")
        print(f"  1. Wait for GitHub Actions to build (~5 minutes)")
        print(f"  2. Nightly release will appear at:")
        print(f"     https://github.com/OpenSurface/SonosESP/releases/tag/v{nightly_tag}")
        print(f"  3. Test OTA update with 'Nightly' channel selected")
        print(f"\n[NOTE] This is a prerelease and won't appear in Stable channel")
    else:
        print(f"\n[FAILED] Could not trigger workflow. See error above.")
        sys.exit(1)

if __name__ == '__main__':
    main()
