import './custom.css'
import DefaultTheme from 'vitepress/theme'
import type { Theme } from 'vitepress'
import InstallPanel from './InstallPanel.vue'
import HomeHero from './HomeHero.vue'
import HomeBody from './HomeBody.vue'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('InstallPanel', InstallPanel)
    app.component('HomeHero', HomeHero)
    app.component('HomeBody', HomeBody)
  }
} satisfies Theme
