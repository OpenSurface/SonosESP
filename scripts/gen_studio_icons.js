/**
 * Studio icon font generator.
 *
 * Rasterises the icon <symbol> definitions from the Claude Design canvas
 * ("SonosESP Studio.dc.html" and "SonosESP Boot + Screensaver.dc.html") into
 * LVGL 4bpp font C files, one per size.
 *
 * ── Why rasterise instead of building a TTF ─────────────────────────────────
 * The rest of this project's fonts come from lv_font_conv, which takes a TTF.
 * That route does not work here: the design's icons are STROKED paths, and a
 * font stores filled outlines only, so every icon would need stroke-to-outline
 * conversion before it could become a glyph — with round caps and joins that is
 * exactly the step that loses fidelity.
 *
 * resvg renders the strokes properly. An LVGL glyph is just an alpha bitmap, so
 * the rendered alpha channel IS the glyph, and the result is pixel-identical to
 * the canvas. Text colour still tints it, so every existing
 * lv_obj_set_style_text_color() call keeps working.
 *
 * ── Output format ───────────────────────────────────────────────────────────
 * Same shape as what lv_font_conv emits for this project (see lv_font_mdi_16.c),
 * but 8bpp rather than 4bpp, uncompressed. adv_w is in 1/16 px.
 *
 * 8bpp is not gratuitous. These are thin STROKED icons — a 1.7-unit stroke on a
 * 24 viewBox is about 1.1px at 16px — so almost every pixel of an edge is a
 * partial-coverage value. 4bpp quantises that to 16 levels, which is what made
 * the first version look grainy and "like an image" rather than a crisp glyph.
 * At 8bpp each glyph is one byte per pixel, so rows are naturally byte-aligned
 * and no bit packing is needed.
 *
 * Each icon is also rendered at SUPERSAMPLE x the target and box-filtered down,
 * which resolves sub-pixel stroke geometry (a 1.1px line lands on a real pixel
 * boundary rather than being guessed by the rasteriser at final size).
 *
 * Usage:  node scripts/gen_studio_icons.js <canvas-dir> [outdir]
 *   <canvas-dir> holds the two .dc.html files.
 *   Default outdir is src/fonts.
 */

const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');

// ── Sizes ───────────────────────────────────────────────────────────────────
// The UI tier mirrors the MDI sizes the call sites already ask for. The weather
// tier is only the three sky glyphs, at the two sizes the screensaver uses.
const UI_SIZES = [16, 24, 32, 40];
const SUPERSAMPLE = 4;
const WX_SIZES = [32, 64];
const WX_ICONS = ['sc-cloud', 'sc-rain', 'sc-sun'];

// Codepoints start at the top of the BMP Private Use Area. Deliberately clear of
// the MDI range (U+F0000+) and of Latin-Ext, so a Studio font can carry a text
// fallback later without colliding.
const CP_BASE = 0xE000;

// ── Symbol extraction ───────────────────────────────────────────────────────
function extractSymbols(html) {
  const out = new Map();
  const re = /<symbol\s+id="([^"]+)"([^>]*)>([\s\S]*?)<\/symbol>/g;
  let m;
  while ((m = re.exec(html)) !== null) {
    out.set(m[1], { attrs: m[2], body: m[3] });
  }
  return out;
}

// A <symbol> carries its presentation attributes (fill, stroke, stroke-width,
// linecap, linejoin) and they inherit into its children, so they must be copied
// onto the standalone <svg> wrapper or the icon renders as a black blob.
function toStandaloneSvg(sym, px) {
  // Only the alpha channel is kept, so paint everything opaque white and let
  // currentColor resolve to it.
  const attrs = sym.attrs.replace(/currentColor/g, '#ffffff');
  const body = sym.body.replace(/currentColor/g, '#ffffff');
  const vb = /viewBox="([^"]+)"/.exec(attrs);
  const viewBox = vb ? vb[1] : '0 0 24 24';
  const rest = attrs.replace(/viewBox="[^"]+"/, '');
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}" ` +
         `width="${px}" height="${px}" ${rest}>${body}</svg>`;
}

// ── Rasterise to an alpha bitmap ────────────────────────────────────────────
function rasterise(svg, px) {
  const big = px * SUPERSAMPLE;
  const r = new Resvg(svg, {
    fitTo: { mode: 'width', value: big },
    background: 'rgba(0,0,0,0)',
    shapeRendering: 2,   // geometricPrecision
    imageRendering: 0,
  });
  const img = r.render();
  const bw = img.width, bh = img.height;
  const rgba = img.pixels;

  // Box-filter down to the target. Averaging SUPERSAMPLE^2 coverage samples per
  // output pixel gives a far more even edge than asking the rasteriser for a
  // 16px icon directly, where a ~1px stroke straddles pixel centres.
  const width = Math.round(bw / SUPERSAMPLE);
  const height = Math.round(bh / SUPERSAMPLE);
  const alpha = new Uint8Array(width * height);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let sum = 0, n = 0;
      for (let sy = 0; sy < SUPERSAMPLE; sy++) {
        const by = y * SUPERSAMPLE + sy;
        if (by >= bh) break;
        for (let sx = 0; sx < SUPERSAMPLE; sx++) {
          const bx = x * SUPERSAMPLE + sx;
          if (bx >= bw) break;
          sum += rgba[(by * bw + bx) * 4 + 3];
          n++;
        }
      }
      alpha[y * width + x] = n ? Math.round(sum / n) : 0;
    }
  }
  return { alpha, width, height };
}

// Trim fully transparent rows/columns; LVGL stores a box plus an offset, so the
// blank margin around an icon costs nothing but bytes.
function crop(a) {
  const { alpha, width, height } = a;
  let x0 = width, y0 = height, x1 = -1, y1 = -1;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      if (alpha[y * width + x] !== 0) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    }
  }
  if (x1 < 0) return { data: new Uint8Array(0), w: 0, h: 0, ox: 0, oy: 0 };
  const w = x1 - x0 + 1, h = y1 - y0 + 1;
  const data = new Uint8Array(w * h);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++)
      data[y * w + x] = alpha[(y + y0) * width + (x + x0)];
  return { data, w, h, ox: x0, oy: y0 };
}

// 8bpp: one byte per pixel, straight through.
function pack8bpp(data) {
  return Array.from(data);
}

// ── Emit one font ───────────────────────────────────────────────────────────
function emitFont(name, px, icons, symbols) {
  const glyphs = [];
  const bitmap = [];
  const lines = [];

  for (const id of icons) {
    const sym = symbols.get(id);
    if (!sym) throw new Error('symbol not found: ' + id);
    const raster = rasterise(toStandaloneSvg(sym, px), px);
    const c = crop(raster);
    const bytes = pack8bpp(c.data);

    // LVGL's y offset is measured UP from the baseline. The baseline sits at the
    // bitmap's bottom edge (base_line 0), so a glyph cropped from the top of a
    // px-tall box sits (px - oy - h) above it.
    const ofs_y = px - c.oy - c.h;
    glyphs.push({
      id,
      bitmap_index: bitmap.length,
      adv_w: px * 16,          // 1/16 px units; icons advance their full box
      box_w: c.w, box_h: c.h,
      ofs_x: c.ox, ofs_y,
    });

    lines.push(`    /* U+${(CP_BASE + icons.indexOf(id)).toString(16).toUpperCase()} "${id}" */`);
    for (let i = 0; i < bytes.length; i += 16) {
      lines.push('    ' + bytes.slice(i, i + 16).map(b => '0x' + b.toString(16).padStart(2, '0')).join(', ') + ',');
    }
    lines.push('');
    bitmap.push(...bytes);
  }

  const UP = name.toUpperCase();
  let s = '';
  s += '/*******************************************************************************\n';
  s += ` * Size: ${px} px\n * Bpp: 8\n`;
  s += ' * GENERATED by scripts/gen_studio_icons.js from the Claude Design canvas.\n';
  s += ' * Do not edit by hand — rerun the generator instead.\n';
  s += ' ******************************************************************************/\n\n';
  s += '#ifdef __has_include\n    #if __has_include("lvgl.h")\n        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n            #define LV_LVGL_H_INCLUDE_SIMPLE\n        #endif\n    #endif\n#endif\n';
  s += '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif\n\n';
  s += `#ifndef ${UP}\n#define ${UP} 1\n#endif\n\n#if ${UP}\n\n`;

  s += '/*-----------------\n *    BITMAPS\n *----------------*/\n\n';
  s += 'static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n';
  s += lines.join('\n');
  s += '};\n\n';

  s += '/*---------------------\n *  GLYPH DESCRIPTION\n *--------------------*/\n\n';
  s += 'static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n';
  s += '    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */';
  for (const g of glyphs) {
    s += `,\n    {.bitmap_index = ${g.bitmap_index}, .adv_w = ${g.adv_w}, .box_w = ${g.box_w}, .box_h = ${g.box_h}, .ofs_x = ${g.ofs_x}, .ofs_y = ${g.ofs_y}}`;
  }
  s += '\n};\n\n';

  s += '/*---------------------\n *  CHARACTER MAPPING\n *--------------------*/\n\n';
  // One contiguous PUA run, so the cheapest cmap format applies.
  s += 'static const lv_font_fmt_txt_cmap_t cmaps[] =\n{\n    {\n';
  s += `        .range_start = ${CP_BASE}, .range_length = ${icons.length}, .glyph_id_start = 1,\n`;
  s += '        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n';
  s += '    }\n};\n\n';

  s += '/*--------------------\n *  ALL CUSTOM DATA\n *--------------------*/\n\n';
  s += '#if LVGL_VERSION_MAJOR == 8\nstatic  lv_font_fmt_txt_glyph_cache_t cache;\n#endif\n\n';
  s += '#if LVGL_VERSION_MAJOR >= 8\nstatic const lv_font_fmt_txt_dsc_t font_dsc = {\n#else\nstatic lv_font_fmt_txt_dsc_t font_dsc = {\n#endif\n';
  s += '    .glyph_bitmap = glyph_bitmap,\n    .glyph_dsc = glyph_dsc,\n    .cmaps = cmaps,\n';
  s += '    .kern_dsc = NULL,\n    .kern_scale = 0,\n    .cmap_num = 1,\n    .bpp = 8,\n';
  s += '    .kern_classes = 0,\n    .bitmap_format = 0,\n';
  s += '#if LVGL_VERSION_MAJOR == 8\n    .cache = &cache\n#endif\n};\n\n';

  s += '/*-----------------\n *  PUBLIC FONT\n *----------------*/\n\n';
  s += `#if LVGL_VERSION_MAJOR >= 8\nconst lv_font_t ${name} = {\n#else\nlv_font_t ${name} = {\n#endif\n`;
  s += '    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n';
  s += '    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n';
  s += `    .line_height = ${px},\n    .base_line = 0,\n`;
  s += '#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)\n    .subpx = LV_FONT_SUBPX_NONE,\n#endif\n';
  s += '#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8\n    .underline_position = 0,\n    .underline_thickness = 0,\n#endif\n';
  s += '    .dsc = &font_dsc,\n';
  s += '#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9\n    .fallback = NULL,\n#endif\n';
  s += '    .user_data = NULL,\n};\n\n';
  s += `#endif /*#if ${UP}*/\n`;
  return { source: s, glyphs, bytes: bitmap.length };
}

// ── Main ────────────────────────────────────────────────────────────────────
const canvasDir = process.argv[2];
const outDir = process.argv[3] || path.join('src', 'fonts');
if (!canvasDir) {
  console.error('usage: node scripts/gen_studio_icons.js <canvas-dir> [outdir]');
  process.exit(1);
}

const symbols = new Map();
for (const f of fs.readdirSync(canvasDir)) {
  if (!f.endsWith('.html')) continue;
  const html = fs.readFileSync(path.join(canvasDir, f), 'utf8');
  for (const [k, v] of extractSymbols(html)) symbols.set(k, v);
}

const uiIcons = [...symbols.keys()].filter(k => k.startsWith('ic-')).sort();
const wxIcons = WX_ICONS.filter(k => symbols.has(k));
const scExtra = [...symbols.keys()].filter(k => k.startsWith('sc-') && !WX_ICONS.includes(k)).sort();
// The non-weather sc-* glyphs (pause, check) belong with the UI set — they are
// used at UI sizes, not at the weather sizes.
const uiSet = uiIcons.concat(scExtra);

if (uiSet.length === 0) { console.error('no symbols found in ' + canvasDir); process.exit(1); }

fs.mkdirSync(outDir, { recursive: true });
const manifest = [];

for (const px of UI_SIZES) {
  const name = `lv_font_studio_${px}`;
  const { source, glyphs, bytes } = emitFont(name, px, uiSet, symbols);
  fs.writeFileSync(path.join(outDir, name + '.c'), source);
  console.log(`${name}.c  ${uiSet.length} glyphs, ${bytes} B`);
  if (manifest.length === 0) manifest.push(...glyphs.map((g, i) => ({ id: g.id, cp: CP_BASE + i })));
}

for (const px of WX_SIZES) {
  const name = `lv_font_studio_wx_${px}`;
  const { source, bytes } = emitFont(name, px, wxIcons, symbols);
  fs.writeFileSync(path.join(outDir, name + '.c'), source);
  console.log(`${name}.c  ${wxIcons.length} glyphs, ${bytes} B`);
}

// ── Header with the codepoint defines ───────────────────────────────────────
function utf8Escape(cp) {
  const b = [];
  if (cp < 0x80) b.push(cp);
  else if (cp < 0x800) b.push(0xC0 | (cp >> 6), 0x80 | (cp & 0x3F));
  else b.push(0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F));
  return b.map(x => '\\x' + x.toString(16).toUpperCase().padStart(2, '0')).join('');
}
function defName(id) { return 'ST_IC_' + id.replace(/^(ic|sc)-/, '').toUpperCase(); }

let h = '';
h += '/**\n * Studio icon codepoints — GENERATED by scripts/gen_studio_icons.js.\n';
h += ' * Do not edit by hand; rerun the generator against the design canvas.\n *\n';
h += ' * The glyphs are the canvas\'s own SVG icons, rasterised into LVGL fonts, so\n';
h += ' * they are the design\'s icons rather than the nearest match from an icon set.\n';
h += ' * Use them exactly like the MDI_* defines: set one as a label\'s text and pick\n';
h += ' * the matching lv_font_studio_* size as its font.\n */\n';
h += '#ifndef STUDIO_ICONS_H\n#define STUDIO_ICONS_H\n\n#include "lvgl.h"\n\n';
h += 'LV_FONT_DECLARE(lv_font_studio_16);\n';
h += 'LV_FONT_DECLARE(lv_font_studio_24);\n';
h += 'LV_FONT_DECLARE(lv_font_studio_32);\n';
h += 'LV_FONT_DECLARE(lv_font_studio_40);\n';
h += 'LV_FONT_DECLARE(lv_font_studio_wx_32);\n';
h += 'LV_FONT_DECLARE(lv_font_studio_wx_64);\n\n';
for (const m of manifest) {
  h += `#define ${defName(m.id).padEnd(20)} "${utf8Escape(m.cp)}"   // U+${m.cp.toString(16).toUpperCase()}  ${m.id}\n`;
}
h += '\n';
// The weather font carries only its three glyphs, renumbered from the base.
wxIcons.forEach((id, i) => {
  h += `#define ${('ST_WX_' + id.replace('sc-', '').toUpperCase()).padEnd(20)} "${utf8Escape(CP_BASE + i)}"   // in lv_font_studio_wx_*\n`;
});
h += '\n#endif // STUDIO_ICONS_H\n';
fs.writeFileSync(path.join('include', 'studio_icons.h'), h);
console.log('include/studio_icons.h  ' + manifest.length + ' defines');
