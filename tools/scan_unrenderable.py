# -*- coding: utf-8 -*-
"""
Find characters in USER-VISIBLE strings that the UI fonts cannot render.

Coverage (see the header of include/ui_fonts.h):
  - the built-in Montserrat faces carry ASCII 0x20-0x7E plus a sparse symbol set
  - lv_font_latinext_* supplements U+00A0-U+017F

Anything outside that renders as a tofu box. U+2014 EM DASH is the one that bit:
it is not Latin-1, so "Classic theme only — the others..." drew a square.

Only C/C++ string literals are scanned, and only in files that build into the
firmware. Comments are ignored: they never reach a panel.
"""
import io, os, re, sys, unicodedata

ROOTS = ['src', 'include']
SKIP_DIRS = {'fonts'}          # generated font tables are pure data
SKIP_FILES = {'stb_image.h'}

def renderable(cp):
    if 0x20 <= cp <= 0x7E:
        return True
    if 0x00A0 <= cp <= 0x017F:
        return True
    if cp in (0x00B0, 0x00B7):     # degree, middle dot - in the sparse symbol range
        return True
    if cp >= 0xE000:               # our icon PUA + the MDI range
        return True
    return False

# String literals, ignoring escaped quotes.
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
# Strip // and /* */ so prose in comments is not reported.
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)

hits = {}
for root in ROOTS:
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            if fn in SKIP_FILES or not fn.endswith(('.c', '.cpp', '.h')):
                continue
            p = os.path.join(dirpath, fn)
            try:
                src = io.open(p, encoding='utf-8').read()
            except UnicodeDecodeError:
                continue
            # Blank comments but keep line numbering intact.
            clean = COMMENT.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), src)
            for m in LIT.finditer(clean):
                lit = m.group(1)
                bad = {c for c in lit if not renderable(ord(c))}
                if not bad:
                    continue
                line = clean.count('\n', 0, m.start()) + 1
                for c in bad:
                    hits.setdefault((p, line), (lit, set()))[1].add(c)

if not hits:
    print("clean - every literal is inside the fonts' coverage")
    sys.exit(0)

print("Unrenderable characters in user-visible literals:\n")
counts = {}
for (p, line), (lit, bad) in sorted(hits.items()):
    show = lit if len(lit) <= 62 else lit[:59] + '...'
    names = ', '.join('U+%04X %s' % (ord(c), unicodedata.name(c, '?')) for c in sorted(bad))
    print('  %s:%d' % (p.replace('\\', '/'), line))
    print('      %s' % show)
    print('      -> %s' % names)
    for c in bad:
        counts[c] = counts.get(c, 0) + 1

print("\nTotals:")
for c, n in sorted(counts.items(), key=lambda kv: -kv[1]):
    print("  U+%04X %-28s %d" % (ord(c), unicodedata.name(c, '?'), n))
