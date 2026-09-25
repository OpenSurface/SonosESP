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
        # doseq so a list value repeats the key, which is how include_paths is
        # passed rather than as one comma-joined string.
        url += "?" + urllib.parse.urlencode(params, doseq=True)
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {KEY}",
            "Content-Type": "application/json",
            "User-Agent": "SonosESP-stats/1.0 (+https://github.com/OpenSurface/SonosESP)",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        # Print the URL and GoatCounter's own message. The first version reported
        # only the status code, which for a 404 says nothing about whether the
        # path, the parameters or the token's permissions were at fault.
        body = ""
        try:
            body = e.read().decode()[:500]
        except Exception:  # noqa: BLE001
            pass
        print(f"  request : GET {url}", file=sys.stderr)
        print(f"  response: {e.code} {e.reason}", file=sys.stderr)
        if body:
            print(f"  body    : {body}", file=sys.stderr)
        raise


def main():
    if not KEY:
        print("GC_KEY not set", file=sys.stderr)
        return 1

    # RFC3339 date-times rounded to the hour, per the API spec: start and end are
    # declared format=date-time and documented as "should be rounded to the
    # hour". The first version sent plain YYYY-MM-DD dates.
    end = datetime.now(timezone.utc).replace(minute=0, second=0, microsecond=0)
    start = end - timedelta(days=WINDOW_DAYS)
    fmt = "%Y-%m-%dT%H:%M:%SZ"
    window = {
        "start": start.strftime(fmt),
        "end": end.strftime(fmt),
    }

    try:
        hits = api("/stats/hits", window)

        # Collect the boot paths this window actually saw, so the country
        # breakdown can be restricted to them.
        #
        # /stats/locations is site-wide by default: it reports the location of
        # EVERY request the site received, not just panel boots. Anything else
        # that touches the endpoint — a probe, a monitor, a stray fetch — lands
        # in the country list as though it were a device. include_paths exists
        # on this endpoint precisely for this, and the first version did not use
        # it, which is why a country appeared that no panel is in.
        boot_paths = [
            row.get("path", "")
            for row in hits.get("hits", [])
            if re.match(r"^/boot/v?[0-9]+\.[0-9]+\.[0-9]+/(4|7)in/?$", row.get("path", ""))
        ]

        loc_params = dict(window)
        if boot_paths:
            loc_params["path_by_name"] = "true"
            loc_params["include_paths"] = boot_paths   # urlencode(doseq) repeats the key
        countries_raw = api("/stats/locations", loc_params)

        # Raw responses to the job log, never to the published file.
        #
        # These numbers have been wrong twice in ways that only showed up after
        # publishing -- boots counted as panels, then a country breakdown that
        # included non-panel traffic. Both took a guess-and-redeploy cycle to
        # spot. Printing what the API actually returned turns the next surprise
        # into something readable in the log instead of another round trip.
        print(f"  boot paths in window : {boot_paths}", file=sys.stderr)
        print(f"  locations raw        : {json.dumps(countries_raw)[:600]}", file=sys.stderr)
        print(f"  hits raw (truncated) : {json.dumps(hits.get('hits', []))[:600]}", file=sys.stderr)
    except urllib.error.HTTPError as e:
        print(f"GoatCounter API returned {e.code}: {e.reason}", file=sys.stderr)
        return 1
    except Exception as e:  # noqa: BLE001 - any failure must not write a bad file
        print(f"GoatCounter API call failed: {e}", file=sys.stderr)
        return 1

    # Version and panel size live in the path: /boot/<version>/<4|7>in
    versions = {}
    total_hits = 0
    for row in hits.get("hits", []):
        path = row.get("path", "")
        count = int(row.get("count", 0))
        m = re.match(r"^/boot/v?([0-9]+\.[0-9]+\.[0-9]+)/(4|7)in/?$", path)
        if not m:
            continue
        total_hits += count
        versions[m.group(1)] = versions.get(m.group(1), 0) + count

    if total_hits == 0:
        print("No /boot/* hits in the window — refusing to publish a zero.",
              file=sys.stderr)
        return 1

    # The headline figure comes from the public counter, NOT from summing hits.
    #
    # /api/v0/stats/hits returns raw hits per path and the response carries no
    # per-path unique count (goatcounter.HitList has 'count' and nothing else),
    # so summing it counts BOOTS, not panels: a device that reboots three times
    # appears as three. For a number labelled "panels running SonosESP" that is
    # simply wrong, and it is why this read 8 while the README badge read 5.
    #
    # /counter/TOTAL.json gives GoatCounter's own unique-visitor count, which is
    # the closest thing to a device count available here — this site receives
    # nothing but boot pings, so a visitor is a panel. It is also exactly what
    # the README badge reads, so the badge and the page can no longer disagree.
    #
    # Still an approximation: sessions expire, so a panel seen across many weeks
    # can count more than once. The hit totals remain useful as *proportions*,
    # which is all the version and country breakdowns need them for.
    panels = total_hits   # fallback if the public counter is disabled
    try:
        req = urllib.request.Request(
            f"https://{SITE}.goatcounter.com/counter/TOTAL.json",
            headers={"User-Agent": "SonosESP-stats/1.0"},
        )
        with urllib.request.urlopen(req, timeout=20) as r:
            payload = json.loads(r.read().decode())
        # The counter returns formatted strings, e.g. {"count_unique": "1,234"}
        raw = str(payload.get("count_unique") or payload.get("count") or "")
        cleaned = re.sub(r"[^0-9]", "", raw)
        if cleaned:
            panels = int(cleaned)
    except Exception as e:  # noqa: BLE001 - falling back is correct, not fatal
        print(f"Public counter unavailable ({e}); falling back to hit total.",
              file=sys.stderr)

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
        "panels": panels,
        "boots": total_hits,
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
    print(f"Wrote {OUT} — {panels} panels ({total_hits} boots) "
          f"across {len(countries)} countries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
