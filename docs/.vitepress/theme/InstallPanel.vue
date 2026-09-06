<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { withBase } from 'vitepress'

const BOARDS = {
  '4inch': {
    name: '4-inch',
    part: 'JC4880P443C',
    bin: 'firmware-4inch.bin',
    manifest: 'manifest-4inch.json',
    label: 'Connect and install',
    beta: false,
    specs: [
      ['Controller', 'ESP32-P4 + ESP32-C6'],
      ['Display', '800×480 (ST7701)'],
      ['Touch', 'GT911 I²C'],
      ['Flash', '16 MB'],
      ['PSRAM', '32 MB OPI'],
    ],
  },
  '7inch': {
    name: '7-inch',
    part: 'JC1060P470C',
    bin: 'firmware-7inch.bin',
    manifest: 'manifest-7inch.json',
    label: 'Connect and install',
    beta: true,
    specs: [
      ['Controller', 'ESP32-P4 + ESP32-C6'],
      ['Display', '1024×600 (JD9165)'],
      ['Touch', 'GT911 I²C'],
      ['Flash', '16 MB'],
      ['PSRAM', '32 MB OPI'],
      ['Extra', 'Ethernet'],
    ],
  },
} as const

type BoardId = keyof typeof BOARDS

const selected = ref<BoardId>('4inch')
const board = computed(() => BOARDS[selected.value])
const manifestUrl = computed(() => withBase('/' + board.value.manifest))

const version = ref('')
const parts = ref<{ path: string; offset: string }[]>([])

/* Read the manifest we are about to flash rather than hardcoding a parts list.
   It is the same file esp-web-tools consumes, so what is shown here is exactly
   what gets written - including boot_app0.bin at 0xe000, the omission that
   deploy-pages.yml has a guard for because leaving it out bricks the boot. */
async function loadManifest(url: string) {
  parts.value = []
  try {
    const m = await (await fetch(url)).json()
    version.value = m.version ?? version.value
    const build = m.builds?.[0]
    parts.value = (build?.parts ?? []).map((p: any) => ({
      path: String(p.path ?? '').split('/').pop() ?? '',
      offset: '0x' + Number(p.offset ?? 0).toString(16),
    }))
  } catch {
    // Silent on purpose: an unreachable manifest should not put an error in
    // front of someone who is about to plug a board in. The button still works.
  }
}

onMounted(() => loadManifest(manifestUrl.value))

function pick(id: BoardId) {
  selected.value = id
  loadManifest(withBase('/' + BOARDS[id].manifest))
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

    <!-- ── step 1 ────────────────────────────────────────────────────────── -->
    <p class="ip-step"><span class="ip-num">01</span> Pick your panel</p>
    <div class="ip-boards" role="radiogroup" aria-label="Panel size">
      <button
        v-for="(b, id) in BOARDS"
        :key="id"
        type="button"
        role="radio"
        class="ip-board"
        :aria-checked="selected === id"
        @click="pick(id as BoardId)"
      >
        <span class="ip-b-top">
          <span class="ip-b-name">{{ b.name }}</span>
          <span v-if="b.beta" class="ip-beta">beta</span>
        </span>
        <span class="ip-b-part">{{ b.part }}</span>
        <span class="ip-b-bin">{{ b.bin }}</span>
      </button>
    </div>

    <dl class="ip-specs">
      <div v-for="row in board.specs" :key="row[0]">
        <dt>{{ row[0] }}</dt>
        <dd>{{ row[1] }}</dd>
      </div>
    </dl>

    <p v-if="board.beta" class="ip-note">
      The 7-inch build runs on hardware but has had far less testing than the
      4-inch. GUITION also ship two different panels under this product code — a
      first-boot wizard works out which one you have.
    </p>

    <!-- ── step 2 ────────────────────────────────────────────────────────── -->
    <p class="ip-step"><span class="ip-num">02</span> Plug the panel in over USB-C</p>

    <!--
      :key forces Vue to destroy and recreate the element when the board changes.
      esp-web-tools parses and caches the manifest on the element, so reusing it
      would keep flashing the previously selected build.
    -->
    <esp-web-install-button :key="selected" :manifest="manifestUrl">
      <button slot="activate" type="button" class="ip-go">
        {{ board.label }} <span class="ip-arrow">&#8594;</span>
      </button>
      <span slot="unsupported" class="ip-unsupported">
        This browser cannot flash over USB — it has no Web Serial. Use Chrome,
        Edge or Opera on a desktop.
      </span>
      <span slot="not-allowed" class="ip-unsupported">
        Flashing needs a secure connection (https).
      </span>
    </esp-web-install-button>

    <!-- What actually gets written. This is the part people get wrong by hand. -->
    <div v-if="parts.length" class="ip-parts">
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
</style>
