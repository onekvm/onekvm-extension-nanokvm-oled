import assert from 'node:assert/strict'
import fs from 'node:fs'
import vm from 'node:vm'

const source = fs.readFileSync(new URL('../web/oled/oled.js', import.meta.url), 'utf8')
const markup = fs.readFileSync(new URL('../web/oled/index.html', import.meta.url), 'utf8')
const componentSource = fs.readFileSync(new URL('../web/oled/OledPage.vue', import.meta.url), 'utf8')
const i18nSource = fs.readFileSync(new URL('../web/oled/i18n.ts', import.meta.url), 'utf8')
const manifestTemplate = fs.readFileSync(new URL('../manifest.json.in', import.meta.url), 'utf8')
const manifest = JSON.parse(manifestTemplate.replace('@VERSION@', '0.0.0-test').replace('@ARCH@', 'test'))
assert.equal(manifest.id, 'nanokvm-oled')
assert.equal(manifest.page.renderer, 'vue')
assert.equal(manifest.page.entrypoint, 'web/oled-page.js')
assert.equal(manifest.settings_schema.properties.sleep_seconds.default, 3600)
assert.equal(source.includes('/api/extensions'), false, 'designer must not save settings directly')
assert.equal(source.includes('fetch('), false, 'designer must use the public page host API')
assert.equal(source.includes('navigator.language'), false, 'designer language selection must use the shared i18n runtime')
assert.equal(componentSource.includes('navigator.language'), false, 'Vue controls must use the shared i18n runtime')
assert.match(i18nSource, /import\.meta\.glob<LocaleDefinition>\('\.\/locales\/\*\.ts'/, 'locale files must be registered automatically')
assert.match(i18nSource, /document\.documentElement\.lang/, 'extension locale must follow the host UI language')
assert.match(componentSource, /new MutationObserver/, 'an open extension page must react to host language changes')
assert.match(source, /function setLocale\(locale\)/, 'the canvas designer must support runtime locale changes')
assert.equal(source.includes("element('rotation')"), false, 'rotation must be controlled by the host NSelect')
assert.equal(source.includes("element('add-layer-menu')"), false, 'layer creation must be controlled by the host NDropdown')
assert.match(source, /host\.onRotationState\?\.\(settings\.rotation\)[\s\S]*syncEditor\(\)/, 'rotation changes must update the host NSelect state')
assert.match(source, /function exportLayers\(\)/)
assert.match(source, /function importLayers\(raw\)/)
assert.match(source, /function restoreDefaultCanvas\(\)/)
assert.match(source, /function pcieDefaultLayout\(\)/)
assert.match(source, /function cubeDefaultLayout\(\)/)
assert.match(source, /\{resolution\} x \{fps\}HZ/)
assert.match(source, /video\?\.input_width > 0 && status\?\.video\?\.input_height > 0/)
assert.match(source, /sleep_seconds: 3600/)
assert.match(source, /return 'box-resize'/)
assert.match(source, /drag\.kind === 'box-resize'/)
assert.match(source, /fixed_size/)
assert.match(source, /name: 'Video Status'[\s\S]*mode: 'hdmi'/)
assert.match(source, /name: 'NET Status'[\s\S]*mode: 'eth'/)
assert.match(source, /format: 'onekvm-oled-layers'/)
assert.match(source, /screen: \{ width: screenWidth, height: screenHeight \}/)
assert.match(source, /variant === 'cube' \? 128 : 64/)
assert.match(markup, /id="add-layer-menu-host"/)
assert.match(markup, /id="rotation-select-host"/)
assert.equal((markup.match(/id="rotation-select-host"/g) || []).length, 1)
assert.ok(markup.indexOf('id="rotation-select-host"') < markup.indexOf('id="display-enabled"'), 'viewing direction belongs in the top-right display controls')
assert.match(markup, /data-i18n="display">Enable OLED</)
assert.match(markup, /class="display-toggle-control">[\s\S]*data-i18n="display">Enable OLED<[\s\S]*class="switch-row compact"/)

const registrations = []
const vue = new Proxy({}, {
  get: (_target, property) => property === 'defineComponent' ? (value) => value : () => undefined,
})
const runtime = {
  v1: {
    vue,
    naive: { NAlert: {}, NButton: {}, NDropdown: {}, NSelect: {} },
    register: (id, definition) => registrations.push({ id, definition }),
  },
}
const context = vm.createContext({ OneKVMPluginUI: runtime })
context.window = context
context.globalThis = context
const bundle = fs.readFileSync(new URL('../web/dist/oled-page.js', import.meta.url), 'utf8')
vm.runInContext(bundle, context)

assert.equal(registrations.length, 1)
assert.equal(registrations[0].id, 'nanokvm-oled')
assert.equal(registrations[0].definition.apiVersion, 1)
assert.ok(registrations[0].definition.component)
assert.match(registrations[0].definition.styles[0], /@scope \(\.onekvm-oled-page\)/)
assert.match(bundle, /\.NDropdown/)
assert.match(bundle, /\.NSelect/)
assert.match(bundle, /onekvm-oled-.*-layers\.json/)
assert.match(bundle, /当前 NanoKVM 机型没有 OLED 屏幕/)
assert.match(bundle, /还原默认画布/)
assert.match(bundle, /画布/)
