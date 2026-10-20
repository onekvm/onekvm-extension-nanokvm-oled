<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { NAlert, NButton, NDropdown, NSelect, NTabPane, NTabs } from 'naive-ui'

import pageHTML from './index.html?raw'
import './font-cjk-12.js'
import { detectLocale, translate, type MessageKey } from './i18n'
import { mountOLEDDesigner } from './oled.js'
import MediaLibrary from './MediaLibrary.vue'
import LayerList, { type LayerListItem } from './LayerList.vue'
import LayerToolbar, { type LayerToolbarState } from './LayerToolbar.vue'
import OledChrome, { type OledChromeState } from './OledChrome.vue'
import VariablesPanel from './VariablesPanel.vue'

type SettingValue = string | number | boolean | null
type Settings = Record<string, SettingValue>

interface SettingsAdapter {
  dirty: Readonly<{ value: boolean }>
  valid: Readonly<{ value: boolean }>
  collect: () => Settings
  reset: (settings: Settings) => void
}

interface PageHost {
  apiVersion: 1
  extension: Readonly<{ value: { id?: string; settings: Settings } }>
  getStatus: () => Promise<unknown>
  invoke: <T = unknown>(method: string, payload?: unknown) => Promise<T>
  saveSettings: () => Promise<boolean>
  registerSettings: (adapter: SettingsAdapter) => () => void
}

const props = defineProps<{ host: PageHost }>()
const root = ref<HTMLElement | null>(null)
const importInput = ref<HTMLInputElement | null>(null)
const addLayerTarget = ref<HTMLElement | null>(null)
const layerListTarget = ref<HTMLElement | null>(null)
const rotationTarget = ref<HTMLElement | null>(null)
const toolbarTarget = ref<HTMLElement | null>(null)
const dirty = ref(false)
const valid = ref(true)
const pageError = ref('')
const pageNoticeKey = ref<MessageKey | ''>('')
const unsupportedDevice = ref(false)
const deviceVariant = ref('')
const pageReady = ref(false)
const layerLimitReached = ref(false)
const layerEmpty = ref(true)
const layers = ref<LayerListItem[]>([])
const rotation = ref(0)
const toolbarState = ref<LayerToolbarState>({
  layerCount: 0, selectedCount: 0, type: '', name: '', x: 0, y: 0, maxX: 0, maxY: 0, xDisabled: false,
  canDelete: false, canCopy: false, canPaste: false, canMoveDown: false, canMoveUp: false, canCenter: false, canDistribute: false,
})
const locale = ref(detectLocale())
const chromeState = ref<OledChromeState | null>(null)
const chromeSlots = ref<Record<string, HTMLElement>>({})
const editField = ref('')
const editNonce = ref(0)
let controller: ReturnType<typeof mountOLEDDesigner> | null = null
let unregisterSettings: (() => void) | null = null
let localeObserver: MutationObserver | null = null

const t = (key: MessageKey) => translate(locale.value, key)
const pageNotice = computed(() => pageNoticeKey.value ? t(pageNoticeKey.value) : '')
const noOLEDMessage = computed(() => unsupportedDevice.value ? t('noOLED') : '')
const addLayerLabel = computed(() => t('addLayer'))
const canvasActionsLabel = computed(() => t('canvasActions'))
const layerOptions = computed(() => [
  { label: t('addText'), key: 'text' },
  { label: t('addLine'), key: 'line' },
  { label: t('addRect'), key: 'rect' },
  { label: t('addGraphic'), key: 'graphic' },
  { label: t('addAnimation'), key: 'animation' },
  { label: t('addLive'), key: 'live' },
])
const canvasActionOptions = computed(() => [
  { label: t('restoreDefault'), key: 'restore' },
  { label: t('import'), key: 'import' },
  { label: t('export'), key: 'export', disabled: layerEmpty.value },
])
const chromeLabels = computed(() => ({
  display: t('display'), sleep: t('sleep'), burnIn: t('burnIn'),
  content: t('content'), scale: t('scale'), fontWidth: t('fontWidth'), alignment: t('alignment'),
  alignLeft: t('alignLeft'), alignCenter: t('alignCenter'), alignRight: t('alignRight'),
  graphicMode: t('graphicMode'), customGraphic: t('customGraphic'), ethStatus: t('ethStatus'), hdmiStatus: t('hdmiStatus'),
  hidStatus: t('hidStatus'), wifiStatus: t('wifiStatus'), liveStatus: t('liveStatus'), filled: t('filled'),
  textAreaWidth: t('textAreaWidth'), fullWidth: t('fullWidth'), width: t('width'), height: t('height'),
  statusContent: t('statusContent'), textContent: t('textContent'), fillWhenConnected: t('fillWhenConnected'), borderWhenConnected: t('borderWhenConnected'),
  importImage: t('importImage'), clearPixels: t('clearPixels'), fillPixels: t('fillPixels'), invertPixels: t('invertPixels'),
  fixedCropSize: t('fixedCropSize'), cropWidth: t('cropWidth'), cropHeight: t('cropHeight'),
  outputWidth: t('outputWidth'), outputHeight: t('outputHeight'), trimBlank: t('trimBlank'), cancel: t('cancel'), applyImage: t('applyImage'),
  animationAsset: t('animationAsset'),
}))
const rotationOptions = computed(() => [
  { label: t('rotation0'), value: 0 },
  { label: t('rotation90'), value: 90 },
  { label: t('rotation180'), value: 180 },
  { label: t('rotation270'), value: 270 },
])

function markNativeChrome(doc: Document) {
  doc.querySelectorAll('input, select, button').forEach((node) => {
    if (!(node instanceof HTMLElement)) return
    if (node instanceof HTMLInputElement && node.type === 'file') return
    node.classList.add('oled-native-control')
    node.tabIndex = -1
    node.setAttribute('aria-hidden', 'true')
  })
  doc.querySelectorAll('label').forEach((label) => {
    const control = label.querySelector('input, select, textarea')
    if (!control || (control instanceof HTMLInputElement && control.type === 'file')) return
    const div = doc.createElement('div')
    div.className = label.className
    if (label.id) div.id = label.id
    for (const attr of [...label.attributes]) {
      if (attr.name === 'class' || attr.name === 'id' || attr.name === 'for') continue
      div.setAttribute(attr.name, attr.value)
    }
    if (!div.classList.contains('field') && !div.classList.contains('check-row') && !div.classList.contains('switch-row') && !div.classList.contains('toolbar-control')) {
      div.classList.add('field')
    }
    while (label.firstChild) div.appendChild(label.firstChild)
    label.replaceWith(div)
  })
  doc.querySelector('label[for="sleep"]')?.removeAttribute('for')
  const slotAfter = (id: string, name: string) => {
    const node = doc.getElementById(id)
    if (!node) return
    const slot = doc.createElement('div')
    slot.className = 'naive-slot'
    slot.dataset.slot = name
    node.insertAdjacentElement('afterend', slot)
  }
  ;[
    ['display-enabled', 'display'],
    ['sleep', 'sleep'],
    ['burn-in', 'burn-in'],
    ['element-content', 'content'],
    ['element-scale', 'scale'],
    ['element-font-width', 'font-width'],
    ['element-align', 'align'],
    ['element-graphic-mode', 'graphic-mode'],
    ['element-filled', 'filled'],
    ['element-text-width', 'text-width'],
    ['element-full-width', 'full-width'],
    ['element-x2', 'x2'],
    ['element-y2', 'y2'],
    ['element-width', 'width'],
    ['element-height', 'height'],
    ['element-graphic-content', 'graphic-content'],
    ['element-graphic-filled', 'graphic-filled'],
    ['element-graphic-bordered', 'graphic-bordered'],
    ['element-graphic-width', 'graphic-width'],
    ['element-graphic-height', 'graphic-height'],
    ['element-animation-asset', 'animation-asset'],
    ['element-animation-width', 'animation-width'],
    ['element-animation-height', 'animation-height'],
    ['bitmap-import-fixed-crop', 'bitmap-fixed'],
    ['bitmap-import-crop-width', 'bitmap-crop-width'],
    ['bitmap-import-crop-height', 'bitmap-crop-height'],
    ['bitmap-import-width', 'bitmap-output-width'],
    ['bitmap-import-height', 'bitmap-output-height'],
    ['bitmap-import-trim-blank', 'bitmap-trim'],
  ].forEach(([id, name]) => slotAfter(id, name))
  const appendSlot = (parent: Element | null, name: string, className: string) => {
    if (!parent) return
    const slot = doc.createElement('div')
    slot.className = className
    slot.dataset.slot = name
    parent.append(slot)
  }
  appendSlot(doc.querySelector('.tokens'), 'tokens', 'naive-slot naive-slot-tokens')
  appendSlot(doc.querySelector('.bitmap-actions'), 'bitmap-actions', 'naive-slot naive-slot-actions')
  appendSlot(doc.querySelector('.bitmap-import-actions'), 'bitmap-import-actions', 'naive-slot naive-slot-actions')
}

const parsed = new DOMParser().parseFromString(pageHTML, 'text/html')
parsed.querySelector('.actions')?.remove()
parsed.querySelector('#error')?.remove()
markNativeChrome(parsed)
const pageMarkup = parsed.querySelector('main')?.outerHTML || ''

onMounted(async () => {
  localeObserver = new MutationObserver(() => { locale.value = detectLocale() })
  localeObserver.observe(document.documentElement, { attributes: true, attributeFilter: ['lang'] })
  let initialStatus: unknown
  try {
    initialStatus = await props.host.getStatus()
    const device = initialStatus as { machine?: string; variant?: string }
    deviceVariant.value = String(device.variant || '').toLowerCase()
    if (device.machine !== 'nanokvm' || !['pcie', 'cube'].includes(String(device.variant || '').toLowerCase())) {
      unsupportedDevice.value = true
      return
    }
    pageReady.value = true
    await nextTick()
  } catch (reason) {
    pageError.value = reason instanceof Error ? reason.message : String(reason)
    return
  }
  if (!root.value) return
  addLayerTarget.value = root.value.querySelector('#add-layer-menu-host')
  layerListTarget.value = root.value.querySelector('#elements-list-host')
  rotationTarget.value = root.value.querySelector('#rotation-select-host')
  toolbarTarget.value = root.value.querySelector('#layer-toolbar-host')
  controller = mountOLEDDesigner(root.value, {
    extension: props.host.extension,
    getStatus: props.host.getStatus,
    invoke: props.host.invoke,
    saveSettings: () => props.host.saveSettings(),
    initialStatus,
    locale: locale.value,
    onError: (message: string) => { pageError.value = message },
    onSettingsState: (state: { dirty: boolean; valid: boolean; error: string }) => {
      dirty.value = state.dirty
      valid.value = state.valid
      if (state.error) pageError.value = state.error
      else if (pageError.value) pageError.value = ''
    },
    onLayerState: (state: { full: boolean; empty: boolean }) => {
      layerLimitReached.value = state.full
      layerEmpty.value = state.empty
    },
    onLayersState: (items: LayerListItem[]) => { layers.value = items },
    onRotationState: (value: number) => { rotation.value = value },
    onToolbarState: (state: LayerToolbarState) => { toolbarState.value = state },
    onChromeState: (state: OledChromeState) => { chromeState.value = state },
    onEditField: (id: string) => {
      editField.value = id
      editNonce.value += 1
      return true
    },
  })
  const slots: Record<string, HTMLElement> = {}
  root.value.querySelectorAll<HTMLElement>('[data-slot]').forEach((node) => {
    const name = node.dataset.slot
    if (name) slots[name] = node
  })
  chromeSlots.value = slots
  unregisterSettings = props.host.registerSettings({
    dirty,
    valid,
    collect: controller.collectSettings,
    reset: controller.resetSettings,
  })
})

watch(locale, (value) => { controller?.setLocale(value) })

function addLayer(key: string | number) {
  controller?.addElement(String(key))
}

function selectLayer(id: string, additive: boolean) {
  controller?.selectLayer(id, additive)
}

function toggleLayer(id: string) {
  controller?.toggleLayer(id)
}

function reorderLayer(source: string, target: string, before: boolean) {
  controller?.reorderLayer(source, target, before)
}

function runCanvasAction(key: string | number) {
  if (key === 'restore') restoreDefaultCanvas()
  else if (key === 'import') chooseImport()
  else if (key === 'export') exportLayers()
}

function updateRotation(value: string | number | null) {
  if (value !== null) controller?.setRotation(Number(value))
}

function updateToolbar(field: 'name' | 'x' | 'y', value: string | number) {
  controller?.updateToolbar(field, value)
}

function runToolbarAction(action: string) {
  controller?.runToolbarAction(action)
}

function applyChrome(kind: string, value: unknown) {
  controller?.applyChrome(kind, value)
}

async function saveVariables() {
  if (!controller) return
  pageError.value = ''
  if (await controller.saveVariables()) pageNoticeKey.value = 'variablesSaved'
}

function chooseImport() {
  importInput.value?.click()
}

async function importLayers(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (!file || !controller) return
  try {
    controller.importLayers(await file.text())
    pageError.value = ''
    pageNoticeKey.value = 'imported'
  } catch (reason) {
    pageNoticeKey.value = ''
    pageError.value = reason instanceof Error ? reason.message : String(reason)
  }
}

function exportLayers() {
  if (!controller || layerEmpty.value) return
  const exported = controller.exportLayers()
  const url = URL.createObjectURL(new Blob([exported.content], { type: 'application/json' }))
  const link = document.createElement('a')
  link.href = url
  link.download = exported.filename
  document.body.append(link)
  link.click()
  link.remove()
  window.setTimeout(() => URL.revokeObjectURL(url), 0)
}

function restoreDefaultCanvas() {
  if (!controller) return
  controller.restoreDefaultCanvas()
  pageError.value = ''
  pageNoticeKey.value = 'restoredDefault'
}

onBeforeUnmount(() => {
  localeObserver?.disconnect()
  localeObserver = null
  unregisterSettings?.()
  unregisterSettings = null
  controller?.dispose()
  controller = null
  addLayerTarget.value = null
  layerListTarget.value = null
  rotationTarget.value = null
  toolbarTarget.value = null
})
</script>

<template>
  <div class="onekvm-oled-page">
    <NAlert v-if="pageError" type="error" :show-icon="false" closable @close="pageError = ''">
      {{ pageError }}
    </NAlert>
    <NAlert v-if="pageNotice" type="success" :show-icon="false" closable @close="pageNoticeKey = ''">
      {{ pageNotice }}
    </NAlert>
    <NAlert v-if="noOLEDMessage" type="warning" :show-icon="false">
      {{ noOLEDMessage }}
    </NAlert>
    <input ref="importInput" class="hidden" type="file" accept=".json,application/json" @change="importLayers">
    <NTabs v-if="pageReady" type="line" animated class="oled-page-tabs">
      <NTabPane name="canvas" :tab="t('canvasTab')" display-directive="show">
        <div ref="root" v-html="pageMarkup" />
      </NTabPane>
      <NTabPane name="variables" :tab="t('variables')" display-directive="show">
        <VariablesPanel
          :locale="locale"
          :variables="chromeState?.variables ?? []"
          :dirty="chromeState?.variablesDirty === true"
          :save="saveVariables"
          @update="applyChrome"
        />
      </NTabPane>
      <NTabPane name="media" :tab="t('mediaTab')" display-directive="show:lazy">
        <MediaLibrary :host="props.host" :locale="locale" :device-variant="deviceVariant" />
      </NTabPane>
    </NTabs>
    <Teleport v-if="layerListTarget" :to="layerListTarget">
      <LayerList
        :layers="layers"
        :labels="{ showLayer: t('showLayer'), hideLayer: t('hideLayer'), empty: t('selectHint'), text: t('text'), line: t('line'), rect: t('rect'), graphic: t('graphic'), animation: t('animation') }"
        @select="selectLayer"
        @toggle="toggleLayer"
        @reorder="reorderLayer"
      />
    </Teleport>
    <Teleport v-if="addLayerTarget" :to="addLayerTarget">
      <NDropdown
        trigger="click"
        placement="bottom-end"
        :options="canvasActionOptions"
        @select="runCanvasAction"
      >
        <NButton size="small" quaternary>
          {{ canvasActionsLabel }}
        </NButton>
      </NDropdown>
      <NDropdown
        trigger="click"
        placement="bottom-end"
        :options="layerOptions"
        :disabled="layerLimitReached"
        @select="addLayer"
      >
        <NButton size="small" secondary :disabled="layerLimitReached">
          {{ addLayerLabel }}
        </NButton>
      </NDropdown>
    </Teleport>
    <Teleport v-if="rotationTarget" :to="rotationTarget">
      <NSelect
        :value="rotation"
        :options="rotationOptions"
        :consistent-menu-width="true"
        :aria-label="t('direction')"
        @update:value="updateRotation"
      />
    </Teleport>
    <Teleport v-if="toolbarTarget" :to="toolbarTarget">
      <LayerToolbar
        :state="toolbarState"
        :labels="{
          empty: t('toolbarEmptyHint'), select: t('toolbarSelectHint'), selected: t('selectedCount'), layerName: t('layerName'),
          text: t('text'), line: t('line'), rect: t('rect'), graphic: t('graphic'), animation: t('animation'),
          centerHorizontal: t('centerHorizontal'), centerVertical: t('centerVertical'), distributeHorizontal: t('distributeHorizontal'), distributeVertical: t('distributeVertical'),
          copy: t('copy'), paste: t('paste'), moveDown: t('moveDown'), moveUp: t('moveUp'), delete: t('delete'),
        }"
        @update="updateToolbar"
        @action="runToolbarAction"
        :edit-field="editField"
        :edit-nonce="editNonce"
      />
    </Teleport>
    <OledChrome
      v-if="chromeState && Object.keys(chromeSlots).length"
      :slots="chromeSlots"
      :state="chromeState"
      :labels="chromeLabels"
      :edit-field="editField"
      :edit-nonce="editNonce"
      @update="applyChrome"
    />
  </div>
</template>
