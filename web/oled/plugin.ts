import OledPage from './OledPage.vue'
import pageStyles from './oled.css?inline'

declare global {
  interface Window {
    OneKVMPluginUI: {
      v1: {
        register: (id: string, definition: { apiVersion: 1; component: unknown; styles?: string[] }) => void
      }
    }
  }
}

window.OneKVMPluginUI.v1.register('nanokvm-oled', {
  apiVersion: 1,
  component: OledPage,
  styles: [pageStyles],
})
