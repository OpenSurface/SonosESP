<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { withBase } from 'vitepress'

/* ── Why this is shaped as MODEL -> REVISION, and not a flat list ───────────
 *
 * The flat list had "4-inch" and "4-inch (rev 3.x)" sitting side by side as
 * though they were two different products. They are the same panel, same
 * product code, same firmware source; only the silicon revision inside
 * differs. Presented as siblings, the obvious move is to click the first one -
 * and for a rev3 owner that installs a build whose screen never lights up.
 *
 * So the panel is chosen first, and the revision second, as a deliberate step.
 * A model with more than one revision starts with NOTHING selected and the
 * install button disabled, because there is no safe default: whichever we
 * preselected would be wrong for somebody, silently.
 */

type Build = {
  bin: string
  manifest: string
  beta?: boolean
}

type Revision = Build & {
  label: string
  blurb: string
}

const MODELS = {
  '4inch': {
    name: '4-inch',
    part: 'JC4880P443C',
    specs: [
      ['Display', '800×480 · ST7701 · MIPI-DSI'],
      ['Orientation', 'Portrait (rotated in software)'],
      ['Touch', 'GT911 capacitive'],
      ['Controller', 'ESP32-P4 + ESP32-C6'],
      ['Flash / PSRAM', '16 MB / 32 MB OPI'],
    ],
    note: '',
    /* Two revisions of the SAME board. See docs/TROUBLESHOOTING.md: the two
     * silicon revisions have different internal memory maps, so the linker
     * places code at different addresses and one image cannot serve both.
     * rev3 also needs its own bootloader. */
    revisions: {
      legacy: {
        label: 'Revision v1.x or v2.x',
        blurb: 'Every panel sold up to late 2026. Pick this if unsure.',
        bin: 'firmware-4inch.bin',
        manifest: 'manifest-4inch.json',
      },
      r3: {
        label: 'Revision v3.x',
        blurb: 'Newer silicon. Needed if the screen stays black on the build above.',
        bin: 'firmware-4inch-r3.bin',
        manifest: 'manifest-4inch-r3.json',
        beta: true,
      },
    } as Record<string, Revision>,
  },
  '7inch': {
    name: '7-inch',
    part: 'JC1060P470C',
    specs: [
      ['Display', '1024×600 · JD9165 · MIPI-DSI'],
      ['Orientation', 'Landscape (native)'],
      ['Touch', 'GT911 capacitive'],
      ['Controller', 'ESP32-P4 + ESP32-C6'],
      ['Flash / PSRAM', '16 MB / 32 MB OPI'],
      ['Extra', 'Ethernet'],
    ],
    note:
      'The 7-inch build runs on hardware but has had far less testing than the '
      + '4-inch. GUITION also ship two different LCDs under this product code - '
      + 'a first-boot wizard works out which one you have.',
    revisions: null,
    bin: 'firmware-7inch.bin',
    manifest: 'manifest-7inch.json',
    beta: true,
  },
} as const

type ModelId = keyof typeof MODELS

const modelId = ref<ModelId | ''>('')
const revId = ref<string>('')

const model = computed(() => (modelId.value ? MODELS[modelId.value] : null))

/* The chosen build: a revision when the model has them, otherwise the model's
 * own single build. Null until the choice is complete, which is what gates the
 * install button. */
const build = computed<Build | null>(() => {
  const m = model.value as any
  if (!m) return null
  if (!m.revisions) return m as Build
  return revId.value ? (m.revisions[revId.value] as Build) : null
})

const needsRevision = computed(() => !!(model.value as any)?.revisions)
const ready = computed(() => build.value !== null)
const manifestUrl = computed(() =>
  build.value ? withBase('/' + build.value.manifest) : '')

const version = ref('')
const parts = ref<{ path: string; offset: string }[]>([])

/* Read the manifest we are about to flash rather than hardcoding a parts list.
   It is the same file esp-web-tools consumes, so what is shown here is exactly
   what gets written - including boot_app0.bin at 0xe000, the omission that
   deploy-pages.yml has a guard for because leaving it out bricks the boot. */
async function loadManifest(url: string) {
  parts.value = []
  if (!url) return
  try {
    const m = await (await fetch(url)).json()
    version.value = m.version ?? version.value
    const b = m.builds?.[0]
    parts.value = (b?.parts ?? []).map((pt: any) => ({
      path: String(pt.path ?? '').split('/').pop() ?? '',
      offset: '0x' + Number(pt.offset ?? 0).toString(16),
    }))
  } catch {
    // Silent on purpose: an unreachable manifest should not put an error in
    // front of someone who is about to plug a board in. The button still works.
  }
}

/* Version for the header badge, before any choice is made. */
onMounted(() => loadManifest(withBase('/manifest-4inch.json')))

function pickModel(id: ModelId) {
  modelId.value = id
  revId.value = ''
  const m = MODELS[id] as any
  // Single-build models can resolve immediately; multi-revision ones wait.
  loadManifest(m.revisions ? '' : withBase('/' + m.manifest))
}

function pickRevision(id: string) {
  revId.value = id
  loadManifest(manifestUrl.value)
}
</script>

<template>
  <div class="ip">
    <div class="ip-head">
      <div>
        <p class="ip-kicker">Browser installer</p>
        <p class="ip-title">Flash it over USB</p>
      </div>
      <span class="ip-env">Web Serial · Chrome · Edge · Opera</span>
    </div>

    <!-- ── before you start ──────────────────────────────────────────────
         Up front, not buried. The cable is the single most common reason a
         panel never appears in the browser's port list, and a charge-only
         USB-C cable looks identical to a data one. -->
    <p class="ip-step"><span class="ip-num">00</span> Before you start</p>
    <ul class="ip-pre">
      <li>
        <strong>A desktop Chrome, Edge or Opera.</strong>
        Firefox and Safari have no Web Serial, and no phone browser can do this.
      </li>
      <li>
        <strong>A USB-C <em>data</em> cable.</strong>
        Charge-only cables look identical and will not work. The board also has
        <em>two</em> USB-C ports and only one of them talks to a computer — if
        nothing appears, that is the first thing to change.
      </li>
      <li>
        <strong>Your 2.4 GHz Wi-Fi name and password.</strong>
        The panel asks for them on first boot. 5 GHz is not supported.
      </li>
    </ul>

    <!-- ── step 1 : the panel ───────────────────────────────────────────── -->
    <p class="ip-step"><span class="ip-num">01</span> Pick your panel</p>
    <div class="ip-boards" role="radiogroup" aria-label="Panel size">
      <button
        v-for="(m, id) in MODELS"
        :key="id"
        type="button"
        role="radio"
        class="ip-board"
        :aria-checked="modelId === id"
        @click="pickModel(id as ModelId)"
      >
        <span class="ip-b-top">
          <span class="ip-b-name">{{ m.name }}</span>
          <span v-if="(m as any).beta" class="ip-beta">beta</span>
        </span>
        <span class="ip-b-part">{{ m.part }}</span>
        <span class="ip-b-bin">{{ m.specs[0][1] }}</span>
      </button>
    </div>

    <!-- ── step 2 : the revision, only when there is a choice ──────────── -->
    <template v-if="needsRevision">
      <p class="ip-step">
        <span class="ip-num">02</span> Pick your hardware revision
      </p>
      <div class="ip-revs" role="radiogroup" aria-label="Hardware revision">
        <button
          v-for="(r, id) in (model as any).revisions"
          :key="id"
          type="button"
          role="radio"
          class="ip-rev"
          :aria-checked="revId === id"
          @click="pickRevision(id as string)"
        >
          <span class="ip-r-dot" aria-hidden="true"></span>
          <span class="ip-r-body">
            <span class="ip-r-top">
              <span class="ip-r-label">{{ r.label }}</span>
              <span v-if="r.beta" class="ip-beta">beta</span>
            </span>
            <span class="ip-r-blurb">{{ r.blurb }}</span>
            <span class="ip-r-bin">{{ r.bin }}</span>
          </span>
        </button>
      </div>

      <details class="ip-help">
        <summary>How do I know which revision I have?</summary>
        <p>
          Nothing on the box says. Any of these will tell you:
        </p>
        <ul>
          <li>
            <strong>Just try it.</strong> Install the v1.x / v2.x build. If the
            screen stays black, it is a v3.x board — install the other one over
            the top. Nothing is damaged either way.
          </li>
          <li>
            <strong>Already running SonosESP?</strong> The serial log says so on
            its first lines:
            <code>[CHIP] ESP32-P4 rev v1.0 …</code>
          </li>
          <li>
            <strong>esptool</strong> prints it directly:
            <code>ESP32-P4 (revision v3.2)</code>
          </li>
        </ul>
        <p class="ip-help-warn">
          Worth knowing: a hand-installed mismatch is <em>not</em> rejected and
          does <em>not</em> roll itself back. Reinstalling the correct build
          fixes it.
        </p>
      </details>
    </template>

    <!-- ── specs + per-model caveat ─────────────────────────────────────── -->
    <dl v-if="model" class="ip-specs">
      <div v-for="row in model.specs" :key="row[0]">
        <dt>{{ row[0] }}</dt>
        <dd>{{ row[1] }}</dd>
      </div>
    </dl>

    <!-- Per-model, not per-beta: more than one board is flagged beta and they
         need different warnings. -->
    <p v-if="model && model.note" class="ip-note">{{ model.note }}</p>

    <!-- ── step 3 : connect ─────────────────────────────────────────────── -->
    <p class="ip-step">
      <span class="ip-num">{{ needsRevision ? '03' : '02' }}</span>
      Plug the panel in over USB-C
    </p>

    <!--
      Rendered only once the choice is complete. The disabled stand-in below is
      a plain button rather than a disabled esp-web-install-button, so the web
      component is never live with an empty manifest.

      :key forces Vue to destroy and recreate the element when the build
      changes. esp-web-tools parses and caches the manifest on the element, so
      reusing it would keep flashing the previously selected build.
    -->
    <esp-web-install-button
      v-if="ready"
      :key="build!.manifest"
      :manifest="manifestUrl"
    >
      <button slot="activate" type="button" class="ip-go">
        Connect and install <span class="ip-arrow">&#8594;</span>
      </button>
      <span slot="unsupported" class="ip-unsupported">
        This browser cannot flash over USB — it has no Web Serial. Use Chrome,
        Edge or Opera on a desktop.
      </span>
      <span slot="not-allowed" class="ip-unsupported">
        Flashing needs a secure connection (https).
      </span>
    </esp-web-install-button>

    <template v-else>
      <button type="button" class="ip-go ip-go-off" disabled>
        Connect and install <span class="ip-arrow">&#8594;</span>
      </button>
      <p class="ip-gate">
        {{ modelId
          ? 'Choose your hardware revision above to enable USB installation.'
          : 'Choose your panel above to enable USB installation.' }}
      </p>
    </template>

    <!-- What actually gets written. This is the part people get wrong by hand. -->
    <div v-if="ready && parts.length" class="ip-parts">
      <p class="ip-parts-h">
        Writes {{ parts.length }} parts
        <span v-if="version" class="ip-ver">v{{ version }}</span>
      </p>
      <ul>
        <li v-for="p in parts" :key="p.offset">
          <code>{{ p.offset }}</code><span>{{ p.path }}</span>
        </li>
      </ul>
      <p class="ip-parts-f">Your Wi-Fi and speaker settings are kept.</p>
    </div>

    <!-- ── what to expect ───────────────────────────────────────────────
         Timings, because the two points people give up at are the empty port
         dialog and the long first boot. -->
    <div v-if="ready" class="ip-expect">
      <p class="ip-expect-h">What happens next</p>
      <ol>
        <li>
          A port dialog opens and the panel appears as a USB serial device.
          <strong>If the list is empty, try the board's other USB-C port</strong>
          — it has two and only one carries data. That is the most common cause
          by a wide margin; a charge-only cable is the next.
        </li>
        <li>Writing takes about a minute. Do not unplug it.</li>
        <li>
          The panel reboots and asks for Wi-Fi. First boot can take
          <strong>up to 30 seconds</strong> before anything is drawn.
        </li>
        <li>Your speakers are found automatically once it is on the network.</li>
      </ol>
    </div>
  </div>
</template>

<style scoped>
.ip {
  border: 1px solid rgba(242, 236, 228, .12);
  border-radius: 18px;
  padding: clamp(20px, 3vw, 28px);
  margin: 28px 0;
  background:
    radial-gradient(90% 120% at 100% 0%, rgba(224, 178, 82, .08), transparent 62%),
    rgba(242, 236, 228, .025);
}

.ip-head {
  display: flex; flex-wrap: wrap; gap: 12px;
  align-items: flex-start; justify-content: space-between;
  padding-bottom: 18px; border-bottom: 1px solid rgba(242, 236, 228, .08);
}
.ip-kicker {
  margin: 0 0 6px; font-family: var(--vp-font-family-mono);
  font-size: 10.5px; letter-spacing: .18em; text-transform: uppercase;
  color: var(--se-gold);
}
.ip-title { margin: 0; font-size: 1.25rem; font-weight: 600; letter-spacing: -.02em; }
.ip-env {
  font-family: var(--vp-font-family-mono); font-size: 11px; letter-spacing: .06em;
  color: var(--vp-c-text-3); padding-top: 4px;
}

.ip-step {
  display: flex; align-items: center; gap: 10px;
  margin: 22px 0 12px; font-size: .95rem; font-weight: 600;
}
.ip-num {
  font-family: var(--vp-font-family-mono); font-size: 11px; letter-spacing: .16em;
  color: var(--se-gold);
}

.ip-boards { display: flex; gap: 10px; flex-wrap: wrap; }
.ip-board {
  flex: 1 1 180px; display: flex; flex-direction: column; gap: 5px;
  padding: 14px 16px; border-radius: 12px; cursor: pointer; text-align: left;
  border: 1px solid rgba(242, 236, 228, .12);
  background: rgba(242, 236, 228, .02);
  color: var(--vp-c-text-1); font: inherit;
  transition: border-color .15s, background .15s;
}
.ip-board:hover { border-color: rgba(242, 236, 228, .3); }
.ip-board[aria-checked='true'] {
  border-color: var(--se-gold);
  background: rgba(242, 236, 228, .08);
}
.ip-b-top { display: flex; align-items: center; gap: 8px; }
.ip-b-name { font-weight: 600; font-size: 15px; }
.ip-b-part, .ip-b-bin {
  font-family: var(--vp-font-family-mono); font-size: 11.5px;
  color: var(--vp-c-text-3);
}
.ip-b-part { color: var(--vp-c-text-2); }
.ip-beta {
  font-family: var(--vp-font-family-mono);
  font-size: 9.5px; letter-spacing: .12em; text-transform: uppercase;
  padding: 3px 7px; border-radius: 4px;
  background: rgba(201, 117, 47, .18); color: #e09a5a;
}

.ip-specs {
  margin: 16px 0 0; display: grid; gap: 1px; border-radius: 12px; overflow: hidden;
  background: rgba(242, 236, 228, .08);
  border: 1px solid rgba(242, 236, 228, .08);
}
.ip-specs > div {
  display: flex; justify-content: space-between; gap: 16px;
  padding: 10px 14px; background: #100e0d; font-size: 13.5px;
}
.ip-specs dt { color: var(--vp-c-text-3); }
.ip-specs dd {
  margin: 0; font-family: var(--vp-font-family-mono); font-size: 12.5px;
  color: var(--vp-c-text-1);
}

.ip-note {
  font-size: 13px; line-height: 1.6; color: var(--vp-c-text-2);
  border-left: 2px solid #c9752f; padding: 2px 0 2px 14px; margin: 16px 0 0;
}

.ip-go {
  width: 100%; padding: 15px 20px; border: 0; border-radius: 999px; cursor: pointer;
  background: var(--se-gold); color: #17120f;
  font: inherit; font-weight: 600; font-size: 15px;
  display: inline-flex; align-items: center; justify-content: center; gap: 10px;
  transition: background .15s, transform .15s;
}
.ip-go:hover { background: var(--se-accent-hover); transform: translateY(-1px); }
.ip-arrow { font-family: var(--vp-font-family-mono); }
.ip-unsupported {
  display: block; font-size: 13px; line-height: 1.6; color: var(--vp-c-text-2);
  border: 1px dashed rgba(242, 236, 228, .18); border-radius: 10px; padding: 14px 16px;
}

.ip-parts { margin-top: 20px; padding-top: 16px; border-top: 1px solid rgba(242, 236, 228, .08); }
.ip-parts-h {
  display: flex; align-items: center; gap: 10px; margin: 0 0 10px;
  font-family: var(--vp-font-family-mono); font-size: 10.5px;
  letter-spacing: .16em; text-transform: uppercase; color: var(--vp-c-text-3);
}
.ip-ver {
  letter-spacing: .04em; text-transform: none; padding: 2px 8px; border-radius: 999px;
  border: 1px solid rgba(242, 236, 228, .16); color: var(--vp-c-text-2);
}
.ip-parts ul { list-style: none; margin: 0; padding: 0; display: grid; gap: 4px; }
.ip-parts li { display: flex; gap: 12px; align-items: baseline; font-size: 12.5px; }
.ip-parts code {
  font-family: var(--vp-font-family-mono); color: var(--se-gold);
  background: none; padding: 0; min-width: 62px;
}
.ip-parts li span { color: var(--vp-c-text-2); }
.ip-parts-f { margin: 12px 0 0; font-size: 12.5px; color: var(--vp-c-text-3); }

@media (prefers-reduced-motion: reduce) {
  .ip-board, .ip-go { transition: none; }
  .ip-go:hover { transform: none; }
}

/* ── before-you-start checklist ─────────────────────────────────────────── */
.ip-pre { margin: 0; padding: 0; list-style: none; display: grid; gap: 10px; }
.ip-pre li {
  font-size: 13.5px; line-height: 1.55; color: var(--vp-c-text-2);
  padding-left: 18px; position: relative;
}
.ip-pre li::before {
  content: ''; position: absolute; left: 0; top: .55em;
  width: 6px; height: 6px; border-radius: 50%; background: var(--se-gold);
}
.ip-pre strong { color: var(--vp-c-text-1); font-weight: 600; }

/* ── revision picker ─────────────────────────────────────────────────────
   A vertical list rather than side-by-side cards: the blurb is the part that
   actually decides it, and it needs room to read as a sentence. */
.ip-revs { display: grid; gap: 8px; }
.ip-rev {
  display: flex; gap: 12px; align-items: flex-start; text-align: left;
  padding: 13px 15px; border-radius: 12px; cursor: pointer;
  border: 1px solid rgba(242, 236, 228, .12);
  background: rgba(242, 236, 228, .02);
  color: var(--vp-c-text-1); font: inherit;
  transition: border-color .15s, background .15s;
}
.ip-rev:hover { border-color: rgba(242, 236, 228, .3); }
.ip-rev[aria-checked='true'] {
  border-color: var(--se-gold); background: rgba(242, 236, 228, .08);
}
.ip-r-dot {
  flex: 0 0 auto; width: 14px; height: 14px; margin-top: 3px;
  border-radius: 50%; border: 1px solid rgba(242, 236, 228, .35);
  position: relative;
}
.ip-rev[aria-checked='true'] .ip-r-dot { border-color: var(--se-gold); }
.ip-rev[aria-checked='true'] .ip-r-dot::after {
  content: ''; position: absolute; inset: 3px;
  border-radius: 50%; background: var(--se-gold);
}
.ip-r-body { display: flex; flex-direction: column; gap: 3px; min-width: 0; }
.ip-r-top { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.ip-r-label { font-weight: 600; font-size: 14.5px; }
.ip-r-blurb { font-size: 13px; line-height: 1.5; color: var(--vp-c-text-2); }
.ip-r-bin {
  font-family: var(--vp-font-family-mono); font-size: 11.5px;
  color: var(--vp-c-text-3);
}

/* ── "how do I know" disclosure ──────────────────────────────────────────── */
.ip-help {
  margin: 12px 0 0; border-radius: 12px;
  border: 1px solid rgba(242, 236, 228, .1);
  background: rgba(242, 236, 228, .02);
}
.ip-help summary {
  cursor: pointer; padding: 11px 15px; font-size: 13.5px; font-weight: 600;
  color: var(--se-gold); list-style: none;
}
.ip-help summary::-webkit-details-marker { display: none; }
.ip-help summary::before { content: '+ '; font-family: var(--vp-font-family-mono); }
.ip-help[open] summary::before { content: '2 '; }
.ip-help > p, .ip-help > ul { margin: 0 15px 11px; font-size: 13px; line-height: 1.6; color: var(--vp-c-text-2); }
.ip-help > ul { padding-left: 18px; display: grid; gap: 7px; }
.ip-help strong { color: var(--vp-c-text-1); }
.ip-help code {
  font-size: 11.5px; padding: 1px 5px; border-radius: 4px;
  background: rgba(242, 236, 228, .07);
}
.ip-help-warn { border-left: 2px solid #c9752f; padding-left: 12px !important; }

/* ── gated install button ───────────────────────────────────────────────── */
.ip-go-off {
  background: rgba(242, 236, 228, .08); color: var(--vp-c-text-3);
  cursor: not-allowed;
}
.ip-go-off:hover { background: rgba(242, 236, 228, .08); transform: none; }
.ip-gate {
  margin: 9px 0 0; text-align: center; font-size: 12.5px;
  color: var(--vp-c-text-3);
}

/* ── what happens next ──────────────────────────────────────────────────── */
.ip-expect {
  margin: 18px 0 0; padding: 15px 17px; border-radius: 12px;
  border: 1px solid rgba(242, 236, 228, .08);
  background: rgba(242, 236, 228, .02);
}
.ip-expect-h {
  margin: 0 0 9px; font-family: var(--vp-font-family-mono);
  font-size: 10.5px; letter-spacing: .16em; text-transform: uppercase;
  color: var(--vp-c-text-3);
}
.ip-expect ol {
  margin: 0; padding-left: 20px; display: grid; gap: 7px;
  font-size: 13px; line-height: 1.55; color: var(--vp-c-text-2);
}
.ip-expect strong { color: var(--vp-c-text-1); }

/* Narrow screens: the panel cards stack rather than squeezing. */
@media (max-width: 520px) {
  .ip-board { flex: 1 1 100%; }
}
</style>
