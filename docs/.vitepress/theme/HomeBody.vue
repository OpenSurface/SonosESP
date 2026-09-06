<script setup lang="ts">
import { ref, computed } from 'vue'
import { withBase } from 'vitepress'

const sources = ['Spotify', 'YouTube Music', 'Apple Music', 'TuneIn', 'Amazon Music', 'Your NAS']

const cards = [
  { n: '01', wide: true, t: 'Browse your whole library',
    d: 'Artists, albums, genres, composers and tracks, plus saved playlists, internet radio, your NAS shares, the queue and line-in. The list comes from your speaker, so you only see what you actually have.' },
  { n: '02', t: 'Synced lyrics',
    d: 'Time-synced and following the track, colour-matched to the artwork, and out of the way on songs that have none.' },
  { n: '03', t: 'Album art, properly',
    d: 'Decoded by the P4’s hardware JPEG unit, with the dominant colour pulled out to tint the rest of the screen.' },
  { n: '04', t: 'Multi-room and groups',
    d: 'Switch zones with live indicators for what is playing where. Create and break speaker groups from the panel.' },
  { n: '05', t: 'Clock, weather, themes',
    d: 'Five clock faces with a six-hour forecast, three player themes, and auto-dimming when you walk away.' },
]

// The toggle swaps the spec row. Values match docs/guide/hardware.md.
// Prices are a rough guide checked Sept 2026 and they move, so the wording is
// "about" and the link is a SEARCH by part number, not a listing URL -
// individual AliExpress listings go dead constantly, a search does not.
const panel = ref<4 | 7>(4)
const spec = computed(() => panel.value === 4
  ? { part: 'JC4880P443C', screen: '4″ · 800×480', price: '~$32' }
  : { part: 'JC1060P470C', screen: '7″ · 1024×600', price: '~$37' })
const buyUrl = computed(
  () => `https://www.aliexpress.com/w/wholesale-${spec.value.part}.html`)

const steps = [
  { n: '01', t: 'Plug it in',   d: 'USB-C to your computer. Chrome, Edge or Opera.' },
  { n: '02', t: 'Click install', d: 'Pick your screen size, pick the port. About a minute.' },
  { n: '03', t: 'Join Wi-Fi',   d: 'Type the password on the panel, scan for speakers, done.' },
]

// Real builds submitted through the showcase issue template, mirrored from the
// README block. Photos are hosted here rather than hotlinked from GitHub's
// attachment CDN, and each keeps the credit and issue link it came with.
const community = [
  { src: '/community/brennan-b3.jpg',  t: 'Living room · Brennan B3 Jukebox',
    gear: 'Sonos Beam 2 + 2 Symfonisk frames', who: '@johnhenrick3-cpu',
    issue: 'https://github.com/OpenSurface/SonosESP/issues/97',
    alt: '4-inch SonosESP with a Brennan B3 jukebox' },
  { src: '/community/kitchen-7in.jpg', t: 'Kitchen table · 7″ variant (beta)',
    gear: 'Sonos Move 2', who: '@johnhenrick3-cpu',
    issue: 'https://github.com/OpenSurface/SonosESP/issues/95',
    alt: '7-inch SonosESP variant on a kitchen table' },
  { src: '/community/brennan-b2.jpg',  t: 'Living room · Brennan B2 Jukebox',
    gear: 'Sonos Era 300', who: '@johnhenrick3-cpu',
    issue: 'https://github.com/OpenSurface/SonosESP/issues/96',
    alt: '4-inch SonosESP with a Brennan B2 jukebox' },
  { src: '/community/bedside.jpg',     t: 'Bedside table',
    gear: 'Sonos Ray + 2 Symfonisk lamps', who: '@johnhenrick3-cpu',
    issue: 'https://github.com/OpenSurface/SonosESP/issues/94',
    alt: '4-inch SonosESP on a bedside table' },
]

const showcaseUrl =
  'https://github.com/OpenSurface/SonosESP/issues/new?template=showcase.yml'
</script>

<template>
  <!-- ---- sources band ---- -->
  <section class="band">
    <div class="wrap split">
      <div class="col-a">
        <p class="eyebrow muted">The bit people ask about</p>
        <h2 class="h2 narrow">Anything in your Sonos favourites plays from the panel</h2>
        <p class="body">
          Whatever service it came from. Spotify playlists, YouTube Music mixes, radio
          stations, all of it. You never sign into anything on the device — your speaker
          already holds the accounts and does the work.
        </p>
        <a class="ulink" :href="withBase('/guide/sources')">How sources work &#8594;</a>
      </div>
      <div class="chips">
        <div v-for="s in sources" :key="s" class="chip">{{ s }}</div>
      </div>
    </div>
  </section>

  <!-- ---- feature bento ---- -->
  <section id="features" class="sec">
    <div class="wrap bento">
      <article v-for="c in cards" :key="c.n" class="card" :class="{ wide: c.wide }">
        <div class="idx">{{ c.n }}</div>
        <h3>{{ c.t }}</h3>
        <p>{{ c.d }}</p>
      </article>
    </div>
  </section>

  <!-- ---- hardware ---- -->
  <section id="hardware" class="sec hw-sec">
    <div class="wrap split middle">
      <div class="col-a">
        <div class="hw-shot">
          <img :src="withBase('/panel.png')"
               alt="GUITION JC4880P443C ESP32-P4 touchscreen panel, shown front and back"
               width="636" height="608" loading="lazy" />
        </div>
      </div>
      <div class="col-b">
        <p class="eyebrow muted">What you need</p>
        <h2 class="h2">One off-the-shelf panel</h2>
        <p class="body">
          A GUITION ESP32-P4 touchscreen and a USB-C cable. That is the whole bill of
          materials — no soldering, no breakout boards, no case to print. It arrives as a
          finished unit running a demo launcher, and SonosESP replaces that.
        </p>

        <div class="seg" role="group" aria-label="Panel size">
          <button type="button" :class="{ on: panel === 4 }" :aria-pressed="panel === 4"
                  @click="panel = 4">4&#8243; panel</button>
          <button type="button" :class="{ on: panel === 7 }" :aria-pressed="panel === 7"
                  @click="panel = 7">7&#8243; panel</button>
        </div>

        <div class="specs">
          <div class="cell"><span class="k">Part</span><span class="v">{{ spec.part }}</span></div>
          <div class="cell"><span class="k">Screen</span><span class="v nowrap">{{ spec.screen }}</span></div>
          <div class="cell"><span class="k">Cable</span><span class="v">USB-C</span></div>
          <div class="cell"><span class="k">Price</span><span class="v nowrap">{{ spec.price }}</span></div>
        </div>

        <p class="pricenote">
          About <strong>$30&#8211;40</strong> shipped, depending on the seller and the day.
        </p>

        <div class="hw-links">
          <a class="ulink" :href="buyUrl" target="_blank" rel="noopener">Find it on AliExpress &#8594;</a>
          <a class="ulink" :href="withBase('/guide/hardware')">Which one to buy &#8594;</a>
        </div>
      </div>
    </div>
  </section>

  <!-- ---- three steps ---- -->
  <section id="install" class="sec">
    <div class="wrap">
      <h2 class="h2 center big">Running in three steps</h2>
      <div class="steps">
        <div v-for="s in steps" :key="s.n" class="step">
          <div class="idx">{{ s.n }}</div>
          <h3>{{ s.t }}</h3>
          <p>{{ s.d }}</p>
        </div>
      </div>
      <div class="center-row">
        <a class="btn primary" :href="withBase('/guide/install')">
          Install it now <span class="mono">&#8594;</span>
        </a>
      </div>
    </div>
  </section>

  <!-- ---- community builds ---- -->
  <section v-if="community.length" id="builds" class="sec bordered">
    <div class="wrap">
      <div class="com-head">
        <div>
          <p class="eyebrow muted">Community builds</p>
          <h2 class="h2">Where people put theirs</h2>
          <p class="body tight">Real installs, sent in by the people who made them.</p>
        </div>
        <a class="ulink" :href="showcaseUrl" target="_blank" rel="noopener">Share yours &#8594;</a>
      </div>
      <div class="com-grid">
        <figure v-for="c in community" :key="c.src">
          <a :href="c.issue" target="_blank" rel="noopener">
            <img :src="withBase(c.src)" :alt="c.alt" loading="lazy" />
          </a>
          <figcaption>
            <span class="ct">{{ c.t }}</span>
            <span class="cg">{{ c.gear }}</span>
            <span class="cw">{{ c.who }}</span>
          </figcaption>
        </figure>
      </div>
    </div>
  </section>

  <!-- ---- support ---- -->
  <section class="sec">
    <div class="wrap">
      <div class="support">
        <div class="sup-copy">
          <h2 class="h2 sm">Free, and staying that way</h2>
          <p class="body">
            SonosESP is MIT licensed and built in spare time. If it earns a place on your
            wall, a coffee helps pay for the panels that get tested to breaking point so
            yours doesn&#8217;t.
          </p>
        </div>
        <a class="btn kofi" href="https://ko-fi.com/pizzapasta" target="_blank" rel="noopener">
          Support on Ko-fi
        </a>
      </div>
    </div>
  </section>

  <!-- ---- footer ---- -->
  <footer class="site-foot">
    <div class="wrap foot-inner">
      <div class="foot-brand">
        <span class="fdot" aria-hidden="true"></span>
        <span class="fname">Sonos<span class="gold">ESP</span></span>
        <span class="mono lic">MIT licensed</span>
      </div>
      <div class="foot-links">
        <a :href="withBase('/guide/install')">Install</a>
        <a :href="withBase('/guide/features')">Features</a>
        <a :href="withBase('/TROUBLESHOOTING')">Troubleshooting</a>
        <a href="https://github.com/OpenSurface/SonosESP" target="_blank" rel="noopener">GitHub</a>
        <a href="https://ko-fi.com/pizzapasta" target="_blank" rel="noopener">Ko-fi</a>
      </div>
    </div>
  </footer>
</template>

<style scoped>
.wrap { max-width: 1180px; margin: 0 auto; }
.sec { padding: clamp(48px, 6vw, 88px) clamp(20px, 4vw, 40px); }
.band {
  padding: clamp(40px, 5vw, 72px) clamp(20px, 4vw, 40px);
  border-top: 1px solid rgba(242, 236, 228, .07);
}
.bordered { border-top: 1px solid rgba(242, 236, 228, .07); }

.split { display: flex; flex-wrap: wrap; gap: clamp(28px, 4vw, 56px); align-items: flex-start; }
.split.middle { align-items: center; gap: clamp(28px, 4vw, 60px); }
.col-a { flex: 1 1 400px; min-width: 0; }
.col-b { flex: 1 1 380px; min-width: 0; }

.eyebrow {
  font-family: var(--se-mono); font-size: 11.5px; letter-spacing: .2em;
  text-transform: uppercase; margin: 0 0 18px;
}
.eyebrow.muted { color: rgba(242, 236, 228, .64); }

.h2 {
  margin: 0; font-size: clamp(1.7rem, 3.2vw, 2.4rem); line-height: 1.12;
  letter-spacing: -.025em; font-weight: 600; text-wrap: balance;
  border: 0; padding: 0;
}
.h2.narrow { max-width: 24ch; }
.h2.center { text-align: center; }
.h2.big { font-size: clamp(1.8rem, 3.4vw, 2.6rem); line-height: 1.1; letter-spacing: -.03em; }
.h2.sm { font-size: clamp(1.5rem, 2.6vw, 2rem); line-height: 1.15; }

.body {
  margin: 20px 0 0; max-width: 52ch; font-size: 1rem; line-height: 1.62;
  color: rgba(242, 236, 228, .66); text-wrap: pretty;
}
.body.tight { margin-top: 14px; }

.ulink {
  display: inline-block; margin-top: 22px; font-size: 14.5px; font-weight: 500;
  color: var(--se-accent-text); text-decoration: none;
  border-bottom: 1px solid rgba(224, 178, 82, .35); padding-bottom: 2px;
}
.ulink:hover { color: var(--se-accent-hover); }

/* ---- source chips ---- */
.chips {
  flex: 1 1 300px; min-width: 0; display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 8px;
}
.chip {
  font-family: var(--se-mono); font-size: 12.5px; letter-spacing: .02em;
  color: rgba(242, 236, 228, .78); padding: 14px 16px;
  border: 1px solid rgba(242, 236, 228, .1); border-radius: 12px;
  background: rgba(242, 236, 228, .025);
}

/* ---- bento ---- */
.bento { display: flex; flex-wrap: wrap; gap: 14px; }
.card {
  flex: 1 1 260px; min-width: 0; padding: clamp(24px, 3vw, 34px);
  border: 1px solid rgba(242, 236, 228, .1); border-radius: 18px;
  background: rgba(242, 236, 228, .028);
  transition: border-color .16s ease, background .16s ease;
}
/* The wide card takes the first row with card 02; the rest wrap to a row of three. */
.card.wide { flex: 1.7 1 380px; }
.card:hover { border-color: rgba(242, 236, 228, .2); background: rgba(242, 236, 228, .045); }
.idx {
  font-family: var(--se-mono); font-size: 11px; letter-spacing: .16em;
  color: var(--se-accent-text); margin-bottom: 16px;
}
.card h3 {
  margin: 0; font-size: clamp(1.15rem, 1.8vw, 1.45rem); font-weight: 600;
  letter-spacing: -.02em; border: 0; padding: 0;
}
.card p {
  margin: 12px 0 0; font-size: .96rem; line-height: 1.6;
  color: rgba(242, 236, 228, .62); text-wrap: pretty;
}

/* ---- hardware ---- */
.hw-sec {
  border-top: 1px solid rgba(242, 236, 228, .07);
  border-bottom: 1px solid rgba(242, 236, 228, .07);
  background: rgba(242, 236, 228, .015);
}
.hw-shot img { display: block; width: 100%; height: auto; border-radius: 16px; }
.pricenote { margin: 14px 0 0; font-size: .9rem; color: rgba(242, 236, 228, .58); }
.pricenote strong { color: rgba(242, 236, 228, .82); font-weight: 600; }
.hw-links { display: flex; flex-wrap: wrap; gap: 22px; }
.hw-links .ulink { margin-top: 18px; }

.seg {
  display: inline-flex; gap: 4px; margin-top: 28px; padding: 4px;
  border-radius: 999px; border: 1px solid rgba(242, 236, 228, .12);
  background: rgba(0, 0, 0, .3);
}
.seg button {
  font-family: var(--se-mono); font-size: 12.5px; padding: 9px 18px;
  border-radius: 999px; border: 0; cursor: pointer;
  background: transparent; color: rgba(242, 236, 228, .7);
  transition: background .16s ease, color .16s ease;
}
.seg button.on { background: var(--se-ink); color: #17120f; }

.specs {
  margin-top: 20px; display: flex; flex-wrap: wrap; gap: 1px;
  background: rgba(242, 236, 228, .1);
  border: 1px solid rgba(242, 236, 228, .1); border-radius: 12px; overflow: hidden;
}
.cell {
  background: #100e0d; padding: 16px 18px; flex: 1 1 150px; min-width: 0;
  display: flex; flex-direction: column;
}
.k {
  font-family: var(--se-mono); font-size: 10.5px; letter-spacing: .14em;
  text-transform: uppercase; color: rgba(242, 236, 228, .64);
}
.v { font-family: var(--se-mono); font-size: 14px; margin-top: 8px; color: var(--se-ink); }
.nowrap { white-space: nowrap; }

/* ---- steps ---- */
.steps {
  display: flex; flex-wrap: wrap; gap: clamp(20px, 3vw, 40px);
  margin-top: clamp(32px, 4vw, 56px);
}
.step {
  flex: 1 1 240px; min-width: 0; padding-top: 24px;
  border-top: 1px solid rgba(242, 236, 228, .16);
}
.step .idx { font-size: 12px; margin-bottom: 0; }
.step h3 {
  margin: 14px 0 0; font-size: 1.2rem; font-weight: 600; letter-spacing: -.02em;
  border: 0; padding: 0;
}
.step p { margin: 10px 0 0; font-size: .96rem; line-height: 1.6; color: rgba(242, 236, 228, .62); }

.center-row { display: flex; justify-content: center; margin-top: clamp(32px, 4vw, 52px); }
.btn {
  display: inline-flex; align-items: center; gap: 10px; font-size: 15px;
  font-weight: 600; padding: 15px 28px; border-radius: 999px; text-decoration: none;
  transition: background .16s ease, transform .16s ease;
}
.btn.primary { background: var(--se-ink); color: #17120f; }
.btn.primary:hover { background: #fff; transform: translateY(-1px); }
.mono { font-family: var(--se-mono); }

/* ---- community ---- */
.com-head {
  display: flex; flex-wrap: wrap; gap: 16px;
  align-items: flex-end; justify-content: space-between;
}
.com-head .ulink { margin-top: 0; }
.com-grid {
  display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 14px; margin-top: clamp(28px, 3.5vw, 44px);
}
.com-grid figure { margin: 0; min-width: 0; }
.com-grid img {
  display: block; width: 100%; aspect-ratio: 4 / 3; object-fit: cover;
  border-radius: 14px; border: 1px solid rgba(242, 236, 228, .1);
  background: rgba(242, 236, 228, .025);
  transition: border-color .16s ease, transform .16s ease;
}
.com-grid a:hover img { border-color: rgba(242, 236, 228, .28); transform: translateY(-2px); }
.com-grid figcaption { display: flex; flex-direction: column; }
.ct { margin-top: 14px; font-size: 14.5px; font-weight: 600; letter-spacing: -.01em; color: var(--se-ink); }
.cg { margin-top: 5px; font-size: 13px; color: rgba(242, 236, 228, .55); }
.cw { margin-top: 3px; font-family: var(--se-mono); font-size: 11.5px; color: rgba(242, 236, 228, .62); }

/* ---- support ---- */
.support {
  padding: clamp(28px, 4vw, 48px); border-radius: 20px;
  border: 1px solid rgba(242, 236, 228, .1);
  background:
    radial-gradient(80% 140% at 100% 0%, rgba(224, 178, 82, .16), transparent 70%),
    rgba(242, 236, 228, .025);
  display: flex; flex-wrap: wrap; gap: 28px;
  align-items: center; justify-content: space-between;
}
.sup-copy { flex: 1 1 420px; min-width: 0; }
.sup-copy .body { max-width: 58ch; margin-top: 16px; }
.btn.kofi { background: var(--se-accent); color: #17120f; padding: 15px 26px; }
.btn.kofi:hover { background: var(--se-accent-hover); transform: translateY(-1px); }

/* ---- footer ---- */
.site-foot {
  padding: 36px clamp(20px, 4vw, 40px) 56px;
  border-top: 1px solid rgba(242, 236, 228, .07);
}
.foot-inner {
  display: flex; flex-wrap: wrap; gap: 20px; align-items: center;
  justify-content: space-between; font-size: 13.5px; color: rgba(242, 236, 228, .66);
}
.foot-brand { display: flex; align-items: center; gap: 10px; }
.fdot { width: 8px; height: 8px; border-radius: 50%; background: var(--se-accent); }
.fname { color: rgba(242, 236, 228, .75); font-weight: 600; }
.gold { color: var(--se-gold); }
.lic { font-size: 12px; }
.foot-links { display: flex; flex-wrap: wrap; gap: 20px; }
.foot-links a { color: rgba(242, 236, 228, .55); text-decoration: none; transition: color .16s ease; }
.foot-links a:hover { color: var(--se-ink); }

@media (prefers-reduced-motion: reduce) {
  .card, .btn, .seg button, .com-grid img, .foot-links a { transition: none; }
  .btn:hover, .com-grid a:hover img { transform: none; }
}
</style>
