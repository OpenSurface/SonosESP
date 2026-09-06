<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { withBase } from 'vitepress'

/* The hero frame shows the GIF at rest and swaps to the live panel on hover.
   Leaving the frame always returns to the GIF, and the panel resets itself to
   the player so the next hover starts clean rather than resuming on whatever
   settings page was last open. Tap does the same on pointer-less devices. */
const live = ref(false)
const hovering = ref(false)

function enter() { hovering.value = true; live.value = true }
function leave() { hovering.value = false; live.value = false }

/* focusout bubbles, and it fires with a null relatedTarget whenever the focused
   element is simply removed - which is exactly what tapping the gear does, since
   swapping to the settings view destroys the button that was just clicked. Left
   naive, that reverted to the GIF with the pointer still sitting on the panel.
   So: only surrender on focusout if the pointer is elsewhere AND focus actually
   landed outside the frame. */
function focusOut(e: FocusEvent) {
  if (hovering.value) return
  const frame = e.currentTarget as HTMLElement
  const to = e.relatedTarget as Node | null
  if (!to || !frame.contains(to)) live.value = false
}

const version = ref('')
onMounted(async () => {
  try {
    const r = await fetch(withBase('/manifest-4inch.json'))
    version.value = (await r.json()).version ?? ''
  } catch {}
})
</script>

<template>
  <!--
    The design ships its own sticky header rather than reusing VitePress's nav,
    so index.md sets navbar:false. Keeping both would have stacked two bars.
  -->
  <header class="site-head">
    <nav class="head-inner">
      <a class="brand" :href="withBase('/')">
        <!-- ESP in the firmware's gold, matching the boot wordmark. -->
        <span>Sonos<span class="gold">ESP</span></span>
      </a>
      <span class="spacer"></span>
      <div class="head-links">
        <a :href="withBase('/guide/features')">Features</a>
        <a :href="withBase('/guide/hardware')">Guide</a>
        <a :href="withBase('/TROUBLESHOOTING')">Troubleshooting</a>
        <a href="https://github.com/OpenSurface/SonosESP/releases" target="_blank" rel="noopener">
          Releases <span class="ext">&#8599;</span>
        </a>
        <a class="pill" :href="withBase('/guide/install')">Install it</a>
      </div>
    </nav>
  </header>

  <section id="top" class="hero">
    <div class="bloom" aria-hidden="true"></div>
    <div class="hero-inner">
      <div class="copy">
        <p class="eyebrow">
          ESP32-P4 · Open source
          <span v-if="version" class="ver">v{{ version }}</span>
        </p>

        <h1>Your Sonos,<br /><span class="accent">on a screen you own.</span></h1>

        <p class="lede">
          A touchscreen remote that shows album art, follows the lyrics, browses your
          whole library and plays anything you have saved — including Spotify and
          YouTube Music. It flashes from your browser in about a minute.
        </p>

        <div class="cta">
          <a class="btn primary" :href="withBase('/guide/install')">
            Install it <span class="mono">&#8594;</span>
          </a>
          <a class="btn ghost" :href="withBase('/guide/features')">See what it does</a>
        </div>

        <div class="meta">
          <span>No login on the device</span><span>·</span>
          <span>Updates over the air</span><span>·</span>
          <span>4&#8243; and 7&#8243; panels</span>
        </div>
      </div>

      <div class="media">
        <div class="frame" :class="{ live }"
             @mouseenter="enter" @mouseleave="leave"
             @focusin="enter" @focusout="focusOut" @pointerdown="enter">
          <div class="aperture">
            <!-- Both layers are always mounted: the panel needs to be measured
                 to scale itself, and swapping on hover would make it size from
                 zero on first reveal. Opacity crossfades between them. -->
            <img class="layer gif" :class="{ hide: live }"
                 :src="withBase('/sonosESP.gif')"
                 alt="SonosESP running on a GUITION ESP32-P4 touchscreen, showing album art and playback controls" />
            <div class="layer panel" :class="{ show: live }" :aria-hidden="!live">
              <PanelDemo :active="live" />
            </div>
            <span class="tryit" :class="{ hide: live }" aria-hidden="true">Hover to try it</span>
          </div>
        </div>
        <p class="cap">
          <template v-if="live">Live — tap the gear, the room pill, the queue icon or LRC</template>
          <template v-else>GUITION ESP32-P4 · 7&#8243; panel</template>
        </p>
      </div>
    </div>
  </section>
</template>

<style scoped>
/* ---- header ---- */
.site-head {
  position: sticky; top: 0; z-index: 50;
  backdrop-filter: blur(18px) saturate(1.4);
  -webkit-backdrop-filter: blur(18px) saturate(1.4);
  background: rgba(12, 11, 10, .72);
  border-bottom: 1px solid rgba(242, 236, 228, .08);
}
.head-inner {
  max-width: 1180px; margin: 0 auto; padding: 0 clamp(20px, 4vw, 40px);
  height: 62px; display: flex; align-items: center; gap: 24px;
}
.brand {
  display: flex; align-items: center; gap: 10px;
  color: var(--se-ink); font-weight: 600; letter-spacing: -.01em; font-size: 16px;
  text-decoration: none;
}
/* The firmware paints "ESP" gold on the boot wordmark; the site does the same. */
.gold { color: var(--se-gold); }
.spacer { flex: 1; }
.head-links {
  display: flex; align-items: center; gap: 4px;
  font-size: 13.5px; flex-wrap: wrap; justify-content: flex-end;
}
.head-links a {
  color: rgba(242, 236, 228, .7); padding: 7px 11px; border-radius: 8px;
  text-decoration: none; transition: color .16s ease, background .16s ease;
}
.head-links a:hover { color: var(--se-ink); background: rgba(242, 236, 228, .06); }
.ext { font-family: var(--se-mono); font-size: 11px; opacity: .6; }
.head-links a.pill {
  margin-left: 8px; color: #17120f; background: var(--se-ink);
  font-weight: 600; padding: 9px 16px; border-radius: 999px;
}
.head-links a.pill:hover { background: #fff; color: #17120f; }

/* ---- hero ---- */
.hero {
  position: relative;
  padding: clamp(56px, 8vw, 104px) clamp(20px, 4vw, 40px) clamp(48px, 6vw, 88px);
}
.bloom {
  position: absolute; inset: -10% -20% 30% -20%; pointer-events: none;
  background:
    radial-gradient(52% 60% at 22% 8%, oklch(.7 .16 40 / .2), transparent 70%),
    radial-gradient(46% 54% at 82% 4%, oklch(.74 .12 72 / .15), transparent 72%);
}
.hero-inner {
  position: relative; max-width: 1180px; margin: 0 auto;
  display: flex; flex-wrap: wrap; gap: clamp(32px, 5vw, 64px); align-items: center;
}
.copy { flex: 1 1 400px; min-width: 0; }
.media { flex: 1 1 380px; min-width: 0; display: flex; flex-direction: column; align-items: center; }

.eyebrow {
  display: flex; align-items: center; gap: 12px; flex-wrap: wrap;
  font-family: var(--se-mono); font-size: 11.5px; letter-spacing: .2em;
  text-transform: uppercase; color: var(--se-accent-text); margin: 0 0 22px;
}
.ver {
  letter-spacing: .04em; text-transform: none; padding: 2px 9px; border-radius: 999px;
  border: 1px solid rgba(242, 236, 228, .16); color: rgba(242, 236, 228, .66);
}

h1 {
  margin: 0; font-size: clamp(2.6rem, 6.4vw, 4.5rem); line-height: .98;
  letter-spacing: -.035em; font-weight: 600; text-wrap: balance;
}
.accent { color: oklch(.72 .16 40); }

.lede {
  margin: 26px 0 0; max-width: 46ch; font-size: clamp(1rem, 1.3vw, 1.12rem);
  line-height: 1.62; color: rgba(242, 236, 228, .68); text-wrap: pretty;
}

.cta { display: flex; flex-wrap: wrap; gap: 12px; margin-top: 34px; }
.btn {
  display: inline-flex; align-items: center; gap: 10px;
  font-size: 15px; padding: 14px 24px; border-radius: 999px; text-decoration: none;
  transition: background .16s ease, border-color .16s ease, transform .16s ease;
}
.primary { background: var(--se-ink); color: #17120f; font-weight: 600; }
.primary:hover { background: #fff; transform: translateY(-1px); }
.ghost {
  background: transparent; color: var(--se-ink); font-weight: 500;
  border: 1px solid rgba(242, 236, 228, .22);
}
.ghost:hover { border-color: rgba(242, 236, 228, .5); background: rgba(242, 236, 228, .05); }
.mono { font-family: var(--se-mono); }

.meta {
  display: flex; flex-wrap: wrap; gap: 8px 20px; margin-top: 30px;
  font-family: var(--se-mono); font-size: 11.5px; letter-spacing: .06em;
  color: rgba(242, 236, 228, .66);
}

/* ---- device ---- */
.frame {
  width: 100%; max-width: 520px; border-radius: 22px; padding: 12px;
  background: linear-gradient(180deg, rgba(242, 236, 228, .12), rgba(242, 236, 228, .03));
  box-shadow: 0 40px 80px -30px rgba(0, 0, 0, .9), 0 0 0 1px rgba(242, 236, 228, .06);
}
/* 5:3 is the panel's own ratio (800x480) and the GIF's (640x384), so both
   layers fill the aperture exactly with no letterboxing on either. */
.aperture {
  position: relative; border-radius: 14px; overflow: hidden; background: #050505;
  aspect-ratio: 5 / 3;
}
.layer {
  position: absolute; inset: 0; transition: opacity .28s ease;
}
.gif { width: 100%; height: 100%; object-fit: cover; display: block; }
.gif.hide { opacity: 0; }
.panel { opacity: 0; pointer-events: none; }
.panel.show { opacity: 1; pointer-events: auto; }

.frame { transition: box-shadow .28s ease, transform .28s ease; }
.frame.live { box-shadow: 0 40px 90px -28px rgba(0, 0, 0, .95), 0 0 0 1px var(--se-gold); }

.tryit {
  position: absolute; right: 12px; bottom: 12px; z-index: 2; pointer-events: none;
  font-family: var(--se-mono); font-size: 10.5px; letter-spacing: .14em;
  text-transform: uppercase; padding: 6px 11px; border-radius: 999px;
  background: rgba(6, 6, 5, .72); color: rgba(242, 236, 228, .82);
  backdrop-filter: blur(6px); transition: opacity .2s ease;
}
.tryit.hide { opacity: 0; }
.cap { min-height: 1.2em; }
.cap {
  margin: 12px 0 0; text-align: center; font-family: var(--se-mono);
  font-size: 11px; letter-spacing: .08em; color: rgba(242, 236, 228, .62);
}

@media (prefers-reduced-motion: reduce) {
  .btn, .head-links a, .layer, .frame, .tryit { transition: none; }
  .primary:hover { transform: none; }
}
</style>
