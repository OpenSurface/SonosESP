<script setup>
import { ref, onMounted, onUnmounted } from 'vue'

// Reads a static file, not the GoatCounter API.
//
// The country breakdown needs an authenticated API call, and a docs site built
// by GitHub Pages is public — an API key in this component would be an API key
// on the internet. A scheduled workflow fetches the numbers with the key held as
// a repo secret and commits the result here, so this page only ever reads a
// plain JSON file that is safe for anyone to see.
const stats = ref(null)
const failed = ref(false)
const shown = ref(0)

let raf = 0

// Count up to the real figure once, on reveal. Respects reduced-motion: the
// animation is decoration, the number is the content.
function animateTo(target) {
  const reduce = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
  if (reduce || target <= 0) { shown.value = target; return }
  const t0 = performance.now()
  const dur = 1100
  const step = (now) => {
    const p = Math.min(1, (now - t0) / dur)
    // ease-out cubic — fast start, settles rather than stops dead
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
  } catch (e) {
    failed.value = true
  }
})

onUnmounted(() => cancelAnimationFrame(raf))

// Flag emoji from an ISO-3166 alpha-2 code, by offsetting into the regional
// indicator block. No image assets, no CDN, and it degrades to the letters on
// platforms without flag glyphs (notably Windows).
function flag(cc) {
  if (!cc || cc.length !== 2) return '🌐'
  return String.fromCodePoint(...[...cc.toUpperCase()].map(c => 0x1f1a5 + c.charCodeAt(0)))
}

function pct(n, total) {
  if (!total) return 0
  return Math.max(1.5, Math.round((n / total) * 100))
}
</script>

<template>
  <section v-if="stats && !failed" class="sp">
    <div class="sp-head">
      <div class="sp-count">
        <span class="sp-num">{{ shown.toLocaleString() }}</span>
        <span class="sp-label">panels running SonosESP</span>
      </div>
      <p class="sp-sub">
        Counted anonymously: each panel reports its firmware version and screen
        size once at startup, and nothing else. No identifier, no location.
        <a href="https://github.com/OpenSurface/SonosESP/blob/main/PRIVACY.md">What is sent</a>.
      </p>
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

    <p class="sp-foot" v-if="stats.updated">
      Updated {{ new Date(stats.updated).toLocaleDateString(undefined,
        { year: 'numeric', month: 'short', day: 'numeric' }) }}
    </p>
  </section>
</template>

<style scoped>
.sp { margin: 3rem auto; max-width: 960px; padding: 0 16px; }

.sp-head { text-align: center; margin-bottom: 2rem; }
.sp-count { display: flex; flex-direction: column; align-items: center; gap: .25rem; }
.sp-num {
  font-family: var(--se-mono);
  font-size: clamp(3rem, 12vw, 5.5rem);
  line-height: 1;
  font-weight: 700;
  color: var(--se-gold);
  text-shadow: 0 0 40px var(--se-accent-glow);
  font-variant-numeric: tabular-nums;
}
.sp-label {
  font-size: .95rem;
  letter-spacing: .14em;
  text-transform: uppercase;
  color: var(--vp-c-text-2);
}
.sp-sub {
  margin: 1rem auto 0;
  max-width: 56ch;
  font-size: .88rem;
  line-height: 1.6;
  color: var(--vp-c-text-3);
}
.sp-sub a { color: var(--se-accent-text); }

.sp-grid { display: grid; gap: 1rem; grid-template-columns: 1fr; }
@media (min-width: 720px) { .sp-grid { grid-template-columns: 1fr 1fr; } }

.sp-card {
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-divider);
  border-radius: 14px;
  padding: 1.25rem 1.35rem;
}
.sp-card h3 {
  margin: 0 0 1rem;
  font-size: .78rem;
  letter-spacing: .12em;
  text-transform: uppercase;
  color: var(--vp-c-text-3);
  font-weight: 600;
  border: 0;
}

.sp-list { list-style: none; margin: 0; padding: 0; display: grid; gap: .6rem; }
.sp-list li {
  display: grid;
  grid-template-columns: auto minmax(0, 8.5rem) 1fr auto;
  align-items: center;
  gap: .6rem;
  font-size: .9rem;
}
.sp-list li:has(.sp-mono) { grid-template-columns: minmax(0, 8.5rem) 1fr auto; }

.sp-flag { font-size: 1.05rem; line-height: 1; }
.sp-name { color: var(--vp-c-text-1); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.sp-mono { font-family: var(--se-mono); font-size: .84rem; }

.sp-bar { height: 6px; border-radius: 3px; background: var(--vp-c-gutter); overflow: hidden; }
.sp-bar i {
  display: block; height: 100%; border-radius: 3px;
  background: linear-gradient(90deg, var(--se-accent-dim), var(--se-gold));
  animation: sp-grow .9s cubic-bezier(.22,.9,.3,1) both;
}
@keyframes sp-grow { from { transform: scaleX(0); transform-origin: left; } }
@media (prefers-reduced-motion: reduce) { .sp-bar i { animation: none; } }

.sp-n {
  font-family: var(--se-mono); font-size: .82rem;
  color: var(--vp-c-text-2); font-variant-numeric: tabular-nums;
}

.sp-foot {
  margin-top: 1.25rem; text-align: center;
  font-size: .78rem; color: var(--vp-c-text-3);
}
</style>
