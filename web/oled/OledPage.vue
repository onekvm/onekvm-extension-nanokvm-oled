<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { NAlert, NButton, NDropdown, NSelect } from 'naive-ui'

import pageHTML from './index.html?raw'
import './font-cjk-12.js'
import { detectLocale, translate, type MessageKey } from './i18n'
import { mountOLEDDesigner } from './oled.js'

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
  extension: Readonly<{ value: { settings: Settings } }>
  getStatus: () => Promise<unknown>
  registerSettings: (adapter: SettingsAdapter) => () => void
}

const props = defineProps<{ host: PageHost }>()
const root = ref<HTMLElement | null>(null)
const importInput = ref<HTMLInputElement | null>(null)
const addLayerTarget = ref<HTMLElement | null>(null)
const rotationTarget = ref<HTMLElement | null>(null)
const dirty = ref(false)
const valid = ref(true)
const pageError = ref('')
const pageNoticeKey = ref<MessageKey | ''>('')
const unsupportedDevice = ref(false)
const pageReady = ref(false)
const layerLimitReached = ref(false)
const layerEmpty = ref(true)
const rotation = ref(0)
const locale = ref(detectLocale())
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
  { label: t('addLive'), key: 'live' },
])
const canvasActionOptions = computed(() => [
  { label: t('restoreDefault'), key: 'restore' },
  { label: t('import'), key: 'import' },
  { label: t('export'), key: 'export', disabled: layerEmpty.value },
])
const rotationOptions = computed(() => [
  { label: t('rotation0'), value: 0 },
  { label: t('rotation90'), value: 90 },
  { label: t('rotation180'), value: 180 },
  { label: t('rotation270'), value: 270 },
])

const parsed = new DOMParser().parseFromString(pageHTML, 'text/html')
parsed.querySelector('.actions')?.remove()
parsed.querySelector('#error')?.remove()
const pageMarkup = parsed.querySelector('main')?.outerHTML || ''

onMounted(async () => {
  localeObserver = new MutationObserver(() => { locale.value = detectLocale() })
  localeObserver.observe(document.documentElement, { attributes: true, attributeFilter: ['lang'] })
  let initialStatus: unknown
  try {
    initialStatus = await props.host.getStatus()
    const device = initialStatus as { machine?: string; variant?: string }
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
  rotationTarget.value = root.value.querySelector('#rotation-select-host')
  controller = mountOLEDDesigner(root.value, {
    extension: props.host.extension,
    getStatus: props.host.getStatus,
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
    onRotationState: (value: number) => { rotation.value = value },
  })
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

function runCanvasAction(key: string | number) {
  if (key === 'restore') restoreDefaultCanvas()
  else if (key === 'import') chooseImport()
  else if (key === 'export') exportLayers()
}

function updateRotation(value: string | number | null) {
  if (value !== null) controller?.setRotation(Number(value))
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
  rotationTarget.value = null
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
    <div v-if="pageReady" ref="root" v-html="pageMarkup" />
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
  </div>
</template>
