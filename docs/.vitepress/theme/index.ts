import DefaultTheme from 'vitepress/theme'
import type { Theme } from 'vitepress'
import InstallPanel from './InstallPanel.vue'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('InstallPanel', InstallPanel)
  }
} satisfies Theme
