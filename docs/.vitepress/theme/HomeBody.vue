<script setup lang="ts">
import { withBase } from 'vitepress'

const cards = [
  { t: 'Browse your whole library',
    d: 'Artists, albums, genres, composers and tracks, plus saved playlists, internet radio, your NAS shares, the queue and line-in. The list comes from your speaker, so you only see what you actually have.' },
  { t: 'Synced lyrics',
    d: 'Time-synced and following the track, colour-matched to the artwork, and out of the way on songs that have none.' },
  { t: 'Album art, properly',
    d: 'Decoded by the P4\u2019s hardware JPEG unit, with the dominant colour pulled out to tint the rest of the screen.' },
  { t: 'Multi-room and groups',
    d: 'Switch zones with live indicators for what is playing where. Create and break speaker groups from the panel.' },
  { t: 'Clock, weather, themes',
    d: 'Four clock faces with a six-hour forecast, three player themes, and auto-dimming when you walk away.' },
]

const steps = [
  { n: 'Plug it in',  d: 'USB-C to your computer. Chrome, Edge or Opera.' },
  { n: 'Click install', d: 'Pick your screen size, pick the port. About a minute.' },
  { n: 'Join Wi-Fi',  d: 'Type the password on the panel, scan for speakers, done.' },
]
</script>

<template>
  <div class="body">
    <!-- headline capability gets the wide card because it is the reason to build one -->
    <section class="lead-card">
      <div class="lead-copy">
        <p class="kicker">The bit people ask about</p>
        <h2>Anything in your Sonos favourites plays from the panel</h2>
        <p>
          Whatever service it came from. Spotify playlists, YouTube Music mixes,
          radio stations, all of it. You never sign into anything on the device —
          your speaker already holds the accounts and does the work.
        </p>
        <a class="link" :href="withBase('/guide/sources')">How sources work →</a>
      </div>
      <ul class="chips">
        <li>Spotify</li><li>YouTube Music</li><li>Apple Music</li>
        <li>TuneIn</li><li>Amazon Music</li><li>Your NAS</li>
      </ul>
    </section>

    <section class="grid">
      <article v-for="c in cards" :key="c.t" class="card">
        <h3>{{ c.t }}</h3>
        <p>{{ c.d }}</p>
      </article>
    </section>

    <section class="steps">
      <h2 class="steps-title">Running in three steps</h2>
      <ol>
        <li v-for="(s, i) in steps" :key="s.n">
          <span class="num">{{ i + 1 }}</span>
          <div><strong>{{ s.n }}</strong><p>{{ s.d }}</p></div>
        </li>
      </ol>
      <a class="btn" :href="withBase('/guide/install')">Install it now</a>
    </section>
  </div>
</template>

<style scoped>
.body { max-width: 1180px; margin: 0 auto; padding: 0 24px 96px; }

.kicker {
  font-size: 12px; font-weight: 700; letter-spacing: .14em; text-transform: uppercase;
  color: var(--vp-c-text-3); margin: 0 0 12px;
}

.lead-card {
  display: grid; grid-template-columns: minmax(0,1.35fr) minmax(0,1fr); gap: 40px;
  align-items: center; padding: 40px; border-radius: 20px; margin-bottom: 20px;
  border: 1px solid var(--vp-c-divider);
  background:
    radial-gradient(120% 140% at 0% 0%, color-mix(in srgb, #d4a84b 12%, transparent), transparent 58%),
    var(--vp-c-bg-soft);
}
.lead-card h2 { margin: 0 0 14px; font-size: clamp(1.5rem, 2.6vw, 2rem); line-height: 1.15; letter-spacing: -.025em; border: 0; padding: 0; }
.lead-card p { margin: 0 0 18px; color: var(--vp-c-text-2); line-height: 1.65; }
.link { font-weight: 600; text-decoration: none; color: var(--vp-c-brand-1); }
.link:hover { text-decoration: underline; }

.chips { list-style: none; margin: 0; padding: 0; display: flex; flex-wrap: wrap; gap: 8px; }
.chips li {
  font-size: 13px; font-weight: 500; padding: 7px 14px; border-radius: 999px;
  border: 1px solid var(--vp-c-divider); background: var(--vp-c-bg);
  color: var(--vp-c-text-2);
}

.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(15rem, 1fr)); gap: 20px; margin-bottom: 20px; }
.card {
  padding: 26px; border-radius: 16px;
  border: 1px solid var(--vp-c-divider); background: var(--vp-c-bg-soft);
  transition: border-color .18s ease, transform .18s ease;
}
.card:hover { border-color: color-mix(in srgb, #d4a84b 55%, var(--vp-c-divider)); transform: translateY(-3px); }
.card h3 { margin: 0 0 9px; font-size: 1.02rem; letter-spacing: -.01em; }
.card p { margin: 0; font-size: .92rem; line-height: 1.6; color: var(--vp-c-text-2); }

.steps {
  padding: 44px 40px; border-radius: 20px; text-align: center;
  border: 1px solid var(--vp-c-divider); background: var(--vp-c-bg-soft);
}
.steps-title { margin: 0 0 30px; border: 0; padding: 0; font-size: clamp(1.4rem, 2.4vw, 1.8rem); letter-spacing: -.02em; }
.steps ol {
  list-style: none; margin: 0 0 30px; padding: 0;
  display: grid; grid-template-columns: repeat(auto-fit, minmax(14rem, 1fr)); gap: 24px; text-align: left;
}
.steps li { display: flex; gap: 14px; align-items: flex-start; }
.num {
  flex: 0 0 30px; height: 30px; border-radius: 50%;
  display: grid; place-items: center; font-size: 14px; font-weight: 700;
  background: var(--vp-c-text-1); color: var(--vp-c-bg);
}
.steps li p { margin: 4px 0 0; font-size: .9rem; color: var(--vp-c-text-2); line-height: 1.55; }
.btn {
  display: inline-flex; padding: 13px 30px; border-radius: 999px; font-weight: 600;
  text-decoration: none; background: var(--vp-c-text-1); color: var(--vp-c-bg);
  transition: transform .16s ease;
}
.btn:hover { transform: translateY(-2px); }

@media (max-width: 860px) {
  .lead-card { grid-template-columns: 1fr; gap: 26px; padding: 30px; }
  .steps { padding: 34px 24px; }
}
@media (prefers-reduced-motion: reduce) { .card, .btn { transition: none; } }
</style>
