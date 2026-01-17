#!/usr/bin/env python3
"""
Version Bump Script for SonosESP
Updates version in all required files automatically.

Usage:
    python bump_version.py patch    # 1.0.9 -> 1.0.10
    python bump_version.py minor    # 1.0.9 -> 1.1.0
    python bump_version.py major    # 1.0.9 -> 2.0.0
    python bump_version.py 1.2.3    # Set specific version
"""

import json
import re
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
    'src/main.cpp': {
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

def parse_version(version_str):
    """Parse version string into tuple of ints"""
    parts = version_str.split('.')
    return [int(p) for p in parts]

def bump_version(current, bump_type):
    """Calculate new version based on bump type"""
    parts = parse_version(current)

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
        print("Usage: python bump_version.py [patch|minor|major|X.Y.Z]")
        print("\nExamples:")
        print("  python bump_version.py patch    # 1.0.9 -> 1.0.10")
        print("  python bump_version.py minor    # 1.0.9 -> 1.1.0")
        print("  python bump_version.py major    # 1.0.9 -> 2.0.0")
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
    print("\nNext steps:")
    print(f"  1. Build and test locally")
    print(f"  2. git add -A && git commit -m \"v{new_version}: <description>\"")
    print(f"  3. git push origin main")
    print(f"  4. gh release create v{new_version} --title \"v{new_version} - <title>\" --generate-notes")

if __name__ == '__main__':
    main()
