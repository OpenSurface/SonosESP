import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'SonosESP',
  description: 'A touchscreen Sonos controller for ESP32-P4',
  // Repo is served from https://opensurface.github.io/SonosESP/
  base: '/SonosESP/',
  srcDir: '.',
  cleanUrls: true,
  lastUpdated: true,

  head: [
    // esp-web-tools provides <esp-web-install-button>. Loaded globally so the
    // install page can create the element from script, the same way the previous
    // standalone installer did.
    ['script', {
      type: 'module',
      src: 'https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module'
    }],
    // Link preview card. og:image was pointing at the 2.1 MB animated GIF on
    // raw.githubusercontent.com: several scrapers reject GIFs outright, the rest
    // show a single arbitrary frame, and nothing animates it. og-card.png is a
    // static 1200x630 built from the clearest player frame.
    //
    // og:title and og:description were missing entirely, so every share fell
    // back to whatever the scraper inferred. These are absolute URLs because
    // relative ones are not resolved by most crawlers.
    ['meta', { property: 'og:type',        content: 'website' }],
    ['meta', { property: 'og:site_name',   content: 'SonosESP' }],
    ['meta', { property: 'og:url',         content: 'https://opensurface.github.io/SonosESP/' }],
    ['meta', { property: 'og:title',       content: 'SonosESP - a touchscreen Sonos controller for ESP32-P4' }],
    ['meta', { property: 'og:description', content: 'Album art, synced lyrics, multi-room control and a weather clock on a 4-inch or 7-inch panel. Free, MIT licensed, and you flash it from your browser.' }],
    ['meta', { property: 'og:image',       content: 'https://opensurface.github.io/SonosESP/og-card.png' }],
    ['meta', { property: 'og:image:width',  content: '1200' }],
    ['meta', { property: 'og:image:height', content: '630' }],
    ['meta', { property: 'og:image:alt',   content: 'A wall-mounted touchscreen showing album art, track title and synced lyrics, with playback controls below.' }],
    ['meta', { name: 'twitter:card',        content: 'summary_large_image' }],
    ['meta', { name: 'twitter:title',       content: 'SonosESP - a touchscreen Sonos controller for ESP32-P4' }],
    ['meta', { name: 'twitter:description', content: 'Album art, synced lyrics, multi-room control and a weather clock on a 4-inch or 7-inch panel. Free, MIT licensed, and you flash it from your browser.' }],
    ['meta', { name: 'twitter:image',       content: 'https://opensurface.github.io/SonosESP/og-card.png' }],
  ],

  vue: {
    template: {
      compilerOptions: {
        // Without this Vue treats <esp-web-install-button> as an unknown
        // component and warns on every render.
        isCustomElement: (tag) => tag.startsWith('esp-web-')
      }
    }
  },

  themeConfig: {
    nav: [
      { text: 'Install', link: '/guide/install' },
      { text: 'Features', link: '/guide/features' },
      { text: 'Guide', link: '/guide/hardware' },
      { text: 'Troubleshooting', link: '/TROUBLESHOOTING' },
      { text: 'Releases', link: 'https://github.com/OpenSurface/SonosESP/releases' },
    ],

    sidebar: [
      {
        text: 'Getting started',
        items: [
          { text: 'Everything it does', link: '/guide/features' },
          { text: 'Install', link: '/guide/install' },
          { text: 'First-time setup', link: '/guide/setup' },
          { text: 'Hardware', link: '/guide/hardware' },
        ]
      },
      {
        text: 'Using it',
        items: [
          { text: 'Music sources', link: '/guide/sources' },
          { text: 'Themes', link: '/guide/themes' },
          { text: 'Updating', link: '/guide/updates' },
        ]
      },
      {
        text: 'Help',
        items: [
          { text: 'Troubleshooting', link: '/TROUBLESHOOTING' },
          { text: '7-inch screens', link: '/MULTI_SCREEN_SUPPORT' },
        ]
      },
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/OpenSurface/SonosESP' }
    ],

    editLink: {
      pattern: 'https://github.com/OpenSurface/SonosESP/edit/main/docs/:path',
      text: 'Edit this page on GitHub'
    },

    search: { provider: 'local' },

    footer: {
      message:
        'MIT licensed · <a href="https://ko-fi.com/pizzapasta" target="_blank" rel="noopener">Support on Ko-fi</a>',
      copyright: 'SonosESP'
    }
  }
})
