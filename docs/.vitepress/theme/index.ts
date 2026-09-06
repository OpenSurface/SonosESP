import DefaultTheme from 'vitepress/theme'
// AFTER the default theme on purpose: these are overrides, and :root vs .dark
// is a specificity tie that source order decides. Imported first, every token
// below would lose to VitePress's own palette.
import './custom.css'
import type { Theme } from 'vitepress'
import InstallPanel from './InstallPanel.vue'
import HomeHero from './HomeHero.vue'
import HomeBody from './HomeBody.vue'
import PanelDemo from './PanelDemo.vue'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('InstallPanel', InstallPanel)
    app.component('HomeHero', HomeHero)
    app.component('HomeBody', HomeBody)
    app.component('PanelDemo', PanelDemo)
  }
} satisfies Theme
