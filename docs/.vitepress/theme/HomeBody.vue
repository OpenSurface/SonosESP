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
    d: 'Five clock faces with a six-hour forecast, three player themes, and auto-dimming when you walk away.' },
]

// Photos people have sent in of their own panels. Empty by design: these are
// other people's builds, so nothing goes in here that was not actually submitted
// and credited. To add one, drop the file in docs/public/community/ and append
// { src, who, note } below - the section hides itself while the list is empty.
const community: { src: string; who: string; note?: string }[] = []

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

    <!--
      What you actually need to buy. The three steps below open with "plug it in",
      which quietly assumes you already own a panel - this is the piece that was
      missing, and the render shows the board is a finished product rather than a
      bare PCB you have to assemble.
    -->
    <section class="hardware">
      <div class="hw-shot">
        <img :src="withBase('/panel.png')"
             alt="GUITION JC4880P433C ESP32-P4 touchscreen panel, shown front and back"
             width="636" height="608" loading="lazy" />
      </div>
      <div class="hw-copy">
        <p class="kicker">What you need</p>
        <h2>One off-the-shelf panel</h2>
        <p>
          A GUITION ESP32-P4 touchscreen and a USB-C cable. That is the whole bill
          of materials — no soldering, no breakout boards, no case to print. It
          arrives as a finished unit running a demo launcher, and SonosESP replaces
          that.
        </p>
        <ul class="chips">
          <li>4″ JC4880P433C</li>
          <li>7″ JC1060P470C</li>
          <li>USB-C</li>
        </ul>
        <a class="link" :href="withBase('/guide/hardware')">Which one to buy →</a>
      </div>
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

    <section v-if="community.length" class="community">
      <div class="com-head">
        <p class="kicker">Community builds</p>
        <h2>Where people put theirs</h2>
      </div>
      <ul class="com-grid">
        <li v-for="c in community" :key="c.src">
          <img :src="withBase(c.src)" :alt="'SonosESP panel built by ' + c.who"
               loading="lazy" />
          <p class="com-cap"><strong>{{ c.who }}</strong><span v-if="c.note"> · {{ c.note }}</span></p>
        </li>
      </ul>
    </section>

    <section class="support">
      <div>
        <h2>Free, and staying that way</h2>
        <p>
          SonosESP is MIT licensed and built in spare time. If it earns a place on
          your wall, a coffee helps pay for the panels that get tested to breaking
          point so yours doesn't.
        </p>
      </div>
      <a class="kofi" href="https://ko-fi.com/pizzapasta" target="_blank" rel="noopener">
        Support on Ko-fi
      </a>
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

/* ---- hardware ---- */
/* Image first in the source so it leads on narrow screens - the render is the
   point of this section, and a wall of text above it buries it. */
.hardware {
  display: grid; grid-template-columns: minmax(0,1fr) minmax(0,1.1fr); gap: 40px;
  align-items: center; padding: 36px 40px; border-radius: 20px; margin-bottom: 20px;
  border: 1px solid var(--vp-c-divider);
  background:
    radial-gradient(110% 130% at 100% 0%, color-mix(in srgb, #2f4d86 12%, transparent), transparent 60%),
    var(--vp-c-bg-soft);
}
.hw-shot img { display: block; width: 100%; height: auto; }
.hardware h2 { margin: 0 0 14px; font-size: clamp(1.4rem, 2.5vw, 1.9rem); line-height: 1.15; letter-spacing: -.025em; border: 0; padding: 0; }
.hardware p { margin: 0 0 18px; color: var(--vp-c-text-2); line-height: 1.65; }
.hardware .chips { margin-bottom: 18px; }

/* ---- community ---- */
.community { margin-bottom: 20px; }
.com-head { margin-bottom: 22px; }
.community h2 { margin: 0; border: 0; padding: 0; font-size: clamp(1.4rem, 2.4vw, 1.8rem); letter-spacing: -.02em; }
.com-grid {
  list-style: none; margin: 0; padding: 0;
  display: grid; grid-template-columns: repeat(auto-fill, minmax(16rem, 1fr)); gap: 18px;
}
.com-grid li { margin: 0; }
.com-grid img {
  display: block; width: 100%; aspect-ratio: 4 / 3; object-fit: cover;
  border-radius: 14px; border: 1px solid var(--vp-c-divider); background: var(--vp-c-bg-soft);
}
.com-cap { margin: 10px 2px 0; font-size: .85rem; color: var(--vp-c-text-3); line-height: 1.5; }
.com-cap strong { color: var(--vp-c-text-2); font-weight: 600; }

@media (max-width: 860px) {
  .lead-card { grid-template-columns: 1fr; gap: 26px; padding: 30px; }
  .hardware { grid-template-columns: 1fr; gap: 26px; padding: 30px; }
  .steps { padding: 34px 24px; }
}
@media (prefers-reduced-motion: reduce) { .card, .btn { transition: none; } }

.support {
  display: flex; align-items: center; justify-content: space-between;
  flex-wrap: wrap; gap: 24px;
  margin-top: 20px; padding: 30px 32px;
  border: 1px solid var(--vp-c-divider); border-radius: 18px;
  background: var(--vp-c-bg-soft);
}
.support h2 { margin: 0 0 8px; border: 0; padding: 0; font-size: 1.18rem; letter-spacing: -.015em; }
.support p { margin: 0; max-width: 56ch; font-size: .92rem; line-height: 1.6; color: var(--vp-c-text-2); }
.kofi {
  flex-shrink: 0;
  display: inline-block; padding: 11px 22px; border-radius: 999px;
  background: #ff5e5b; color: #fff; font-weight: 600; font-size: .92rem;
  text-decoration: none; white-space: nowrap;
  transition: transform .18s ease, box-shadow .18s ease;
}
.kofi:hover { transform: translateY(-2px); box-shadow: 0 8px 20px rgba(255, 94, 91, .32); }
@media (prefers-reduced-motion: reduce) { .kofi { transition: none; } .kofi:hover { transform: none; } }
@media (max-width: 620px) { .support { flex-direction: column; align-items: flex-start; } }
</style>
