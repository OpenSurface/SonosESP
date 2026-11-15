#!/usr/bin/env python3
"""
Create Nightly Release for SonosESP
Triggers GitHub Actions workflow to build and release a nightly version.

Usage:
    python scripts/create_nightly.py                              # Use current branch + version.json
    python scripts/create_nightly.py --branch feature/my-branch  # Build from specific branch
    python scripts/create_nightly.py --version 1.2.0             # Specify base version explicitly
    python scripts/create_nightly.py --branch feature/x --version 1.3.0  # Both
"""

import json
import re
import subprocess
import sys
from pathlib import Path


def get_ota_repo():
    """The repo the firmware pulls OTA updates from (GITHUB_REPO in ui_common.h).

    Nightlies must be published THERE, or the devices never see them. Read it
    from the header rather than repeating it, so a fork changes it in one place.
    """
    text = Path('include/ui_common.h').read_text()
    m = re.search(r'#define\s+GITHUB_REPO\s+"([^"]+)"', text)
    if not m:
        print("[ERROR] GITHUB_REPO not found in include/ui_common.h")
        sys.exit(1)
    return m.group(1)


REPO = get_ota_repo()


def get_repo_remote():
    """Name of the git remote pointing at REPO (a fork has origin = upstream)."""
    try:
        out = subprocess.run(['git', 'remote', '-v'], capture_output=True,
                             text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return 'origin'
    for line in out.splitlines():
        name, url = line.split()[:2]
        if re.search(rf'[:/]{re.escape(REPO)}(\.git)?$', url):
            return name
    return 'origin'

def get_current_version():
    """Read current version from version.json"""
    with open('version.json', 'r') as f:
        data = json.load(f)
        return data['version']

def get_current_branch():
    """Get the current git branch name"""
    try:
        result = subprocess.run(['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
                              capture_output=True, text=True, check=True)
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None

def get_git_commit_sha(branch=None):
    """Get git commit SHA (short form) for a branch, or HEAD if not specified"""
    try:
        ref = f'{get_repo_remote()}/{branch}' if branch else 'HEAD'
        result = subprocess.run(['git', 'rev-parse', '--short=7', ref],
                              capture_output=True,
                              text=True,
                              check=True)
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None

def generate_nightly_tag(base_version, branch=None):
    """Generate nightly tag: X.Y.Z-nightly.COMMITHASH"""
    # Remove any existing -nightly suffix
    if '-nightly' in base_version:
        base_version = base_version.split('-nightly')[0]

    # Get git commit SHA (from branch tip if specified, else local HEAD)
    commit_sha = get_git_commit_sha(branch)
    if not commit_sha:
        if branch:
            print(f"[ERROR] Could not get commit SHA for branch '{branch}'")
            print(f"[INFO] Make sure the branch is pushed: git push {get_repo_remote()} {branch}")
        else:
            print("[ERROR] Could not get git commit SHA")
            print("[INFO] Make sure you're in a git repository")
        sys.exit(1)

    return f"{base_version}-nightly.{commit_sha}"

def check_gh_cli():
    """Check if GitHub CLI (gh) is installed"""
    try:
        subprocess.run(['gh', '--version'],
                       capture_output=True,
                       text=True,
                       check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def trigger_workflow(nightly_tag, branch=None):
    """Trigger the nightly-release workflow using GitHub CLI"""
    print(f"\n[INFO] Triggering nightly release workflow...")
    print(f"[INFO] Tag:    v{nightly_tag}")
    print(f"[INFO] Repo:   {REPO}")
    print(f"[INFO] Branch: {branch or 'main (default)'}")

    # --repo explicitly: with an upstream remote next to the fork, gh may
    # otherwise resolve to the upstream repo.
    cmd = ['gh', 'workflow', 'run', 'nightly-release.yml', '--repo', REPO,
           '-f', f'version_tag={nightly_tag}']
    if branch:
        cmd += ['--ref', branch]

    try:
        # Trigger workflow with version_tag input (and optional branch ref)
        subprocess.run(cmd, capture_output=True, text=True, check=True)

        print(f"\n[SUCCESS] Nightly release workflow triggered!")
        print(f"\n[INFO] Monitor progress:")
        print(f"  gh run list --repo {REPO} --workflow=nightly-release.yml")
        print(f"  gh run watch")
        print(f"\n[INFO] Or visit: https://github.com/{REPO}/actions")

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
    branch = None
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] in ['-h', '--help']:
            print(__doc__)
            sys.exit(0)
        elif args[i] == '--version' and i + 1 < len(args):
            base_version = args[i + 1]
            i += 2
        elif args[i] == '--branch' and i + 1 < len(args):
            branch = args[i + 1]
            i += 2
        else:
            i += 1

    # Default branch to current local branch if not specified
    if not branch:
        branch = get_current_branch()
        if branch and branch != 'main':
            print(f"\n[INFO] Auto-detected branch: {branch}")
            print(f"[INFO] (use --branch main to override)")

    # Normalize: treat 'main' as no explicit branch (workflow default)
    if branch == 'main':
        branch = None

    # Get base version
    if not base_version:
        base_version = get_current_version()
        print(f"\n[INFO] Using version from version.json: {base_version}")
    else:
        print(f"\n[INFO] Using specified version: {base_version}")

    # Use version from version.json directly (don't regenerate commit hash)
    if '-nightly' in base_version:
        nightly_tag = base_version
        print(f"[INFO] Using nightly version as-is: v{nightly_tag}")
    else:
        # If stable version, generate nightly tag from branch tip
        nightly_tag = generate_nightly_tag(base_version, branch)
        print(f"[INFO] Generated nightly tag: v{nightly_tag}")

    # Check for GitHub CLI
    if not check_gh_cli():
        print(f"\n[ERROR] GitHub CLI (gh) not found!")
        print(f"[INFO] Install it from: https://cli.github.com/")
        print(f"\n[MANUAL] To create nightly release manually:")
        print(f"  1. Go to: https://github.com/{REPO}/actions/workflows/nightly-release.yml")
        print(f"  2. Click 'Run workflow'")
        print(f"  3. Enter version tag: {nightly_tag}")
        print(f"     Format: X.Y.Z-nightly.COMMITHASH (7 hex chars)")
        print(f"  4. Click 'Run workflow'")
        sys.exit(1)

    # Confirm
    # Get commit info for display
    try:
        commit_msg = subprocess.run(['git', 'log', '-1', '--pretty=%s'],
                                   capture_output=True, text=True, check=True).stdout.strip()
        commit_author = subprocess.run(['git', 'log', '-1', '--pretty=%an'],
                                      capture_output=True, text=True, check=True).stdout.strip()
    except:
        commit_msg = "Unknown"
        commit_author = "Unknown"

    print(f"\n[CONFIRM] This will create a nightly prerelease:")
    print(f"  - Tag:    v{nightly_tag}")
    print(f"  - Branch: {branch or 'main (default)'}")
    print(f"  - Commit: {commit_msg}")
    print(f"  - Author: {commit_author}")
    print(f"  - Marked as: Prerelease (unstable)")
    print(f"\nContinue? [y/N]: ", end='')

    response = input().strip().lower()
    if response not in ['y', 'yes']:
        print("[CANCELLED] Nightly release creation cancelled.")
        sys.exit(0)

    # Trigger workflow
    if trigger_workflow(nightly_tag, branch):
        print(f"\n[NEXT STEPS]")
        print(f"  1. Wait for GitHub Actions to build (~5 minutes)")
        print(f"  2. Nightly release will appear at:")
        print(f"     https://github.com/{REPO}/releases/tag/v{nightly_tag}")
        print(f"  3. Test OTA update with 'Nightly' channel selected")
        print(f"\n[NOTE] This is a prerelease and won't appear in Stable channel")
    else:
        print(f"\n[FAILED] Could not trigger workflow. See error above.")
        sys.exit(1)

if __name__ == '__main__':
    main()
