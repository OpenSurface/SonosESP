<script setup lang="ts">
/**
 * The firmware UI, running in the page.
 *
 * This mirrors the LVGL Amber theme rather than the website's own design
 * language - Montserrat, gold #E0B252, the warm neutral ramp - because it
 * doubles as a spec for what the panel actually looks like. Keep it in step
 * with src/screens/ui_theme_amber.cpp.
 *
 * The frame is a fixed 800x480 (the 7" panel's logical size) scaled to fit its
 * container, so the proportions stay exactly right at any width instead of
 * reflowing into something the hardware never shows.
 */
import { ref, reactive, computed, watch, onMounted, onBeforeUnmount } from 'vue'
import { withBase } from 'vitepress'

type View = 'player' | 'settings'
type Overlay = null | 'queue' | 'rooms'

/* The host tells us when it is showing. Going dark resets the panel, so the
   next reveal opens on the player instead of resuming on whatever settings
   page happened to be open when the pointer left. */
const props = defineProps<{ active?: boolean }>()

/* The queue is the source of truth for what is playing: the transport, the
   drawer and the artwork column all read the same row, so skipping a track
   moves every one of them together the way the firmware does. */
const QUEUE = [
  { t: 'To Crawl Under Your Skin',    a: 'Neurosis', al: 'Souls At Zero \u00b7 1992', d: '7:52', secs: 472 },
  { t: 'Cleanse III (Live In London)', a: 'Neurosis', al: 'Times Of Grace \u00b7 1999', d: '8:12', secs: 492 },
  { t: 'Takeahnase',                   a: 'Neurosis', al: 'Through Silver In Blood \u00b7 1996', d: '6:04', secs: 364 },
  { t: 'Syndic Calls',                 a: 'ISIS',     al: 'Oceanic \u00b7 2002', d: '9:32', secs: 572 },
  { t: 'Weight',                       a: 'ISIS',     al: 'Oceanic \u00b7 2002', d: '5:41', secs: 341 },
  { t: 'Grey Machine',                 a: 'Godflesh', al: 'Streetcleaner \u00b7 1989', d: '6:18', secs: 378 },
  { t: 'Like Rats',                    a: 'Godflesh', al: 'Streetcleaner \u00b7 1989', d: '4:12', secs: 252 },
  { t: 'Locust Star',                  a: 'Neurosis', al: 'Through Silver In Blood \u00b7 1996', d: '6:47', secs: 407 },
]

const ROOMS = [
  { name: 'Living Room', playing: true,  vol: 42 },
  { name: 'Kitchen',     playing: false, vol: 18 },
  { name: 'Bedroom',     playing: false, vol: 30 },
  { name: 'Office',      playing: false, vol: 55 },
]

const view = ref<View>('player')
const page = ref('general')
const playing = ref(true)
const lyrics = ref(false)
const overlay = ref<Overlay>(null)

const queue = ref([...QUEUE])
const nowIdx = ref(0)
const progress = ref(29)
const shuffle = ref(false)
const repeat = ref(false)
const rooms = reactive(ROOMS.map(r => ({ ...r })))
const roomIdx = ref(0)
const scanning = ref(false)

const now = computed(() => queue.value[nowIdx.value] ?? QUEUE[0])
const room = computed(() => rooms[roomIdx.value] ?? rooms[0])

function mmss(total: number) {
  const m = Math.floor(total / 60), sec = Math.floor(total % 60)
  return `${m}:${sec < 10 ? '0' : ''}${sec}`
}
const elapsed = computed(() => mmss(now.value.secs * progress.value / 100))
const remain  = computed(() => '-' + mmss(now.value.secs * (100 - progress.value) / 100))

const upNext = computed(() => queue.value[nowIdx.value + 1] ?? queue.value[0] ?? null)
const queueSummary = computed(() => {
  const n = queue.value.length
  if (!n) return 'Nothing queued'
  const secs = queue.value.reduce((a, q) => a + q.secs, 0)
  const h = Math.floor(secs / 3600), m = Math.round((secs % 3600) / 60)
  return `${n} track${n === 1 ? '' : 's'} \u00b7 ${h ? h + ' hr ' : ''}${m} min`
})

/* Slider read-outs are derived, not stored, so the number tracks the drag. */
function slideLabel(r: Row) {
  const v = slideOf(r)
  if (r.v && r.v.endsWith('%')) return v + '%'
  if (r.t === 'Auto-dim after') return Math.round(v * 1.2) + 's'
  if (r.t === 'Inactivity timeout') return Math.max(1, Math.round(v / 5)) + ' min'
  return String(v)
}

function skip(delta: number) {
  if (!queue.value.length) return
  nowIdx.value = (nowIdx.value + delta + queue.value.length) % queue.value.length
  progress.value = 0
  playing.value = true
}
function playRow(i: number) { nowIdx.value = i; progress.value = 0; playing.value = true }

function clearQueue() {
  // The firmware asks first, because it empties the queue for everyone on that
  // speaker. Here it just leaves the drawer honestly empty.
  queue.value = []
  nowIdx.value = 0
  playing.value = false
}
function shuffleQueue() {
  shuffle.value = !shuffle.value
  if (!shuffle.value || queue.value.length < 2) return
  const head = queue.value[nowIdx.value]
  const rest = queue.value.filter((_, i) => i !== nowIdx.value)
  for (let i = rest.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1))
    ;[rest[i], rest[j]] = [rest[j], rest[i]]
  }
  queue.value = [head, ...rest]
  nowIdx.value = 0
}

function scan() {
  if (scanning.value) return
  scanning.value = true
  window.setTimeout(() => { scanning.value = false }, 1400)
}

/* Drag-anywhere bars.
   getBoundingClientRect reports the post-transform box and clientX is in the
   same space, so the ratio survives the scaling the frame does - no dividing by
   scale. The element is captured off the first event and closed over: reading
   currentTarget later would give null, since it is only valid during dispatch. */
function drag(e: PointerEvent, set: (v: number) => void) {
  const el = e.currentTarget as HTMLElement
  const at = (clientX: number) => {
    const r = el.getBoundingClientRect()
    if (!r.width) return 0
    return Math.round(Math.max(0, Math.min(1, (clientX - r.left) / r.width)) * 100)
  }
  el.setPointerCapture?.(e.pointerId)
  set(at(e.clientX))

  const move = (ev: PointerEvent) => set(at(ev.clientX))
  const up = () => {
    el.removeEventListener('pointermove', move)
    el.removeEventListener('pointerup', up)
    el.removeEventListener('pointercancel', up)
  }
  el.addEventListener('pointermove', move)
  el.addEventListener('pointerup', up)
  el.addEventListener('pointercancel', up)
}

/* Settings controls keep their live value here, keyed by page and row, so the
   page tables stay declarative and a toggle you flip survives navigating away
   and back. */
const toggles = reactive<Record<string, boolean>>({})
const values = reactive<Record<string, string>>({})
const slides = reactive<Record<string, number>>({})
const key = (t: string) => page.value + '|' + t

function isOn(r: Row) { const k = key(r.t); return k in toggles ? toggles[k] : r.ctl === 'on' }
function flip(r: Row) { toggles[key(r.t)] = !isOn(r) }
function valOf(r: Row) { const k = key(r.t); return k in values ? values[k] : (r.v ?? '') }
function cycle(r: Row) {
  if (!r.opts || r.opts.length < 2) return
  const i = r.opts.indexOf(valOf(r))
  values[key(r.t)] = r.opts[(i + 1) % r.opts.length]
}
function slideOf(r: Row) { const k = key(r.t); return k in slides ? slides[k] : (r.pct ?? 0) }
function setSlide(r: Row, v: number) { slides[key(r.t)] = v }

/* The theme row's description IS the selected theme's registry entry - the
   firmware retargets that label, so cycling the value here retargets it too. */
const THEME_DESC: Record<string, string> = {
  'SonosESP': 'The original - blurred album art fills the screen',
  'Immersive': 'Full-bleed colour, oversized title and large lyrics',
  'Amber': 'Flat panel, artwork column, controls always visible',
}
const FACE_DESC: Record<string, string> = {
  'Amber': 'Warm flat clock with weather, forecast and the paused track',
  'Horizon': 'Centred clock over an ambient glow, with a 6-hour forecast',
  'Orbit': 'Light clock with a live sun-path arc and temperature curve',
  'Monolith': 'Hours stacked over minutes, with a details column',
  'StandBy': 'Oversized overlapping digits, tinted from the album art',
}
function descOf(r: Row) {
  const v = valOf(r)
  if (r.t === 'Player appearance') return THEME_DESC[v] ?? r.d
  if (r.t === 'Face') return FACE_DESC[v] ?? r.d
  return r.d
}

const rail = [
  { id: 'general',  label: 'General',  icon: '#ic-gear' },
  { id: 'speakers', label: 'Speakers', icon: '#ic-speaker' },
  { id: 'groups',   label: 'Groups',   icon: '#ic-groups' },
  { id: 'sources',  label: 'Sources',  icon: '#ic-music' },
  { id: 'display',  label: 'Display',  icon: '#ic-display' },
  { id: 'wifi',     label: 'WiFi',     icon: '#ic-wifi' },
  { id: 'clock',    label: 'Clock',    icon: '#ic-clock' },
  { id: 'update',   label: 'Update',   icon: '#ic-download' },
]

/* Settings pages, transcribed from the firmware screens rather than invented:
   ui_general_screen.cpp, ui_devices_screen.cpp, ui_groups_screen.cpp,
   ui_settings_screens.cpp (Sources), ui_display_screen.cpp, ui_wifi_screen.cpp,
   ui_clock_settings.cpp and ui_ota_screen.cpp. Card titles, row labels and the
   description strings are the text the panel itself shows. Every page shares one
   grammar - a gold card label with a rule, then rows carrying a single control. */
type Ctl = 'on' | 'off' | 'val' | 'chev' | 'slider' | 'badge'
type Row = { t: string; d?: string; ctl: Ctl; v?: string; pct?: number; opts?: string[] }
type Group = { label: string; rows: Row[] }
type Tile = { label: string; icon: string }
type Page = {
  title: string; action?: string; status?: string
  groups?: Group[]; tiles?: Tile[]; note?: string
}

const pages: Record<string, Page> = {
  general: { title: 'General', groups: [
    { label: 'Lyrics', rows: [
      { t: 'Show synced lyrics', d: 'Time-synced from LRCLIB. No API key needed.', ctl: 'on' },
    ] },
    // The description IS the selected theme's entry in the THEMES[] registry -
    // the firmware retargets that label when you pick a different theme.
    { label: 'Theme', rows: [
      { t: 'Player appearance', d: 'Flat panel, artwork column, controls always visible',
        ctl: 'val', v: 'Amber', opts: ['Amber', 'SonosESP', 'Immersive'] },
    ] },
  ] },

  speakers: { title: 'Speakers', action: 'Scan', status: '3 speakers found \u00b7 1 playing', groups: [
    { label: 'Available', rows: [
      { t: 'Living Room', d: 'Playing', ctl: 'slider', pct: 42 },
      { t: 'Kitchen', d: 'Grouped \u00b7 +1 speaker', ctl: 'slider', pct: 18 },
      { t: 'Bedroom', ctl: 'slider', pct: 30 },
    ] },
  ] },

  groups: { title: 'Groups', action: 'Scan', status: '3 speakers \u00b7 2 groups', groups: [
    { label: 'Groups', rows: [
      { t: 'Living Room', d: '2 speakers in group', ctl: 'chev' },
      { t: 'Bedroom', d: 'Standalone', ctl: 'chev' },
    ] },
  ] },

  // Sources is a tile grid on the panel, not a row list. Labels and order are
  // the SOURCES[] table in ui_settings_screens.cpp.
  sources: { title: 'Sources', tiles: [
    { label: 'Music Library',   icon: '#ic-music' },
    { label: 'Music Shares',    icon: '#ic-folder' },
    { label: 'Sonos Playlists', icon: '#ic-queue' },
    { label: 'Favorites',       icon: '#ic-music' },
    { label: 'Internet Radio',  icon: '#ic-radio' },
    { label: 'Queue',           icon: '#ic-speaker' },
  ] },

  display: { title: 'Display', groups: [
    { label: 'Brightness', rows: [
      { t: 'Screen brightness', ctl: 'slider', pct: 80, v: '80%' },
      { t: 'Dimmed brightness', d: 'Level the screen drops to once the auto-dim timer expires',
        ctl: 'slider', pct: 20, v: '20%' },
    ] },
    { label: 'Auto-dim', rows: [
      { t: 'Auto-dim after', d: 'Idle time before the screen dims. 0 disables dimming.',
        ctl: 'slider', pct: 35, v: '30s' },
    ] },
    { label: 'Player background', rows: [
      { t: 'Blurred album art', d: 'Classic theme only - the others paint their own backdrop',
        ctl: 'off' },
    ] },
  ] },

  wifi: { title: 'WiFi', action: 'Scan', groups: [
    { label: 'Available', rows: [
      { t: 'Home-2G', d: 'Connected \u00b7 2.4 GHz', ctl: 'chev' },
      { t: 'Home-Guest', d: '2.4 GHz', ctl: 'chev' },
      { t: 'Studio', d: '2.4 GHz', ctl: 'chev' },
    ] },
  ] },

  clock: { title: 'Clock', groups: [
    { label: 'Display', rows: [
      { t: 'Activate clock', d: 'When the panel should fall back to a clock face',
        ctl: 'val', v: 'On inactivity',
        opts: ['On inactivity', 'When paused', 'When stopped', 'Disabled'] },
      { t: 'Inactivity timeout', ctl: 'slider', pct: 25, v: '5 min' },
      { t: '12-hour format', d: 'Off = 24-hour clock', ctl: 'off' },
      { t: 'Face', d: 'Warm flat clock with weather, forecast and the paused track',
        ctl: 'val', v: 'Amber', opts: ['Amber', 'Horizon', 'Orbit', 'Monolith', 'StandBy'] },
    ] },
    { label: 'Photo Background', rows: [
      { t: 'Enable random photos',
        d: 'Random photos from Flickr via loremflickr.com (requires WiFi)', ctl: 'off' },
      { t: 'Photo theme', ctl: 'val', v: 'Nature', opts: ['Nature', 'City', 'Abstract', 'Space'] },
      { t: 'Photo refresh interval', ctl: 'val', v: '30 min', opts: ['15 min', '30 min', '1 hr', '6 hr'] },
    ] },
    { label: 'Time Zone', rows: [
      { t: 'Timezone', d: 'Used for the clock and for sunrise/sunset',
        ctl: 'val', v: 'Europe/London',
        opts: ['Europe/London', 'Europe/Paris', 'America/New_York', 'Asia/Tokyo'] },
    ] },
    { label: 'Weather', rows: [
      { t: 'Enable widget', ctl: 'on' },
      { t: 'Location method', ctl: 'val', v: 'Automatic', opts: ['Automatic', 'Manual'] },
      { t: 'Temperature unit', d: 'On = Fahrenheit (\u00b0F), Off = Celsius (\u00b0C)', ctl: 'off' },
    ] },
  ] },

  update: { title: 'Update', note: 'Keep the panel powered during the update.', groups: [
    { label: 'Firmware', rows: [
      { t: 'Current:  v2.0.0', ctl: 'badge', v: 'INSTALLED' },
      { t: 'Latest:  v2.0.0',  ctl: 'badge', v: 'AVAILABLE' },
    ] },
    { label: 'Channel', rows: [
      { t: 'Channel', d: 'Nightly builds can break', ctl: 'val', v: 'Stable', opts: ['Stable', 'Nightly'] },
    ] },
  ] },
}

const current = computed(() => pages[page.value])

watch(() => props.active, on => {
  if (on) return
  view.value = 'player'
  page.value = 'general'
  overlay.value = null
  playing.value = true
  lyrics.value = false
  queue.value = [...QUEUE]
  nowIdx.value = 0
  progress.value = 29
  shuffle.value = false
  repeat.value = false
  roomIdx.value = 0
  rooms.forEach((r, i) => Object.assign(r, ROOMS[i]))
  for (const k of Object.keys(toggles)) delete toggles[k]
  for (const k of Object.keys(values)) delete values[k]
  for (const k of Object.keys(slides)) delete slides[k]
})

/* Nothing else drives the view - the panel is navigated by touching it, the
   way the hardware is. */
/* ---- scale to fit ------------------------------------------------------
   800x480 is the 7" panel's logical size. It scales as one piece rather than
   reflowing, so the layout stays exactly what the hardware renders. The host
   aperture is 5:3, which is the same ratio, so it fills with no letterboxing. */
const W = 800
const box = ref<HTMLElement | null>(null)
const scale = ref(1)
let ro: ResizeObserver | null = null

function fit() {
  const el = box.value
  if (!el) return
  scale.value = Math.min(1, el.clientWidth / W)
}
onMounted(() => {
  fit()
  if (typeof ResizeObserver !== 'undefined') {
    ro = new ResizeObserver(fit)
    if (box.value) ro.observe(box.value)
  }
  window.addEventListener('resize', fit)
})
onBeforeUnmount(() => {
  ro?.disconnect()
  window.removeEventListener('resize', fit)
})
</script>

<template>
  <!--
    Just the screen. The bezel, the caption and the hover swap live in
    HomeHero, which mounts this inside the device frame in place of the GIF.
  -->
  <div ref="box" class="panel-fit">
    <div class="scaler" :style="{ transform: `scale(${scale})` }">
      <div class="screen">

                <!-- ── player ───────────────────────────────────────────── -->
                <div class="row">
                  <div class="art-col">
                    <img class="art" :src="withBase('/demo-art.jpg')" alt="Album art" />
                    <div class="shelf">
                      <template v-if="!lyrics">
                        <div class="tag">NEXT</div>
                        <div class="next-t">{{ upNext ? upNext.t : '—' }}</div>
                        <div class="next-a">{{ upNext ? upNext.a : 'Nothing queued' }}</div>
                      </template>
                      <template v-else>
                        <div class="ly-dim">and the black of the tide comes in</div>
                        <div class="ly-hi">Now I see your thinned face at the window</div>
                        <div class="ly-dim">Is this the next “last day”?</div>
                      </template>
                    </div>
                  </div>

                  <div class="play-col">
                    <div class="topbar">
                      <button class="room-pill" @click="overlay = 'rooms'">
                        <span class="live" :class="{ idle: !room.playing }"></span>
                        <span class="room-name">{{ room.name }}</span>
                        <svg class="i16" viewBox="0 0 24 24"><use href="#ic-chev" /></svg>
                      </button>
                      <span class="grow"></span>
                      <button class="rbtn" :class="{ lit: lyrics }" @click="lyrics = !lyrics"><span class="lrc">LRC</span></button>
                      <button class="rbtn" @click="overlay = 'queue'"><svg class="i21" viewBox="0 0 24 24"><use href="#ic-queue" /></svg></button>
                      <button class="rbtn" @click="view = 'settings'"><svg class="i21" viewBox="0 0 24 24"><use href="#ic-gear" /></svg></button>
                    </div>

                    <div class="track">
                      <div class="artist">{{ queue.length ? now.a.toUpperCase() : '' }}</div>
                      <div class="title">{{ queue.length ? now.t : 'Not Playing' }}</div>
                      <div class="album">{{ queue.length ? now.al : '' }}</div>
                      <div class="bar seekable" @pointerdown="drag($event, v => progress = v)">
                        <div class="fill" :style="{ width: progress + '%' }"></div>
                        <div class="knob gold" :style="{ left: progress + '%' }"></div>
                      </div>
                      <div class="times"><span>{{ elapsed }}</span><span>{{ remain }}</span></div>
                    </div>

                    <div class="transport">
                      <button class="tbtn" :aria-pressed="shuffle" @click="shuffle = !shuffle">
                        <svg class="i22" :class="shuffle ? 'gold-i' : 'mute-i'" viewBox="0 0 24 24"><use href="#ic-shuffle" /></svg>
                      </button>
                      <button class="tbtn lg" @click="skip(-1)"><svg class="i26" viewBox="0 0 24 24"><use href="#ic-prev" /></svg></button>
                      <button class="playbtn" @click="playing = !playing">
                        <svg class="i32" viewBox="0 0 24 24"><use :href="playing ? '#ic-pause' : '#ic-play'" /></svg>
                      </button>
                      <button class="tbtn lg" @click="skip(1)"><svg class="i26" viewBox="0 0 24 24"><use href="#ic-next" /></svg></button>
                      <button class="tbtn" :aria-pressed="repeat" @click="repeat = !repeat">
                        <svg class="i22" :class="repeat ? 'gold-i' : 'mute-i'" viewBox="0 0 24 24"><use href="#ic-repeat" /></svg>
                      </button>
                    </div>

                    <div class="vol">
                      <svg class="i20 mute-i" viewBox="0 0 24 24"><use href="#ic-vol" /></svg>
                      <div class="bar seekable" @pointerdown="drag($event, v => room.vol = v)">
                        <div class="fill ink" :style="{ width: room.vol + '%' }"></div>
                        <div class="knob ink-k" :style="{ left: room.vol + '%' }"></div>
                      </div>
                      <span class="vnum">{{ room.vol }}</span>
                    </div>
                  </div>
                </div>

                <!-- ── queue drawer ─────────────────────────────────────── -->
                <template v-if="overlay === 'queue'">
                  <div class="scrim" @click="overlay = null"></div>
                  <div class="drawer">
                    <div class="dhead">
                      <div class="grow">
                        <div class="dtitle">Queue</div>
                        <div class="dsub">{{ queueSummary }}</div>
                      </div>
                      <button class="xbtn" @click="overlay = null"><svg class="i18" viewBox="0 0 24 24"><use href="#ic-x" /></svg></button>
                    </div>
                    <div class="dlist">
                      <p v-if="!queue.length" class="empty">Queue is empty</p>
                      <button v-for="(q, i) in queue" :key="q.t" class="qrow"
                              :class="{ now: i === nowIdx }" @click="playRow(i)">
                        <svg v-if="i === nowIdx" class="i16 gold-i" viewBox="0 0 24 24"><use href="#ic-play" /></svg>
                        <span v-else class="qn">{{ i + 1 }}</span>
                        <span class="grow">
                          <span class="qt" :class="{ 'gold-t': i === nowIdx }">{{ q.t }}</span>
                          <span class="qa">{{ q.a }}</span>
                        </span>
                        <span class="qd">{{ q.d }}</span>
                      </button>
                    </div>
                    <div class="dfoot">
                      <button class="dbtn" :class="{ on: shuffle }" @click="shuffleQueue">
                        <svg class="i18" viewBox="0 0 24 24"><use href="#ic-shuffle" /></svg><span>Shuffle</span>
                      </button>
                      <button class="dbtn" @click="clearQueue">
                        <svg class="i18" viewBox="0 0 24 24"><use href="#ic-x" /></svg><span>Clear</span>
                      </button>
                    </div>
                  </div>
                </template>

                <!-- ── rooms modal ──────────────────────────────────────── -->
                <template v-if="overlay === 'rooms'">
                  <div class="scrim dark" @click="overlay = null"></div>
                  <div class="modal">
                    <div class="mhead"><div class="grow mtitle">Rooms</div>
                      <button class="xbtn sm" @click="overlay = null"><svg class="i17" viewBox="0 0 24 24"><use href="#ic-x" /></svg></button>
                    </div>
                    <div class="mbody">
                      <div v-for="(r, i) in rooms" :key="r.name" class="rrow" :class="{ on: i === roomIdx }">
                        <button class="rpick" @click="roomIdx = i">
                          <svg class="i22" :class="i === roomIdx ? 'gold-i' : 'mute-i'" viewBox="0 0 24 24"><use href="#ic-speaker" /></svg>
                          <span class="grow">
                            <span class="rn" :class="{ dim: i !== roomIdx }">{{ r.name }}</span>
                            <span class="rs" :class="{ 'live-t': r.playing }">{{ r.playing ? 'Playing · ' + now.a : 'Idle' }}</span>
                          </span>
                        </button>
                        <div class="rbar" @pointerdown="drag($event, v => r.vol = v)">
                          <div class="fill" :class="{ 'mute-f': i !== roomIdx }" :style="{ width: r.vol + '%' }"></div>
                          <div class="knob" :class="i === roomIdx ? 'gold' : 'mute-k'" :style="{ left: r.vol + '%' }"></div>
                        </div>
                      </div>
                    </div>
                  </div>
                </template>

                <!-- ── settings ─────────────────────────────────────────── -->
                <div v-if="view === 'settings'" class="settings">
                  <div class="srail">
                    <div class="shead">
                      <div class="grow"><div class="stitle">Settings</div><div class="sver">v2.0.0 · 7&#8243;</div></div>
                      <button class="xbtn xs" @click="view = 'player'"><svg class="i16" viewBox="0 0 24 24"><use href="#ic-x" /></svg></button>
                    </div>
                    <button v-for="it in rail" :key="it.id" class="ritem" :class="{ on: page === it.id }" @click="page = it.id">
                      <svg class="i19" viewBox="0 0 24 24"><use :href="it.icon" /></svg>
                      <span>{{ it.label }}</span>
                    </button>
                  </div>
                  <div class="spane">
                    <div class="pane-head">
                      <div class="pane-title">{{ current.title }}</div>
                      <button v-if="current.action" class="scan" :class="{ busy: scanning }" @click="scan">
                        <svg class="i16" :class="{ spin: scanning }" viewBox="0 0 24 24"><use href="#ic-refresh" /></svg>
                        <span>{{ scanning ? 'Scanning…' : current.action }}</span>
                      </button>
                    </div>
                    <div v-if="current.status" class="pane-status">{{ current.status }}</div>

                    <div v-if="current.tiles" class="tiles">
                      <button v-for="t in current.tiles" :key="t.label" class="tile">
                        <svg class="i22 gold-i" viewBox="0 0 24 24"><use :href="t.icon" /></svg>
                        <span>{{ t.label }}</span>
                      </button>
                    </div>

                    <template v-for="g in current.groups" :key="g.label">
                      <div class="glabel">{{ g.label }}</div>
                      <div class="gline"></div>
                      <div v-for="r in g.rows" :key="r.t" class="srow">
                        <div class="grow">
                          <div class="rt">{{ r.t }}</div>
                          <div v-if="descOf(r)" class="rd">{{ descOf(r) }}</div>
                          <div v-if="r.ctl === 'slider'" class="sbar"
                               @pointerdown="drag($event, v => setSlide(r, v))">
                            <div class="sfill" :style="{ width: slideOf(r) + '%' }"></div>
                            <div class="sknob" :style="{ left: slideOf(r) + '%' }"></div>
                          </div>
                        </div>
                        <button v-if="r.ctl === 'on' || r.ctl === 'off'" class="tgl"
                                :class="{ off: !isOn(r) }" :aria-pressed="isOn(r)" @click="flip(r)"><i></i></button>
                        <button v-else-if="r.ctl === 'val'" class="rv" @click="cycle(r)">{{ valOf(r) }}</button>
                        <span v-else-if="r.ctl === 'badge'" class="badge">{{ r.v }}</span>
                        <span v-else-if="r.ctl === 'slider'" class="rnum">{{ slideLabel(r) }}</span>
                        <button v-else class="chevb"><svg class="i18 mute-i" viewBox="0 0 24 24"><use href="#ic-chev" /></svg></button>
                      </div>
                    </template>

                    <div v-if="current.note" class="pane-note">{{ current.note }}</div>
                  </div>
                </div>

      </div>
    </div>
    <!-- icon sprite -->
    <svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
      <symbol id="ic-speaker" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><rect x="6" y="3" width="12" height="18" rx="2.5"/><circle cx="12" cy="15" r="3"/><circle cx="12" cy="7.5" r="1.2" fill="currentColor" stroke="none"/></symbol>
      <symbol id="ic-groups" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><rect x="3" y="5" width="11.5" height="14" rx="2.5"/><path d="M18 7.5v9"/><path d="M21.5 9.5v5"/></symbol>
      <symbol id="ic-queue" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M4 6.5h11M4 12h8M4 17.5h8"/><path d="M15.5 12.5l5 3-5 3z" fill="currentColor" stroke="none"/></symbol>
      <symbol id="ic-display" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><rect x="3" y="4" width="18" height="13" rx="2.5"/><path d="M8.5 21h7"/></symbol>
      <symbol id="ic-wifi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M3.2 9.2a14 14 0 0 1 17.6 0"/><path d="M6.8 13a9 9 0 0 1 10.4 0"/><circle cx="12" cy="17.6" r="1.5" fill="currentColor" stroke="none"/></symbol>
      <symbol id="ic-clock" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><circle cx="12" cy="12" r="8.6"/><path d="M12 7.4V12l3.4 2"/></symbol>
      <symbol id="ic-download" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3.5v11"/><path d="M7.6 10.5 12 15l4.4-4.5"/><path d="M5 19.5h14"/></symbol>
      <symbol id="ic-gear" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3.1"/><path d="M19.14 12.94a7.5 7.5 0 0 0 0-1.88l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.3 7.3 0 0 0-1.63-.94l-.36-2.54a.5.5 0 0 0-.5-.42h-3.84a.5.5 0 0 0-.5.42l-.36 2.54c-.59.24-1.13.56-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.65 8.84a.5.5 0 0 0 .12.64l2.03 1.58a7.5 7.5 0 0 0 0 1.88l-2.03 1.58a.5.5 0 0 0-.12.64l1.92 3.32a.5.5 0 0 0 .6.22l2.39-.96c.5.38 1.04.7 1.63.94l.36 2.54a.5.5 0 0 0 .5.42h3.84a.5.5 0 0 0 .5-.42l.36-2.54c.59-.24 1.13-.56 1.63-.94l2.39.96a.5.5 0 0 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64z"/></symbol>
      <symbol id="ic-chev" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9.5 5.5 16 12l-6.5 6.5"/></symbol>
      <symbol id="ic-x" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/></symbol>
      <symbol id="ic-shuffle" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M3 7h3.6l11 10H21"/><path d="M17.6 13.6 21 17l-3.4 3.4"/><path d="M3 17h3.6l2.6-2.4"/><path d="M17.6 3.6 21 7l-3.4 3.4"/></symbol>
      <symbol id="ic-repeat" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M4 12.5V9.5a4 4 0 0 1 4-4h9"/><path d="M14.2 2.6 17.6 5.5l-3.4 2.9"/><path d="M20 11.5v3a4 4 0 0 1-4 4H7"/><path d="M9.8 21.4 6.4 18.5l3.4-2.9"/></symbol>
      <symbol id="ic-prev" viewBox="0 0 24 24" fill="currentColor"><path d="M19 5v14L8.5 12z"/><rect x="4.5" y="5" width="2.6" height="14" rx="1.3"/></symbol>
      <symbol id="ic-next" viewBox="0 0 24 24" fill="currentColor"><path d="M5 5v14L15.5 12z"/><rect x="16.9" y="5" width="2.6" height="14" rx="1.3"/></symbol>
      <symbol id="ic-play" viewBox="0 0 24 24" fill="currentColor"><path d="M8 4.5 20 12 8 19.5z"/></symbol>
      <symbol id="ic-pause" viewBox="0 0 24 24" fill="currentColor"><rect x="6.6" y="4.6" width="3.9" height="14.8" rx="1.4"/><rect x="13.5" y="4.6" width="3.9" height="14.8" rx="1.4"/></symbol>
      <symbol id="ic-vol" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M4 9.5h3.6L12.4 5.5v13L7.6 14.5H4z" fill="currentColor" stroke="none"/><path d="M16 9.6a4 4 0 0 1 0 4.8"/><path d="M18.6 7.2a7.4 7.4 0 0 1 0 9.6"/></symbol>
      <symbol id="ic-music" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M9 17.5V5.6l10-2v11.5"/><circle cx="6.4" cy="17.6" r="2.6"/><circle cx="16.4" cy="15.5" r="2.6"/></symbol>
      <symbol id="ic-folder" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M3 7a2 2 0 0 1 2-2h4l2.2 2.6H19a2 2 0 0 1 2 2V17a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></symbol>
      <symbol id="ic-radio" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><rect x="2.5" y="8" width="19" height="11.5" rx="2.4"/><path d="M7.5 4.6 15.5 2"/><circle cx="8.6" cy="13.8" r="2.6"/></symbol>
      <symbol id="ic-refresh" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M20.4 12a8.4 8.4 0 1 1-2.5-6"/><path d="M20.4 4.4V10h-5.5"/></symbol>
      <symbol id="ic-plus" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round"><path d="M12 5.5v13M5.5 12h13"/></symbol>
    </defs></svg>
  </div>
</template>

<style scoped>
/* Fills whatever the host gives it; the inner 800px frame scales to match. */
.panel-fit { position: absolute; inset: 0; overflow: hidden; }
.scaler { position: absolute; top: 0; left: 0; width: 800px; transform-origin: top left; }

/* ---- panel UI: the firmware's own language, not the site's ---- */
.screen {
  position: relative; width: 800px; height: 480px; overflow: hidden;
  background: #0B0A09; color: #F5F1EA;
  font-family: Montserrat, 'Segoe UI', system-ui, sans-serif;
  text-align: left;
}
/* Bare reset. Anything that needs a surface re-states it under `.screen` so it
   outranks this rule - `.screen button` is (0,1,1) and would otherwise strip a
   plain `.room-pill` (0,1,0) back to transparent, leaving it visible on hover
   only. */
.screen button { font-family: inherit; font-size: inherit; border: 0; background: none; padding: 0; cursor: pointer; color: inherit; }
.row { position: absolute; inset: 0; display: flex; }
.grow { flex: 1; min-width: 0; }

.art-col {
  position: relative; width: 344px; height: 480px; flex: 0 0 auto;
  background: #0E0D0C; border-right: 1px solid #23201C; display: flex; flex-direction: column;
}
.art { width: 344px; height: 344px; object-fit: cover; display: block; flex: 0 0 auto; }
.shelf {
  flex: 1; min-height: 0; border-top: 1px solid #23201C; padding: 14px 20px;
  display: flex; flex-direction: column; justify-content: center;
}
.tag { font-size: 10.5px; font-weight: 700; letter-spacing: .16em; color: #8E877D; }
.next-t { font-size: 16px; font-weight: 500; color: #C9C2B8; margin-top: 11px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.next-a { font-size: 12px; color: #8E877D; margin-top: 6px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
/* Three centred lines: prev and next muted at 14, the current line at 20.
   createLyricsOverlay() in src/lyrics.cpp builds exactly this. */
.ly-dim { font-size: 14px; line-height: 1.35; color: #8E877D; text-align: center; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.ly-hi { font-size: 20px; font-weight: 600; line-height: 1.26; color: #F5F1EA; text-align: center; margin: 8px 0; }

.play-col { flex: 1; min-width: 0; display: flex; flex-direction: column; padding: 18px 22px 16px 26px; }
.topbar { display: flex; align-items: center; gap: 10px; }
.screen .room-pill {
  display: flex; align-items: center; gap: 10px; height: 44px; padding: 0 8px 0 14px;
  border-radius: 22px; background: #171513; border: 1px solid #2A2622; min-width: 0;
}
.screen .room-pill:hover, .screen .rbtn:hover { background: #1F1C19; }
.live { width: 7px; height: 7px; border-radius: 4px; background: #6FCF8E; flex: 0 0 auto; }
.room-name { font-size: 15px; font-weight: 500; color: #F5F1EA; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.screen .rbtn {
  width: 44px; height: 44px; border-radius: 22px; background: #171513; border: 1px solid #2A2622;
  display: flex; align-items: center; justify-content: center; flex: 0 0 auto;
}
.lrc { font-size: 12px; font-weight: 700; letter-spacing: .04em; color: #C9C2B8; }
.screen .rbtn.lit { border-color: #4A3D22; }
.screen .rbtn.lit .lrc { color: #E0B252; }

.track { flex: 1; display: flex; flex-direction: column; justify-content: center; padding: 14px 0 2px; min-height: 0; }
.artist { font-size: 12px; font-weight: 700; letter-spacing: .18em; color: #E0B252; }
.title { font-size: 33px; font-weight: 600; line-height: 1.1; letter-spacing: -.015em; margin-top: 9px; color: #F5F1EA; }
.album { font-size: 14px; color: #8E877D; margin-top: 8px; }
.bar { margin-top: 20px; position: relative; height: 6px; border-radius: 3px; background: #2A2622; flex: 1; }
.track .bar { flex: 0 0 auto; }
.fill { position: absolute; left: 0; top: 0; bottom: 0; border-radius: 3px; background: #E0B252; }
.fill.ink { background: #EDE8E0; }
.fill.mute-f { background: #8E877D; }
.knob { position: absolute; top: 3px; width: 18px; height: 18px; margin: -9px 0 0 -9px; border-radius: 9px; border: 3px solid #0B0A09; }
.knob.gold { background: #E0B252; }
.knob.ink-k { background: #F5F1EA; }
.knob.mute-k { background: #8E877D; border: 0; }
.times { display: flex; justify-content: space-between; margin-top: 9px; font-size: 13px; color: #8E877D; font-variant-numeric: tabular-nums; }

.transport { display: flex; align-items: center; justify-content: space-between; margin: 2px 0 14px; }
.tbtn { width: 44px; height: 44px; display: flex; align-items: center; justify-content: center; }
.tbtn.lg { width: 52px; height: 52px; }
.screen .playbtn {
  width: 78px; height: 78px; border-radius: 39px; background: #E0B252;
  display: flex; align-items: center; justify-content: center; flex: 0 0 auto; color: #1A1408;
}
.screen .playbtn:hover { background: #EFC468; }
.vol { display: flex; align-items: center; gap: 14px; }
.vol .bar { margin-top: 0; }
.vnum { font-size: 13px; color: #8E877D; font-variant-numeric: tabular-nums; width: 30px; text-align: right; }

.i16 { width: 16px; height: 16px; } .i17 { width: 17px; height: 17px; }
.i18 { width: 18px; height: 18px; } .i19 { width: 19px; height: 19px; }
.i20 { width: 20px; height: 20px; } .i21 { width: 21px; height: 21px; }
.i22 { width: 22px; height: 22px; } .i26 { width: 26px; height: 26px; }
.i32 { width: 32px; height: 32px; }
.i16, .i17, .i18, .i19, .i20, .i21, .i22, .i26, .i32 { flex: 0 0 auto; color: #C9C2B8; }
.gold-i { color: #E0B252; } .mute-i { color: #8E877D; }
.i26 { color: #F5F1EA; }

/* ---- overlays ---- */
.scrim { position: absolute; inset: 0; background: rgba(6, 6, 5, .6); }
.scrim.dark { background: rgba(6, 6, 5, .72); }
.drawer {
  position: absolute; top: 0; right: 0; bottom: 0; width: 400px;
  background: #100F0E; border-left: 1px solid #2A2622; display: flex; flex-direction: column;
}
.dhead { display: flex; align-items: center; gap: 12px; padding: 18px 18px 14px; border-bottom: 1px solid #221F1B; }
.dtitle { font-size: 18px; font-weight: 600; color: #F5F1EA; }
.dsub { font-size: 12px; color: #8E877D; margin-top: 2px; }
.screen .xbtn { width: 40px; height: 40px; border-radius: 20px; background: #1C1A18; display: flex; align-items: center; justify-content: center; flex: 0 0 auto; }
.screen .xbtn.sm { width: 38px; height: 38px; border-radius: 19px; }
.screen .xbtn.xs { width: 36px; height: 36px; border-radius: 18px; }
/* Scrolls, like the panel's own list. The scrollbar is drawn to match the
   warm ramp - the browser default is a bright slab on this background. */
.dlist { flex: 1; overflow-y: auto; overscroll-behavior: contain; padding: 6px 0; }
.dlist::-webkit-scrollbar, .spane::-webkit-scrollbar, .mbody::-webkit-scrollbar { width: 6px; }
.dlist::-webkit-scrollbar-thumb, .spane::-webkit-scrollbar-thumb, .mbody::-webkit-scrollbar-thumb {
  background: #33302B; border-radius: 3px;
}
.dlist, .spane, .mbody { scrollbar-width: thin; scrollbar-color: #33302B transparent; }
.empty { margin: 22px 0; text-align: center; font-size: 14px; color: #8E877D; }

.screen .qrow {
  display: flex; align-items: center; gap: 14px; width: 100%;
  padding: 11px 18px 11px 21px; text-align: left; background: none;
}
.screen .qrow:hover { background: #151311; }
.screen .qrow.now { padding-left: 18px; background: #171513; border-left: 3px solid #E0B252; }
.screen .qrow.now:hover { background: #1C1A18; }
.qrow .grow { display: flex; flex-direction: column; }
.qn { width: 16px; font-size: 13px; color: #8E877D; text-align: center; font-variant-numeric: tabular-nums; flex: 0 0 auto; }
.qt { display: block; font-size: 15px; color: #F5F1EA; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.qt.gold-t { font-weight: 600; color: #E0B252; }
.qa { display: block; font-size: 12px; color: #8E877D; margin-top: 2px; }
.qd { font-size: 13px; color: #8E877D; font-variant-numeric: tabular-nums; }
.dfoot { padding: 12px 18px; border-top: 1px solid #221F1B; display: flex; gap: 10px; }
.screen .dbtn {
  flex: 1; height: 44px; border-radius: 10px; border: 1px solid #2A2622;
  display: flex; align-items: center; justify-content: center; gap: 9px;
  font-size: 14px; font-weight: 500; color: #C9C2B8;
}
.screen .dbtn:hover { background: #171513; }
.screen .dbtn.on { border-color: #4A3D22; background: #241F16; color: #E0B252; }

.modal {
  position: absolute; left: 140px; top: 56px; width: 520px;
  background: #151311; border: 1px solid #2A2622; border-radius: 16px; overflow: hidden;
}
.mhead { display: flex; align-items: center; padding: 16px 16px 14px 20px; border-bottom: 1px solid #221F1B; }
.mtitle { font-size: 18px; font-weight: 600; }
.mbody { padding: 10px 14px 14px; max-height: 300px; overflow-y: auto; overscroll-behavior: contain; }
.screen .rpick {
  display: flex; align-items: center; gap: 14px; flex: 1; min-width: 0;
  text-align: left; background: none; padding: 0;
}
.rpick .grow { display: flex; flex-direction: column; }
.rrow { display: flex; align-items: center; gap: 14px; padding: 12px 14px; border-radius: 12px; }
.rrow.on { background: #1C1A18; border: 1px solid #4A3D22; }
.rrow + .rrow { margin-top: 8px; }
.rn { display: block; font-size: 16px; font-weight: 600; color: #F5F1EA; }
.rn.dim { font-weight: 500; color: #C9C2B8; }
.rs { display: block; font-size: 13px; color: #8E877D; margin-top: 2px; }
.rs.live-t { color: #6FCF8E; }
.rbar { width: 150px; position: relative; height: 6px; border-radius: 3px; background: #33302B; flex: 0 0 auto; }
.rbar .knob { border: 0; }

/* ---- settings ---- */
.settings { position: absolute; inset: 0; background: #0B0A09; display: flex; }
.srail { width: 216px; flex: 0 0 auto; background: #131211; border-right: 1px solid #23201C; padding: 16px 12px 8px; }
.shead { display: flex; align-items: flex-start; gap: 8px; padding: 0 4px 8px; }
.stitle { font-size: 20px; font-weight: 600; color: #F5F1EA; }
.sver { font-size: 11px; letter-spacing: .1em; color: #8E877D; margin-top: 3px; }
.ritem {
  display: flex; align-items: center; gap: 12px; width: 100%; height: 42px; padding: 0 12px;
  border-radius: 9px; margin-bottom: 1px; color: #8E877D; font-size: 15px; font-weight: 500;
}
.ritem:hover { background: #1A1816; color: #C9C2B8; }
.ritem.on { background: #1C1A18; color: #F5F1EA; font-weight: 600; }
.spane { flex: 1; min-width: 0; padding: 20px 24px; overflow-y: auto; overscroll-behavior: contain; }
.pane-title { font-size: 25px; font-weight: 600; letter-spacing: -.01em; height: 40px; display: flex; align-items: center; margin-bottom: 14px; }
.glabel { font-size: 11px; font-weight: 700; letter-spacing: .18em; color: #E0B252; margin-top: 16px; }
.glabel:first-of-type { margin-top: 0; }
.gline { width: 24px; height: 2px; background: #E0B252; margin-top: 7px; }
.srow {
  display: flex; align-items: center; gap: 16px; min-height: 52px; padding: 11px 0;
  border-bottom: 1px solid #1E1B18;
}
.srow:last-child { border-bottom: 0; }
.rt { font-size: 16px; font-weight: 500; }
.rd { font-size: 13px; color: #8E877D; margin-top: 3px; }
.screen .rv {
  font-size: 15px; color: #E0B252; font-weight: 500; flex: 0 0 auto;
  padding: 6px 12px; border-radius: 8px; background: #1C1A18; border: 1px solid #2A2622;
}
.screen .rv:hover { background: #241F16; border-color: #4A3D22; }
.screen .chevb { padding: 8px; border-radius: 8px; flex: 0 0 auto; }
.screen .chevb:hover { background: #1C1A18; }
.screen .tgl {
  width: 52px; height: 30px; border-radius: 15px; background: #E0B252; padding: 3px;
  display: flex; justify-content: flex-end; flex: 0 0 auto;
  transition: background .16s ease;
}
.screen .tgl.off { background: #33302B; justify-content: flex-start; }
.tgl i { width: 24px; height: 24px; border-radius: 12px; background: #fff; display: block; }
.tgl.off i { background: #8E877D; }

.pane-head { display: flex; align-items: center; gap: 12px; height: 40px; margin-bottom: 10px; }
.pane-title { font-size: 25px; font-weight: 600; letter-spacing: -.01em; flex: 1; min-width: 0; }
.screen .scan {
  display: flex; align-items: center; gap: 7px; height: 34px; padding: 0 14px;
  border-radius: 17px; background: #1C1A18; border: 1px solid #2A2622;
  font-size: 13px; font-weight: 600; color: #C9C2B8; flex: 0 0 auto;
}
.screen .scan:hover { background: #241F16; border-color: #4A3D22; color: #E0B252; }
.pane-status { font-size: 13px; color: #8E877D; margin: -4px 0 12px; }
.pane-note { font-size: 13px; color: #8E877D; margin-top: 14px; }

.tiles { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
.tile {
  display: flex; align-items: center; gap: 11px; height: 62px; padding: 0 14px;
  border-radius: 12px; background: #171513; border: 1px solid #2A2622;
  font-size: 14px; font-weight: 500; color: #F5F1EA;
}
.screen .tile {
  background: #171513; border: 1px solid #2A2622; padding: 0 14px; text-align: left;
}
.screen .tile:hover { background: #1F1C19; border-color: #4A3D22; }

/* Slider rows put the groove under the label, the way the panel lays them out. */
.sbar { position: relative; height: 6px; border-radius: 3px; background: #33302B; margin-top: 10px; }
.sfill { position: absolute; left: 0; top: 0; bottom: 0; border-radius: 3px; background: #E0B252; }
.sknob {
  position: absolute; top: 3px; width: 18px; height: 18px; margin: -9px 0 0 -9px;
  border-radius: 9px; background: #E0B252; border: 3px solid #171513;
}
.rnum { font-size: 14px; color: #C9C2B8; font-variant-numeric: tabular-nums; flex: 0 0 auto; min-width: 44px; text-align: right; }

/* Bars take the pointer anywhere along their length, and get a taller hit area
   than their 6px paint so they are grabbable. */
.seekable, .sbar, .rbar { cursor: pointer; touch-action: none; }
.seekable::before, .sbar::before, .rbar::before {
  content: ''; position: absolute; left: 0; right: 0; top: -9px; bottom: -9px;
}
.sbar, .rbar { position: relative; }
.live.idle { background: #8E877D; }

@keyframes spin { to { transform: rotate(360deg); } }
.spin { animation: spin .9s linear infinite; }
.screen .scan.busy { color: #E0B252; border-color: #4A3D22; }
.badge {
  font-size: 10.5px; font-weight: 700; letter-spacing: .12em; flex: 0 0 auto;
  padding: 5px 10px; border-radius: 6px; background: #241F16; color: #E0B252;
}


@media (prefers-reduced-motion: reduce) {
  .spin { animation: none; }
  .screen .tgl { transition: none; }
}
</style>
