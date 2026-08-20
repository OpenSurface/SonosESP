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
    ['meta', { property: 'og:image', content: 'https://raw.githubusercontent.com/OpenSurface/SonosESP/main/assets/sonosESP.gif' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
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
      message: 'MIT licensed',
      copyright: 'SonosESP'
    }
  }
})
