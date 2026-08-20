<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { withBase } from 'vitepress'

const version = ref('')
onMounted(async () => {
  try {
    const r = await fetch(withBase('/manifest-4inch.json'))
    version.value = (await r.json()).version ?? ''
  } catch {}
})
</script>

<template>
  <section class="hero">
    <!--
      The wash behind the device echoes what the firmware itself does: it samples
      the dominant colour out of the album art and tints the whole screen with it.
      Three slow-drifting blobs, pure CSS, no JS per frame.
    -->
    <div class="wash" aria-hidden="true">
      <span class="blob b1"></span>
      <span class="blob b2"></span>
      <span class="blob b3"></span>
    </div>
    <div class="grain" aria-hidden="true"></div>

    <div class="inner">
      <div class="copy">
        <p class="eyebrow">
          ESP32-P4 · open source
          <span v-if="version" class="ver">v{{ version }}</span>
        </p>

        <h1>
          Your Sonos,<br />
          <span class="grad">on a screen you own.</span>
        </h1>

        <p class="lede">
          A touchscreen remote that shows album art, follows the lyrics, browses your
          whole library and plays anything you have saved — including Spotify and
          YouTube Music. It flashes from your browser in about a minute.
        </p>

        <div class="cta">
          <a class="btn primary" :href="withBase('/guide/install')">Install it</a>
          <a class="btn ghost" :href="withBase('/guide/features')">See what it does</a>
        </div>

        <p class="meta">
          No login on the device · Updates over the air · 4″ and 7″ panels
        </p>
      </div>

      <div class="device-wrap">
        <div class="device">
          <img :src="withBase('/sonosESP.gif')"
               alt="SonosESP running on a GUITION ESP32-P4 touchscreen, showing album art and playback controls" />
        </div>
        <div class="reflect" aria-hidden="true"></div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.hero {
  position: relative;
  overflow: hidden;
  margin: calc(-1 * var(--vp-nav-height)) calc(50% - 50vw) 0;
  padding: calc(var(--vp-nav-height) + 72px) 24px 88px;
  isolation: isolate;
}

/* ---- ambient colour wash ---- */
/* Fades out before the hero's bottom edge. Without this the overflow:hidden
   clips the blobs dead straight and the colour reads as a band stuck across the
   top of the page rather than light falling on it. */
.wash {
  position: absolute; inset: -20%; z-index: -2; filter: blur(90px); opacity: .55;
  -webkit-mask-image: linear-gradient(to bottom, #000 0%, #000 42%, transparent 88%);
          mask-image: linear-gradient(to bottom, #000 0%, #000 42%, transparent 88%);
}
:root.dark .wash { opacity: .42; }
.blob { position: absolute; display: block; border-radius: 50%; }
.b1 { width: 46vw; height: 46vw; left: 4%;  top: -8%;  background: #c9552f; animation: drift1 26s ease-in-out infinite; }
.b2 { width: 40vw; height: 40vw; right: 2%; top: 6%;   background: #d4a84b; animation: drift2 31s ease-in-out infinite; }
.b3 { width: 52vw; height: 52vw; left: 28%; bottom: -26%; background: #2f4d86; animation: drift3 37s ease-in-out infinite; }

@keyframes drift1 { 0%,100% { transform: translate3d(0,0,0) scale(1); } 50% { transform: translate3d(6%,4%,0) scale(1.12); } }
@keyframes drift2 { 0%,100% { transform: translate3d(0,0,0) scale(1.06); } 50% { transform: translate3d(-5%,6%,0) scale(.94); } }
@keyframes drift3 { 0%,100% { transform: translate3d(0,0,0) scale(1); } 50% { transform: translate3d(4%,-5%,0) scale(1.1); } }

/* very fine texture so the gradient does not band on wide screens */
.grain {
  position: absolute; inset: 0; z-index: -1; pointer-events: none; opacity: .035;
  -webkit-mask-image: linear-gradient(to bottom, #000 0%, #000 45%, transparent 90%);
          mask-image: linear-gradient(to bottom, #000 0%, #000 45%, transparent 90%);
  background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='140' height='140'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='.8' numOctaves='3'/%3E%3C/filter%3E%3Crect width='140' height='140' filter='url(%23n)'/%3E%3C/svg%3E");
}

/* ---- layout ---- */
.inner {
  max-width: 1180px; margin: 0 auto;
  display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.05fr);
  gap: 56px; align-items: center;
}

.eyebrow {
  display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
  font-size: 12px; font-weight: 600; letter-spacing: .14em; text-transform: uppercase;
  color: var(--vp-c-text-3); margin: 0 0 18px;
}
.ver {
  font-variant-numeric: tabular-nums; letter-spacing: .04em; text-transform: none;
  padding: 2px 9px; border-radius: 999px;
  border: 1px solid var(--vp-c-divider); color: var(--vp-c-text-2);
}

h1 {
  margin: 0 0 20px; font-size: clamp(2.4rem, 5.2vw, 4rem); line-height: 1.04;
  letter-spacing: -.035em; font-weight: 800; text-wrap: balance;
}
.grad {
  background: linear-gradient(96deg, #d4a84b 0%, #e0873f 42%, #c9552f 100%);
  -webkit-background-clip: text; background-clip: text; color: transparent;
}

.lede {
  margin: 0 0 30px; max-width: 34rem; font-size: 1.06rem; line-height: 1.65;
  color: var(--vp-c-text-2);
}

.cta { display: flex; gap: 12px; flex-wrap: wrap; margin-bottom: 22px; }
.btn {
  display: inline-flex; align-items: center; justify-content: center;
  padding: 13px 26px; border-radius: 999px; font-weight: 600; font-size: 15px;
  text-decoration: none; transition: transform .16s ease, box-shadow .16s ease, background .16s ease;
  border: 1px solid transparent;
}
.btn:hover { transform: translateY(-2px); }
.primary {
  background: var(--vp-c-text-1); color: var(--vp-c-bg);
  box-shadow: 0 10px 26px rgba(0,0,0,.22);
}
.primary:hover { box-shadow: 0 14px 32px rgba(0,0,0,.3); }
.ghost {
  border-color: var(--vp-c-divider); color: var(--vp-c-text-1);
  background: color-mix(in srgb, var(--vp-c-bg) 55%, transparent);
  backdrop-filter: blur(8px);
}
.ghost:hover { border-color: var(--vp-c-text-3); }

.meta { margin: 0; font-size: 13px; color: var(--vp-c-text-3); }

/* ---- device ---- */
.device-wrap { position: relative; }
.device {
  border-radius: 18px; padding: 10px; background: #16161a;
  box-shadow: 0 30px 70px rgba(0,0,0,.45), 0 0 0 1px rgba(255,255,255,.07);
  transform: perspective(1400px) rotateY(-7deg) rotateX(2deg);
  transition: transform .5s cubic-bezier(.2,.7,.3,1);
}
.device:hover { transform: perspective(1400px) rotateY(-2deg) rotateX(0deg); }
.device img { display: block; width: 100%; border-radius: 10px; }
.reflect {
  position: absolute; left: 6%; right: 6%; top: 100%; height: 90px;
  background: linear-gradient(180deg, rgba(255,255,255,.10), transparent 70%);
  filter: blur(14px); border-radius: 50%;
}

@media (max-width: 900px) {
  .inner { grid-template-columns: 1fr; gap: 40px; }
  .device { transform: none; }
  .hero { padding-bottom: 64px; }
}
@media (prefers-reduced-motion: reduce) {
  .blob { animation: none; }
  .btn, .device { transition: none; }
}
</style>
