<script setup>
import { ref, computed, onMounted } from 'vue'

// Reads a static file, never the GoatCounter API.
//
// The country and version breakdowns need an authenticated call, and a GitHub
// Pages site is public — a token in this component would be a token on the
// internet. A scheduled workflow does the authenticated half with the key held
// as a repo secret and commits only the aggregate numbers, so this page reads a
// plain JSON file that is safe for anyone to see. See PRIVACY.md.
//
// No external requests at all: the whole panel is this one same-origin fetch.
const stats = ref(null)
const rolled = ref(false)
const settled = ref(false)

// Full rotations before landing. Without these each reel travelled only as far
// as its own digit, so a 1 barely moved while a 9 spun — the giveaway that it
// is text sliding rather than a wheel turning. Every digit now covers the same
// distance and the stagger does the rest.
const SPINS = 3

onMounted(async () => {
  try {
    const r = await fetch('/SonosESP/stats.json', { cache: 'no-cache' })
    if (!r.ok) throw new Error(String(r.status))
    stats.value = await r.json()
  } catch {
    return   // panel stays hidden; a missing counter is not worth an error box
  }

  if (window.matchMedia?.('(prefers-reduced-motion: reduce)').matches) {
    rolled.value = true
    settled.value = true
    return
  }
  // Two frames: one to paint the reels at rest, one for the transition to pick
  // up the change. Rolling from a standing start is the whole effect.
  requestAnimationFrame(() => requestAnimationFrame(() => { rolled.value = true }))
  // Blur lifts as the last reel arrives, so the number sharpens into place.
  setTimeout(() => { settled.value = true }, 1500 + digits.value.length * 80)
})

const digits = computed(() =>
  (stats.value?.panels ?? 0).toLocaleString('en-US').split(''))

// SPINS full cycles of 0-9, then the target digit. Landing index is SPINS*10.
function reelFor(ch) {
  const out = []
  for (let s = 0; s < SPINS; s++) for (let n = 0; n < 10; n++) out.push(n)
  out.push(Number(ch))
  return out
}

function pct(n, total) {
  return total ? Math.max(1.5, Math.round((n / total) * 100)) : 0
}

function flag(cc) {
  if (!cc || cc.length !== 2) return '🌐'
  return String.fromCodePoint(...[...cc.toUpperCase()].map(c => 0x1f1a5 + c.charCodeAt(0)))
}
</script>

<template>
  <section v-if="stats" class="sp">
    <div class="sp-head">
      <div class="sp-odo" :class="{ 'is-settled': settled }"
           role="img" :aria-label="`${stats.panels} panels running SonosESP`">
        <template v-for="(ch, i) in digits" :key="i">
          <span v-if="ch === ','" class="sp-sep">,</span>
          <span v-else class="sp-slot">
            <span class="sp-reel"
                  :style="{
                    transform: `translateY(${rolled ? -(SPINS * 10) : 0}em)`,
                    transitionDelay: `${(digits.length - i) * 80}ms`
                  }">
              <b v-for="(n, k) in reelFor(ch)" :key="k">{{ n }}</b>
            </span>
          </span>
        </template>
      </div>
      <span class="sp-label">panels running SonosESP</span>
    </div>

    <div class="sp-grid">
      <div v-if="stats.countries?.length" class="sp-card">
        <h3>Where they are</h3>
        <ul class="sp-list">
          <li v-for="c in stats.countries.slice(0, 8)" :key="c.code">
            <span class="sp-flag">{{ flag(c.code) }}</span>
            <span class="sp-name">{{ c.name }}</span>
            <span class="sp-bar"><i :style="{ width: pct(c.count, stats.panels) + '%' }" /></span>
            <span class="sp-n">{{ c.count }}</span>
          </li>
        </ul>
      </div>

      <div v-if="stats.versions?.length" class="sp-card">
        <h3>Firmware in the wild</h3>
        <ul class="sp-list vers">
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
.sp { margin: 4rem auto; max-width: 880px; padding: 0 16px; }
.sp-head { text-align: center; margin-bottom: 2.5rem; }

/* ── Odometer ─────────────────────────────────────────────────────────── */
.sp-odo {
  display: flex;
  justify-content: center;
  align-items: flex-end;
  gap: .02em;
  font-family: var(--se-mono);
  font-weight: 700;
  font-size: clamp(3.5rem, 14vw, 6.5rem);
  line-height: 1;
  color: var(--se-gold);
  font-variant-numeric: tabular-nums;
  letter-spacing: -0.02em;
}
.sp-slot {
  position: relative;
  display: block;
  width: .60em;
  height: 1em;
  overflow: hidden;
  /* Feathered ends: a hard edge reads as clipped text, a soft one as a drum. */
  -webkit-mask-image: linear-gradient(180deg, transparent, #000 26%, #000 74%, transparent);
          mask-image: linear-gradient(180deg, transparent, #000 26%, #000 74%, transparent);
}
.sp-reel {
  display: block;
  will-change: transform, filter;
  /* Long, heavily-decelerated curve — the wheel arrives rather than stops. */
  transition: transform 1.5s cubic-bezier(.12, .78, .18, 1), filter .45s ease-out;
  filter: blur(4px);
}
.is-settled .sp-reel { filter: blur(0); }
.sp-reel b {
  display: block;
  height: 1em;
  line-height: 1;
  font-weight: 700;
  text-align: center;
}
/* Glow on the container, not each digit: on the reel it smears down the strip. */
.sp-odo { text-shadow: 0 0 44px var(--se-accent-glow); }

.sp-sep { display: block; align-self: flex-end; opacity: .4; margin: 0 -.08em; }

@media (prefers-reduced-motion: reduce) {
  .sp-reel { transition: none; filter: none; }
  .sp-slot { -webkit-mask-image: none; mask-image: none; }
}

.sp-label {
  display: block; margin-top: .8rem;
  font-size: .95rem; letter-spacing: .16em; text-transform: uppercase;
  color: var(--vp-c-text-2);
}

/* ── Cards ────────────────────────────────────────────────────────────── */
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
  grid-template-columns: auto minmax(0, 7.5rem) 1fr auto;
  align-items: center; gap: .6rem; font-size: .9rem;
}
.sp-list.vers li { grid-template-columns: minmax(0, 7.5rem) 1fr auto; }

.sp-flag { font-size: 1.05rem; line-height: 1; }
.sp-name { color: var(--vp-c-text-1); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.sp-mono { font-family: var(--se-mono); font-size: .84rem; }

.sp-bar { height: 6px; border-radius: 3px; background: var(--vp-c-gutter); overflow: hidden; }
.sp-bar i {
  display: block; height: 100%; border-radius: 3px;
  background: linear-gradient(90deg, var(--se-accent-dim), var(--se-gold));
  transform-origin: left;
  animation: sp-grow 1.1s cubic-bezier(.16, 1, .3, 1) both .5s;
}
@keyframes sp-grow { from { transform: scaleX(0); } }
@media (prefers-reduced-motion: reduce) { .sp-bar i { animation: none; } }

.sp-n { font-family: var(--se-mono); font-size: .82rem; color: var(--vp-c-text-2); font-variant-numeric: tabular-nums; }

.sp-foot { margin-top: 1.25rem; text-align: center; font-size: .78rem; color: var(--vp-c-text-3); }
</style>
