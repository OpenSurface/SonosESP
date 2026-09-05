#!/usr/bin/env python3
"""
ui_lint.py — static consistency checks for the LVGL UI.

Why this exists
---------------
The UI is 19 screen/face build functions across two panel sizes. Auditing it by
flashing means ~38 renders before you even get to states (dropdown open, list
empty, slider at minimum, text too long), so defects that are obvious in a
screenshot survive for releases. These checks are the subset that can be decided
from the source alone.

What it CANNOT do: layout. Overflow, misalignment, truncation and overlap need a
renderer. Those stay a simulator job. Everything here is "this widget was built
differently from its neighbours", which is what actually drives the drift.

Usage:
    python tools/ui_lint.py            # report
    python tools/ui_lint.py --strict   # exit 1 if anything is found (CI)
"""

import os
import re
import sys
from collections import defaultdict

SRC_DIRS = ['src']
PALETTE_FILE = os.path.join('src', 'ui_globals.cpp')

# Files allowed to hold raw colour literals: the palette itself, and the theme
# registries whose whole purpose is to name per-theme colours.
COLOUR_LITERAL_ALLOWED = {
    os.path.normpath('src/ui_globals.cpp'),
    os.path.normpath('src/ui_theme.cpp'),
}

# Geometry setters whose pixel arguments must go through SX()/SY()/SMIN().
GEOMETRY_CALLS = [
    'lv_obj_set_width', 'lv_obj_set_height', 'lv_obj_set_size',
    'lv_obj_set_x', 'lv_obj_set_y', 'lv_obj_set_pos',
    'lv_obj_align', 'lv_obj_align_to',
]

# Numbers that are fine unscaled anywhere: identity/zero, and the 1-2px hairlines
# used for borders and underlines that should NOT scale.
GEOMETRY_EXEMPT = {'0', '1', '2', '-1', '-2'}


def source_files():
    for d in SRC_DIRS:
        for root, _, files in os.walk(d):
            for f in files:
                if f.endswith(('.cpp', '.h')):
                    yield os.path.normpath(os.path.join(root, f))


def palette_values():
    """Hex values that ARE the palette, so uses of them elsewhere are just drift."""
    vals = {}
    if not os.path.exists(PALETTE_FILE):
        return vals
    with open(PALETTE_FILE, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = re.search(r'(COL_[A-Z0-9_]+)\s*=\s*lv_color_hex\((0x[0-9A-Fa-f]{6})\)', line)
            if m:
                vals[m.group(2).lower()] = m.group(1)
    return vals


def strip_comment(line):
    i = line.find('//')
    return line[:i] if i >= 0 else line


def check_colours(findings, palette):
    # 0x000000 is a genuine universal here - shadows, scrims and the letterbox
    # behind album art - not palette drift. Flagging 14 of them buries the signal.
    UNIVERSAL = {'0x000000'}

    hits = []
    for path in source_files():
        if path in COLOUR_LITERAL_ALLOWED:
            continue
        with open(path, encoding='utf-8', errors='replace') as fh:
            for n, raw in enumerate(fh, 1):
                line = strip_comment(raw)
                for m in re.finditer(r'lv_color_hex\((0x[0-9A-Fa-f]{6})\)', line):
                    hits.append((path, n, m.group(1).lower()))

    counts = defaultdict(int)
    for _, _, val in hits:
        counts[val] += 1

    for path, n, val in hits:
        named = palette.get(val)
        if named:
            findings['colour-duplicates-palette'].append(
                (path, n, '%s is %s - use the token' % (val, named)))
        elif val in UNIVERSAL:
            continue
        elif counts[val] > 1:
            findings['colour-repeated-unnamed'].append(
                (path, n, '%s used %d times - a de-facto palette member, name it'
                 % (val, counts[val])))
        else:
            findings['colour-one-off'].append(
                (path, n, '%s used once - check it is deliberate' % val))


def check_geometry(findings):
    call_re = re.compile(r'\b(' + '|'.join(GEOMETRY_CALLS) + r')\s*\(')
    for path in source_files():
        with open(path, encoding='utf-8', errors='replace') as fh:
            for n, raw in enumerate(fh, 1):
                line = strip_comment(raw)
                m = call_re.search(line)
                if not m:
                    continue
                args = line[m.end():]
                # Drop anything already wrapped in a scaling macro or a percentage.
                cleaned = re.sub(r'\b(SX|SY|SMIN|SETTINGS_LIST_H)\s*\([^)]*\)', '', args)
                cleaned = re.sub(r'lv_pct\s*\([^)]*\)', '', cleaned)
                cleaned = re.sub(r'LV_(SIZE_CONTENT|PCT|ALIGN_[A-Z_]+)', '', cleaned)
                bare = [t for t in re.findall(r'(?<![\w.])(-?\d+)(?![\w.])', cleaned)
                        if t not in GEOMETRY_EXEMPT]
                if bare:
                    findings['geometry-unscaled'].append(
                        (path, n, '%s( ... %s ) - wrap in SX()/SY()/SMIN()'
                         % (m.group(1), ', '.join(bare))))


def check_parts(findings):
    """Theme-sensitive widget parts that are never styled inherit LVGL's defaults."""
    for path in source_files():
        with open(path, encoding='utf-8', errors='replace') as fh:
            text = fh.read()
        lines = text.split('\n')

        # Objects handed back by createSettingsSidebar() already have their
        # scrollbar styled there (ui_sidebar.cpp), once, for every settings screen.
        # Without this the rule fires on every screen that opts its content area
        # into a scrollbar, which is a false positive, not a leak.
        centrally_styled = set()
        for raw in lines:
            m = re.search(r'(\w+)\s*=\s*createSettingsSidebar\s*\(', strip_comment(raw))
            if m:
                centrally_styled.add(m.group(1))

        # A visible scrollbar with no LV_PART_SCROLLBAR styling anywhere in the file.
        if 'LV_PART_SCROLLBAR' not in text:
            for n, raw in enumerate(lines, 1):
                line = strip_comment(raw)
                if re.search(r'LV_SCROLLBAR_MODE_(AUTO|ON)', line):
                    m = re.search(r'lv_obj_set_scrollbar_mode\s*\(\s*(\w+)', line)
                    if m and m.group(1) in centrally_styled:
                        continue
                    findings['unstyled-scrollbar'].append(
                        (path, n, 'scrollbar can show but LV_PART_SCROLLBAR is never styled'))

        # A dropdown list with no LV_PART_SELECTED styling: the open list's
        # highlighted row falls back to the light default.
        if 'lv_dropdown_create' in text and 'LV_PART_SELECTED' not in text:
            for n, raw in enumerate(lines, 1):
                if 'lv_dropdown_create' in strip_comment(raw):
                    findings['unstyled-dropdown-selection'].append(
                        (path, n, 'dropdown built but LV_PART_SELECTED is never styled'))


# Usable inner height of the settings content area, in design pixels. Must track
# SETTINGS_INNER_H in include/ui_settings_card.h.
SETTINGS_INNER_H = 432


def check_content_overflow(findings):
    """A child positioned + sized past the settings content area's inner height.

    This is the defect that clipped the last row of four different lists: the
    author subtracted from the content's OUTER height (480) and forgot the 24px
    padding at top and bottom, so every one of them ended at 455 inside a 432-tall
    box. Cheap to check, and it is the one layout class decidable from source.
    """
    pos_re = re.compile(r'lv_obj_set_pos\s*\(\s*(\w+)\s*,[^,]+,\s*SY\(\s*(\d+)\s*\)')
    size_re = re.compile(r'lv_obj_set_size\s*\(\s*(\w+)\s*,[^,]+,\s*SY\(\s*(\d+)\s*\)')
    sidebar_re = re.compile(r'(\w+)\s*=\s*createSettingsSidebar\s*\(')
    child_re = re.compile(r'(\w+)\s*=\s*lv_\w+_create\s*\(\s*(\w+)\s*\)')
    for path in source_files():
        with open(path, encoding='utf-8', errors='replace') as fh:
            lines = [strip_comment(l) for l in fh]
        if 'createSettingsSidebar' not in ''.join(lines):
            continue

        # Only objects parented to the sidebar's content area live in the padded
        # 432-tall box. ui_settings_screens.cpp also builds the full-screen Queue
        # directly on scr_queue, which is 480 tall with no padding - checking that
        # against 432 is a false positive, not a defect.
        content_vars, in_content = set(), set()
        for line in lines:
            m = sidebar_re.search(line)
            if m:
                content_vars.add(m.group(1))
            m = child_re.search(line)
            if m and m.group(2) in content_vars:
                in_content.add(m.group(1))

        tops, sizes = {}, {}
        for n, line in enumerate(lines, 1):
            m = pos_re.search(line)
            if m and m.group(1) in in_content:
                tops[m.group(1)] = (n, int(m.group(2)))
            m = size_re.search(line)
            if m and m.group(1) in in_content:
                sizes[m.group(1)] = (n, int(m.group(2)))
        for var, (n, h) in sizes.items():
            if var not in tops:
                continue
            top = tops[var][1]
            if top + h > SETTINGS_INNER_H:
                findings['content-overflow'].append(
                    (path, n, '%s: top %d + height %d = %d, past the %d inner height '
                              '- use SETTINGS_LIST_H(%d)'
                     % (var, top, h, top + h, SETTINGS_INNER_H, top)))


def check_helper_bypass(findings):
    """Settings screens hand-rolling a widget that already has a shared helper."""
    helpers = {
        'lv_slider_create': 'addSlider()',
        'lv_switch_create': 'addSwitch()',
    }
    for path in source_files():
        base = os.path.basename(path)
        # Only the settings screens share the card helpers; the player themes
        # deliberately style their own transport sliders.
        if not (base.startswith('ui_') and
                any(k in base for k in ('display', 'general', 'clock_settings', 'wifi', 'ota'))):
            continue
        with open(path, encoding='utf-8', errors='replace') as fh:
            for n, raw in enumerate(fh, 1):
                line = strip_comment(raw)
                for call, helper in helpers.items():
                    if call in line:
                        findings['helper-bypassed'].append(
                            (path, n, '%s in a settings screen - use %s' % (call, helper)))


def main():
    strict = '--strict' in sys.argv
    palette = palette_values()
    findings = defaultdict(list)

    check_colours(findings, palette)
    check_geometry(findings)
    check_parts(findings)
    check_content_overflow(findings)
    check_helper_bypass(findings)

    order = [
        ('colour-duplicates-palette', 'Literal repeats a palette colour'),
        ('colour-repeated-unnamed',   'Repeated literal that should be a token'),
        ('unstyled-scrollbar',        'Visible scrollbar, never styled (light-theme leak)'),
        ('unstyled-dropdown-selection', 'Dropdown selection never styled (light-theme leak)'),
        ('content-overflow',          'Child overflows the settings content area'),
        ('helper-bypassed',           'Shared helper bypassed'),
        ('geometry-unscaled',         'Raw pixels, will not scale to the 7in panel'),
        ('colour-one-off',            'One-off literal (informational)'),
    ]

    total = 0
    print('Palette: %d named colours\n' % len(palette))
    for key, title in order:
        rows = findings[key]
        total += len(rows)
        print('%-4d %s' % (len(rows), title))
        by_file = defaultdict(list)
        for path, n, msg in rows:
            by_file[path].append((n, msg))
        for path in sorted(by_file):
            hits = by_file[path]
            shown = hits[:6]
            print('       %s (%d)' % (path.replace('\\', '/'), len(hits)))
            for n, msg in shown:
                print('         :%-5d %s' % (n, msg))
            if len(hits) > len(shown):
                print('         ... %d more' % (len(hits) - len(shown)))
        print()

    # Informational categories never fail the build: a one-off literal may be a
    # deliberate semantic colour (the WHO UV index scale, a per-theme backdrop).
    # Gating CI on those would train people to ignore the linter.
    INFORMATIONAL = {'colour-one-off'}
    actionable = sum(len(findings[k]) for k, _ in order if k not in INFORMATIONAL)

    print('TOTAL: %d finding(s) - %d actionable, %d informational'
          % (total, actionable, total - actionable))
    if strict and actionable:
        print('FAIL: %d actionable finding(s)' % actionable)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
