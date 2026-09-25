<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'

// Reads a static file, never the GoatCounter API.
//
// The country and version breakdowns need an authenticated call, and a GitHub
// Pages site is public — a token in this component would be a token on the
// internet. A scheduled workflow does the authenticated half with the key held
// as a repo secret and commits only the aggregate numbers, so this page reads a
// plain JSON file that is safe for anyone to see. See PRIVACY.md.
const stats = ref(null)
const geo = ref(null)          // world geometry, fetched separately and optional
const shown = ref(0)
const hover = ref(null)

let raf = 0

const W = 1000, H = 500        // equirectangular is exactly 2:1

// Count up once on load. Decoration only — the number is the content, so
// reduced-motion gets it immediately.
function animateTo(target) {
  const reduce = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
  if (reduce || target <= 0) { shown.value = target; return }
  const t0 = performance.now(), dur = 1100
  const step = (now) => {
    const p = Math.min(1, (now - t0) / dur)
    shown.value = Math.round(target * (1 - Math.pow(1 - p, 3)))
    if (p < 1) raf = requestAnimationFrame(step)
  }
  raf = requestAnimationFrame(step)
}

onMounted(async () => {
  try {
    const r = await fetch('/SonosESP/stats.json', { cache: 'no-cache' })
    if (!r.ok) throw new Error(String(r.status))
    stats.value = await r.json()
    animateTo(stats.value.panels ?? 0)
  } catch { /* panel stays hidden; a missing counter is not worth an error box */ }

  // World geometry is a progressive enhancement: the country list below renders
  // with or without it, so a CDN hiccup costs the map and nothing else.
  try {
    const g = await fetch(
      'https://cdn.jsdelivr.net/gh/nvkelso/natural-earth-vector@master/geojson/ne_110m_admin_0_countries.geojson',
      { cache: 'force-cache' }
    )
    if (g.ok) geo.value = await g.json()
  } catch { /* no map, no problem */ }
})

onUnmounted(() => cancelAnimationFrame(raf))

// counts keyed by ISO-3166 alpha-2, for colouring the map
const byCode = computed(() => {
  const m = new Map()
  for (const c of stats.value?.countries ?? []) m.set(c.code, c)
  return m
})

const maxCount = computed(() =>
  Math.max(1, ...(stats.value?.countries ?? []).map(c => c.count)))

// Equirectangular: longitude maps straight to x, latitude to y. No projection
// library needed, and at this scale nobody is measuring areas off it.
function toPath(geometry) {
  const rings = geometry.type === 'Polygon' ? [geometry.coordinates]
              : geometry.type === 'MultiPolygon' ? geometry.coordinates
              : []
  let d = ''
  for (const poly of rings) {
    for (const ring of poly) {
      // Ring resolution is already low at 110m; skipping alternate points on the
      // long ones keeps the DOM light without a visible difference at this size.
      const step = ring.length > 400 ? 2 : 1
      for (let i = 0; i < ring.length; i += step) {
        const [lon, lat] = ring[i]
        const x = ((lon + 180) / 360) * W
        const y = ((90 - lat) / 180) * H
        d += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + y.toFixed(1)
      }
      d += 'Z'
    }
  }
  return d
}

function codeOf(f) {
  const p = f.properties || {}
  const c = p.ISO_A2_EH || p.ISO_A2 || p.iso_a2 || ''
  return c === '-99' ? '' : c.toUpperCase()
}

// Opacity by share, floored so a single panel is still clearly visible.
function fillFor(code) {
  const hit = byCode.value.get(code)
  if (!hit) return null
  return 0.3 + 0.7 * (hit.count / maxCount.value)
}

function onEnter(f, ev) {
  const code = codeOf(f)
  const hit = byCode.value.get(code)
  if (!hit) return
  hover.value = { name: hit.name, count: hit.count, x: ev.clientX, y: ev.clientY }
}

function flag(cc) {
  if (!cc || cc.length !== 2) return '🌐'
  return String.fromCodePoint(...[...cc.toUpperCase()].map(c => 0x1f1a5 + c.charCodeAt(0)))
}

function pct(n, total) {
  return total ? Math.max(1.5, Math.round((n / total) * 100)) : 0
}
</script>

<template>
  <section v-if="stats" class="sp">
    <div class="sp-head">
      <span class="sp-num">{{ shown.toLocaleString() }}</span>
      <span class="sp-label">panels running SonosESP</span>
      <p class="sp-sub">
        Counted anonymously — each panel reports its firmware version and screen
        size once at startup, and nothing else. No identifier, no location.
        <a href="https://github.com/OpenSurface/SonosESP/blob/main/PRIVACY.md">What is sent</a>.
      </p>
    </div>

    <!-- Map. Absent until the geometry loads, and permanently absent if it fails. -->
    <div v-if="geo && stats.countries?.length" class="sp-map" @mouseleave="hover = null">
      <svg :viewBox="`0 0 ${W} ${H}`" role="img"
           aria-label="World map showing where panels are running">
        <defs>
          <radialGradient id="sp-glow" cx="50%" cy="50%" r="50%">
            <stop offset="0%"   stop-color="var(--se-gold)" stop-opacity=".35" />
            <stop offset="100%" stop-color="var(--se-gold)" stop-opacity="0" />
          </radialGradient>
        </defs>
        <g>
          <path
            v-for="(f, i) in geo.features"
            :key="i"
            :d="toPath(f.geometry)"
            :class="['sp-c', fillFor(codeOf(f)) !== null && 'sp-on']"
            :style="fillFor(codeOf(f)) !== null
                    ? { fillOpacity: fillFor(codeOf(f)) } : null"
            @mousemove="onEnter(f, $event)"
          />
        </g>
      </svg>
      <div v-if="hover" class="sp-tip"
           :style="{ left: hover.x + 'px', top: hover.y + 'px' }">
        <strong>{{ hover.name }}</strong>
        <span>{{ hover.count }} {{ hover.count === 1 ? 'panel' : 'panels' }}</span>
      </div>
    </div>

    <div class="sp-grid">
      <div v-if="stats.countries?.length" class="sp-card">
        <h3>Where they are</h3>
        <ul class="sp-list">
          <li v-for="c in stats.countries.slice(0, 10)" :key="c.code">
            <span class="sp-flag">{{ flag(c.code) }}</span>
            <span class="sp-name">{{ c.name }}</span>
            <span class="sp-bar"><i :style="{ width: pct(c.count, stats.panels) + '%' }" /></span>
            <span class="sp-n">{{ c.count }}</span>
          </li>
        </ul>
      </div>

      <div v-if="stats.versions?.length" class="sp-card">
        <h3>Firmware in the wild</h3>
        <ul class="sp-list">
          <li v-for="v in stats.versions.slice(0, 8)" :key="v.version">
            <span class="sp-name sp-mono">v{{ v.version }}</span>
            <span class="sp-bar"><i :style="{ width: pct(v.count, stats.panels) + '%' }" /></span>
            <span class="sp-n">{{ v.count }}</span>
          </li>
        </ul>
      </div>
    </div>

    <p v-if="stats.updated" class="sp-foot">
      Updated {{ new Date(stats.updated).toLocaleDateString(undefined,
        { year: 'numeric', month: 'short', day: 'numeric' }) }}
      <span v-if="stats.windowDays"> · last {{ stats.windowDays }} days</span>
    </p>
  </section>
</template>

<style scoped>
.sp { margin: 3.5rem auto; max-width: 1000px; padding: 0 16px; }

.sp-head { text-align: center; margin-bottom: 2rem; }
.sp-num {
  display: block;
  font-family: var(--se-mono);
  font-size: clamp(3rem, 12vw, 5.5rem);
  line-height: 1; font-weight: 700;
  color: var(--se-gold);
  text-shadow: 0 0 40px var(--se-accent-glow);
  font-variant-numeric: tabular-nums;
}
.sp-label {
  display: block; margin-top: .4rem;
  font-size: .95rem; letter-spacing: .14em; text-transform: uppercase;
  color: var(--vp-c-text-2);
}
.sp-sub {
  margin: 1rem auto 0; max-width: 58ch;
  font-size: .88rem; line-height: 1.6; color: var(--vp-c-text-3);
}
.sp-sub a { color: var(--se-accent-text); }

/* ── Map ─────────────────────────────────────────────────────────────── */
.sp-map {
  position: relative;
  margin: 0 0 1.5rem;
  border: 1px solid var(--vp-c-divider);
  border-radius: 16px;
  background:
    radial-gradient(120% 90% at 50% 0%, var(--se-accent-wash), transparent 70%),
    var(--vp-c-bg-soft);
  overflow: hidden;
}
.sp-map svg { display: block; width: 100%; height: auto; }

.sp-c {
  fill: rgba(242, 236, 228, .06);
  stroke: rgba(242, 236, 228, .10);
  stroke-width: .4;
  vector-effect: non-scaling-stroke;
  transition: fill-opacity .2s ease;
}
.sp-c.sp-on {
  fill: var(--se-gold);
  stroke: var(--se-accent-hover);
  stroke-width: .6;
  filter: drop-shadow(0 0 4px var(--se-accent-glow));
  cursor: default;
}
.sp-c.sp-on:hover { fill-opacity: 1 !important; }

.sp-tip {
  position: fixed;
  transform: translate(-50%, calc(-100% - 12px));
  pointer-events: none;
  z-index: 40;
  display: grid; gap: 2px;
  padding: .45rem .7rem;
  border-radius: 9px;
  background: var(--vp-c-bg-elv);
  border: 1px solid var(--se-accent-dim);
  box-shadow: 0 8px 28px rgba(0, 0, 0, .55);
  white-space: nowrap;
}
.sp-tip strong { font-size: .84rem; color: var(--vp-c-text-1); }
.sp-tip span   { font-size: .76rem; color: var(--se-accent-text); font-family: var(--se-mono); }

/* ── Cards ───────────────────────────────────────────────────────────── */
.sp-grid { display: grid; gap: 1rem; grid-template-columns: 1fr; }
@media (min-width: 720px) { .sp-grid { grid-template-columns: 1fr 1fr; } }

.sp-card {
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-divider);
  border-radius: 14px;
  padding: 1.25rem 1.35rem;
}
.sp-card h3 {
  margin: 0 0 1rem; border: 0;
  font-size: .78rem; letter-spacing: .12em; text-transform: uppercase;
  color: var(--vp-c-text-3); font-weight: 600;
}

.sp-list { list-style: none; margin: 0; padding: 0; display: grid; gap: .6rem; }
.sp-list li {
  display: grid;
  grid-template-columns: auto minmax(0, 8rem) 1fr auto;
  align-items: center; gap: .6rem; font-size: .9rem;
}
.sp-list li:has(.sp-mono) { grid-template-columns: minmax(0, 8rem) 1fr auto; }

.sp-flag { font-size: 1.05rem; line-height: 1; }
.sp-name { color: var(--vp-c-text-1); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.sp-mono { font-family: var(--se-mono); font-size: .84rem; }

.sp-bar { height: 6px; border-radius: 3px; background: var(--vp-c-gutter); overflow: hidden; }
.sp-bar i {
  display: block; height: 100%; border-radius: 3px;
  background: linear-gradient(90deg, var(--se-accent-dim), var(--se-gold));
  animation: sp-grow .9s cubic-bezier(.22, .9, .3, 1) both;
}
@keyframes sp-grow { from { transform: scaleX(0); transform-origin: left; } }
@media (prefers-reduced-motion: reduce) {
  .sp-bar i { animation: none; }
  .sp-c { transition: none; }
}

.sp-n { font-family: var(--se-mono); font-size: .82rem; color: var(--vp-c-text-2); font-variant-numeric: tabular-nums; }

.sp-foot { margin-top: 1.25rem; text-align: center; font-size: .78rem; color: var(--vp-c-text-3); }
</style>
