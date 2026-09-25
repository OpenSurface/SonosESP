#!/usr/bin/env python3
"""Pull anonymous panel stats from GoatCounter into docs/public/stats.json.

Run by .github/workflows/stats.yml with GC_KEY held as a repo secret, so the
API token never reaches the published site — only the aggregate numbers do.

The firmware pings /boot/<version>/<4|7>in once per startup, so a path hit is a
panel boot and the country comes from GoatCounter's server-side IP lookup. No
identifier is involved at any point; see PRIVACY.md.

Writes a file shaped for StatsPanel.vue:

    {
      "panels":   1234,
      "updated":  "2026-09-25T04:17:00Z",
      "countries": [{"code": "CA", "name": "Canada", "count": 42}, ...],
      "versions":  [{"version": "2.1.2", "count": 900}, ...]
    }

Fails loudly on a broken response rather than writing a zeroed file: a bad
number on the homepage is worse than yesterday's number.
"""

import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone, timedelta
from pathlib import Path

SITE = os.environ.get("GC_SITE", "pizza")
KEY = os.environ.get("GC_KEY")
BASE = f"https://{SITE}.goatcounter.com/api/v0"
OUT = Path(__file__).resolve().parent.parent / "docs" / "public" / "stats.json"

# 30 days: "panels seen in the last month" is the honest reading of an
# at-boot ping. A device unplugged for six weeks is not a running panel.
WINDOW_DAYS = 30


def api(path, params=None):
    url = f"{BASE}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {KEY}",
            "Content-Type": "application/json",
            "User-Agent": "SonosESP-stats/1.0 (+https://github.com/OpenSurface/SonosESP)",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read().decode())


def main():
    if not KEY:
        print("GC_KEY not set", file=sys.stderr)
        return 1

    end = datetime.now(timezone.utc)
    start = end - timedelta(days=WINDOW_DAYS)
    window = {
        "start": start.strftime("%Y-%m-%d"),
        "end": end.strftime("%Y-%m-%d"),
    }

    try:
        hits = api("/stats/hits", window)
        countries_raw = api("/stats/locations", window)
    except urllib.error.HTTPError as e:
        print(f"GoatCounter API returned {e.code}: {e.reason}", file=sys.stderr)
        return 1
    except Exception as e:  # noqa: BLE001 - any failure must not write a bad file
        print(f"GoatCounter API call failed: {e}", file=sys.stderr)
        return 1

    # Version and panel size live in the path: /boot/<version>/<4|7>in
    versions = {}
    total = 0
    for row in hits.get("hits", []):
        path = row.get("path", "")
        count = int(row.get("count", 0))
        m = re.match(r"^/boot/v?([0-9]+\.[0-9]+\.[0-9]+)/(4|7)in/?$", path)
        if not m:
            continue
        total += count
        versions[m.group(1)] = versions.get(m.group(1), 0) + count

    if total == 0:
        print("No /boot/* hits in the window — refusing to publish a zero.",
              file=sys.stderr)
        return 1

    countries = [
        {
            "code": (c.get("id") or "").upper(),
            "name": c.get("name") or c.get("id") or "Unknown",
            "count": int(c.get("count", 0)),
        }
        for c in countries_raw.get("stats", [])
        if int(c.get("count", 0)) > 0
    ]
    countries.sort(key=lambda c: -c["count"])

    out = {
        "panels": total,
        "updated": end.replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "windowDays": WINDOW_DAYS,
        "countries": countries,
        "versions": sorted(
            ({"version": v, "count": n} for v, n in versions.items()),
            key=lambda x: -x["count"],
        ),
    }

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUT} — {total} panels across {len(countries)} countries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
