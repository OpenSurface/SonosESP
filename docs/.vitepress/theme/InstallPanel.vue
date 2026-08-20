<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { withBase } from 'vitepress'

const BOARDS = {
  '4inch': {
    manifest: 'manifest-4inch.json',
    label: 'Install 4″ firmware',
    name: '4-inch',
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
    manifest: 'manifest-7inch.json',
    label: 'Install 7″ firmware (beta)',
    name: '7-inch',
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

onMounted(async () => {
  // Version pill. Failure stays silent on purpose — a stale or unreachable
  // manifest should not put an error in front of someone about to flash.
  try {
    const r = await fetch(withBase('/manifest-4inch.json'))
    version.value = (await r.json()).version ?? ''
  } catch {}
})
</script>

<template>
  <div class="ip">
    <div class="ip-head">
      <span class="ip-title">Choose your screen</span>
      <span v-if="version" class="ip-ver">v{{ version }}</span>
    </div>

    <div class="ip-boards" role="radiogroup" aria-label="Screen size">
      <button
        v-for="(b, id) in BOARDS"
        :key="id"
        type="button"
        role="radio"
        class="ip-board"
        :aria-checked="selected === id"
        @click="selected = id as BoardId"
      >
        <span class="ip-board-name">{{ b.name }}</span>
        <span v-if="b.beta" class="ip-beta">beta</span>
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

    <!--
      :key forces Vue to destroy and recreate the element when the board changes.
      esp-web-tools parses and caches the manifest on the element, so reusing it
      would keep flashing the previously selected build.
    -->
    <esp-web-install-button :key="selected" :manifest="manifestUrl">
      <button slot="activate" type="button" class="ip-go">{{ board.label }}</button>
      <span slot="unsupported" class="ip-unsupported">
        This browser cannot flash over USB. Use Chrome, Edge or Opera on desktop.
      </span>
      <span slot="not-allowed" class="ip-unsupported">
        Flashing needs a secure connection (https).
      </span>
    </esp-web-install-button>
  </div>
</template>

<style scoped>
.ip {
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  padding: 20px;
  margin: 24px 0;
  background: var(--vp-c-bg-soft);
}
.ip-head { display: flex; align-items: baseline; justify-content: space-between; gap: 12px; }
.ip-title { font-weight: 600; }
.ip-ver {
  font-size: 12px; font-variant-numeric: tabular-nums;
  color: var(--vp-c-text-2); border: 1px solid var(--vp-c-divider);
  border-radius: 999px; padding: 2px 8px;
}
.ip-boards { display: flex; gap: 10px; margin: 14px 0 16px; flex-wrap: wrap; }
.ip-board {
  flex: 1 1 140px; display: flex; align-items: center; justify-content: center; gap: 8px;
  padding: 12px 14px; border-radius: 10px; cursor: pointer;
  border: 1px solid var(--vp-c-divider); background: var(--vp-c-bg);
  color: var(--vp-c-text-1); font: inherit; font-weight: 600;
  transition: border-color .15s, background .15s;
}
.ip-board:hover { border-color: var(--vp-c-brand-1); }
.ip-board[aria-checked='true'] {
  border-color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}
.ip-beta {
  font-size: 10px; letter-spacing: .08em; text-transform: uppercase;
  padding: 2px 6px; border-radius: 3px;
  background: var(--vp-c-warning-soft); color: var(--vp-c-warning-1);
}
.ip-specs { margin: 0 0 16px; display: grid; gap: 6px; }
.ip-specs > div { display: flex; justify-content: space-between; font-size: 14px; }
.ip-specs dt { color: var(--vp-c-text-2); }
.ip-specs dd { margin: 0; font-variant-numeric: tabular-nums; }
.ip-note {
  font-size: 13px; color: var(--vp-c-text-2);
  border-left: 3px solid var(--vp-c-warning-1); padding-left: 12px; margin: 0 0 16px;
}
.ip-go {
  width: 100%; padding: 12px 16px; border: 0; border-radius: 10px; cursor: pointer;
  background: var(--vp-c-brand-1); color: var(--vp-c-white);
  font: inherit; font-weight: 600; font-size: 15px;
}
.ip-go:hover { background: var(--vp-c-brand-2); }
.ip-unsupported { display: block; font-size: 13px; color: var(--vp-c-text-2); }
</style>
