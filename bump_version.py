#!/usr/bin/env python3
"""
Version Bump Script for SonosESP
Updates version in all required files automatically.

Usage:
    python bump_version.py patch    # 1.0.9 -> 1.0.10
    python bump_version.py minor    # 1.0.9 -> 1.1.0
    python bump_version.py major    # 1.0.9 -> 2.0.0
    python bump_version.py nightly  # 1.1.6 -> 1.1.6-nightly.abc1234
    python bump_version.py 1.2.3    # Set specific version
"""

import json
import re
import subprocess
import sys
from pathlib import Path

# Files that contain version strings
VERSION_FILES = {
    'version.json': {
        'type': 'json',
        'key': 'version'
    },
    'web-installer/manifest.json': {
        'type': 'json',
        'key': 'version'
    },
    'include/ui_common.h': {
        'type': 'regex',
        'pattern': r'#define FIRMWARE_VERSION "([^"]+)"',
        'replacement': '#define FIRMWARE_VERSION "{version}"'
    }
}

def get_current_version():
    """Read current version from version.json"""
    with open('version.json', 'r') as f:
        data = json.load(f)
        return data['version']

def get_git_commit_sha():
    """Get current git commit SHA (short form)"""
    try:
        result = subprocess.run(['git', 'rev-parse', '--short=7', 'HEAD'],
                              capture_output=True,
                              text=True,
                              check=True)
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None

def parse_version(version_str):
    """Parse version string into tuple of ints (strips -nightly suffix)"""
    # Remove -nightly suffix if present
    base = version_str.split('-nightly')[0]
    parts = base.split('.')
    return [int(p) for p in parts]

def bump_version(current, bump_type):
    """Calculate new version based on bump type"""
    # Strip any -nightly suffix from current version
    base_version = current.split('-nightly')[0]

    if bump_type == 'nightly':
        # Generate nightly version with commit SHA
        commit_sha = get_git_commit_sha()
        if not commit_sha:
            print("[ERROR] Could not get git commit SHA for nightly version")
            print("[INFO] Make sure you're in a git repository with commits")
            sys.exit(1)
        return f"{base_version}-nightly.{commit_sha}"

    parts = parse_version(base_version)

    if bump_type == 'major':
        parts[0] += 1
        parts[1] = 0
        parts[2] = 0
    elif bump_type == 'minor':
        parts[1] += 1
        parts[2] = 0
    elif bump_type == 'patch':
        parts[2] += 1
    else:
        # Assume it's a specific version string
        return bump_type

    return '.'.join(str(p) for p in parts)

def update_json_file(filepath, key, new_version):
    """Update version in a JSON file"""
    with open(filepath, 'r') as f:
        data = json.load(f)

    data[key] = new_version

    with open(filepath, 'w') as f:
        json.dump(data, f, indent=2)
        f.write('\n')

    print(f"  [OK] {filepath}")

def update_regex_file(filepath, pattern, replacement, new_version):
    """Update version in a file using regex"""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    new_content = re.sub(pattern, replacement.format(version=new_version), content)

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(new_content)

    print(f"  [OK] {filepath}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python bump_version.py [patch|minor|major|nightly|X.Y.Z]")
        print("\nExamples:")
        print("  python bump_version.py patch    # 1.0.9 -> 1.0.10")
        print("  python bump_version.py minor    # 1.0.9 -> 1.1.0")
        print("  python bump_version.py major    # 1.0.9 -> 2.0.0")
        print("  python bump_version.py nightly  # 1.1.6 -> 1.1.6-nightly.abc1234")
        print("  python bump_version.py 2.0.0    # Set to 2.0.0")
        sys.exit(1)

    bump_type = sys.argv[1].lower()

    # Get current version
    current_version = get_current_version()
    print(f"\nCurrent version: {current_version}")

    # Calculate new version
    new_version = bump_version(current_version, bump_type)
    print(f"New version: {new_version}\n")

    # Update all files
    print("Updating files:")
    for filepath, config in VERSION_FILES.items():
        path = Path(filepath)
        if not path.exists():
            print(f"  [SKIP] {filepath} (not found)")
            continue

        if config['type'] == 'json':
            update_json_file(filepath, config['key'], new_version)
        elif config['type'] == 'regex':
            update_regex_file(filepath, config['pattern'], config['replacement'], new_version)

    print(f"\n[SUCCESS] Version bumped to {new_version}")

    # Different instructions for nightly vs stable
    if '-nightly' in new_version:
        print("\n[NIGHTLY] Next steps:")
        print(f"  1. Build and test locally: pio run")
        print(f"  2. git add -A && git commit -m \"chore: Nightly build {new_version}\"")
        print(f"  3. git push origin main")
        print(f"  4. python create_nightly.py  # Creates GitHub nightly release")
        print(f"\n[NOTE] Nightly versions won't trigger auto-release workflow")
    else:
        print("\n[STABLE] Next steps:")
        print(f"  1. Build and test locally")
        print(f"  2. git add -A && git commit -m \"v{new_version}: <description>\"")
        print(f"  3. git push origin main")
        print(f"  4. Auto-release workflow will trigger automatically")
        print(f"\n[NOTE] Pushing version.json without '-nightly' triggers stable release")

if __name__ == '__main__':
    main()
