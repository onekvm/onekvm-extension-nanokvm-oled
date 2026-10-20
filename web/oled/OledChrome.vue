<script setup lang="ts">
import { nextTick, ref, watch } from 'vue'
import { NButton, NCheckbox, NInput, NInputNumber, NSelect, NSwitch, type SelectOption } from 'naive-ui'

export interface OledChromeVariable { key: string; value: string }
export type OledChromeOption = SelectOption

export interface OledChromeInspector {
  type: string
  showContent: boolean
  showScale: boolean
  showFontWidth: boolean
  showAlign: boolean
  showFilled: boolean
  showGraphicMode: boolean
  showText: boolean
  showLine: boolean
  showRect: boolean
  showGraphic: boolean
  showAnimation: boolean
  showStatusStyle: boolean
  customPixels: boolean
  content: string
  scale: number
  fontWidth: number
  align: string
  filled: boolean
  graphicMode: string
  textWidth: number
  textWidthDisabled: boolean
  fullWidth: boolean
  x2: number
  y2: number
  width: number
  height: number
  maxX: number
  maxY: number
  maxWidth: number
  maxHeight: number
  graphicContent: string
  graphicFilled: boolean
  graphicBordered: boolean
  animationAsset: string
  animationOptions: SelectOption[]
  tokens: string[]
}

export interface OledBitmapImportState {
  fixedCrop: boolean
  cropWidth: number
  cropHeight: number
  cropMaxWidth: number
  cropMaxHeight: number
  outputWidth: number
  outputHeight: number
  outputMaxWidth: number
  outputMaxHeight: number
  trimBlank: boolean
}

export interface OledChromeState {
  displayEnabled: boolean
  sleepSeconds: number
  burnIn: boolean
  variables: OledChromeVariable[]
  variablesDirty: boolean
  inspector: OledChromeInspector | null
  bitmapImport: OledBitmapImportState | null
}

const props = defineProps<{
  slots: Record<string, HTMLElement | undefined>
  state: OledChromeState
  labels: Record<string, string>
  editField: string
  editNonce: number
}>()

const emit = defineEmits<{ update: [kind: string, value: unknown] }>()
const contentInput = ref<{ focus?: () => void } | null>(null)

const scaleOptions = [
  { label: '1×', value: 1 },
  { label: '2×', value: 2 },
]
const fontOptions = [
  { label: '3×5', value: 3 },
  { label: '4×5', value: 4 },
  { label: '5×5', value: 5 },
]

watch(() => props.editNonce, async () => {
  if (props.editField !== 'element-content') return
  await nextTick()
  contentInput.value?.focus?.()
}, { flush: 'post' })

function emitNumber(kind: string, value: number | null) {
  if (value === null) {
    if (kind === 'bitmap-crop-width' || kind === 'bitmap-crop-height') emit('update', kind, '')
    return
  }
  emit('update', kind, value)
}

</script>

<template>
  <Teleport v-if="slots.display" :to="slots.display">
    <NSwitch :value="state.displayEnabled" :aria-label="labels.display" @update:value="emit('update', 'display', $event)" />
  </Teleport>
  <Teleport v-if="slots.sleep" :to="slots.sleep">
    <NInputNumber :value="state.sleepSeconds" :min="0" :max="86400" :step="1" :aria-label="labels.sleep" @update:value="emitNumber('sleep', $event)" />
  </Teleport>
  <Teleport v-if="slots['burn-in']" :to="slots['burn-in']">
    <NSwitch :value="state.burnIn" :aria-label="labels.burnIn" @update:value="emit('update', 'burn-in', $event)" />
  </Teleport>
  <template v-if="state.inspector">
    <Teleport v-if="slots.content && state.inspector.showContent" :to="slots.content">
      <NInput ref="contentInput" :value="state.inspector.content" :maxlength="128" :aria-label="labels.content" @update:value="emit('update', 'content', $event)" />
    </Teleport>
    <Teleport v-if="slots.scale && state.inspector.showScale" :to="slots.scale">
      <NSelect :value="state.inspector.scale" :options="scaleOptions" :aria-label="labels.scale" @update:value="emit('update', 'scale', $event)" />
    </Teleport>
    <Teleport v-if="slots['font-width'] && state.inspector.showFontWidth" :to="slots['font-width']">
      <NSelect :value="state.inspector.fontWidth" :options="fontOptions" :aria-label="labels.fontWidth" @update:value="emit('update', 'font-width', $event)" />
    </Teleport>
    <Teleport v-if="slots.align && state.inspector.showAlign" :to="slots.align">
      <NSelect
        :value="state.inspector.align"
        :options="[
          { label: labels.alignLeft, value: 'left' },
          { label: labels.alignCenter, value: 'center' },
          { label: labels.alignRight, value: 'right' },
        ]"
        :aria-label="labels.alignment"
        @update:value="emit('update', 'align', $event)"
      />
    </Teleport>
    <Teleport v-if="slots['graphic-mode'] && state.inspector.showGraphicMode" :to="slots['graphic-mode']">
      <NSelect
        :value="state.inspector.graphicMode"
        :options="[
          { label: labels.customGraphic, value: 'custom' },
          { label: labels.ethStatus, value: 'eth' },
          { label: labels.hdmiStatus, value: 'hdmi' },
          { label: labels.hidStatus, value: 'hid' },
          { label: labels.wifiStatus, value: 'wifi' },
          { label: labels.liveStatus, value: 'live' },
        ]"
        :aria-label="labels.graphicMode"
        @update:value="emit('update', 'graphic-mode', $event)"
      />
    </Teleport>
    <Teleport v-if="slots.filled && state.inspector.showFilled" :to="slots.filled">
      <NCheckbox :checked="state.inspector.filled" @update:checked="emit('update', 'filled', $event)">{{ labels.filled }}</NCheckbox>
    </Teleport>
    <Teleport v-if="slots['text-width'] && state.inspector.showText" :to="slots['text-width']">
      <NInputNumber :value="state.inspector.textWidth" size="small" :show-button="false" :min="1" :max="state.inspector.maxWidth" :disabled="state.inspector.textWidthDisabled" :aria-label="labels.textAreaWidth" @update:value="emitNumber('text-width', $event)" />
    </Teleport>
    <Teleport v-if="slots['full-width'] && state.inspector.showText" :to="slots['full-width']">
      <NCheckbox :checked="state.inspector.fullWidth" @update:checked="emit('update', 'full-width', $event)">{{ labels.fullWidth }}</NCheckbox>
    </Teleport>
    <Teleport v-if="slots.tokens && state.inspector.showText" :to="slots.tokens">
      <NButton v-for="token in state.inspector.tokens" :key="token" class="oled-token" size="tiny" secondary @click="emit('update', 'token', token)">{{ token }}</NButton>
    </Teleport>
    <Teleport v-if="slots.x2 && state.inspector.showLine" :to="slots.x2">
      <NInputNumber :value="state.inspector.x2" size="small" :show-button="false" :min="0" :max="state.inspector.maxX" aria-label="X2" @update:value="emitNumber('x2', $event)" />
    </Teleport>
    <Teleport v-if="slots.y2 && state.inspector.showLine" :to="slots.y2">
      <NInputNumber :value="state.inspector.y2" size="small" :show-button="false" :min="0" :max="state.inspector.maxY" aria-label="Y2" @update:value="emitNumber('y2', $event)" />
    </Teleport>
    <Teleport v-if="slots.width && state.inspector.showRect" :to="slots.width">
      <NInputNumber :value="state.inspector.width" size="small" :show-button="false" :min="1" :max="state.inspector.maxWidth" :aria-label="labels.width" @update:value="emitNumber('width', $event)" />
    </Teleport>
    <Teleport v-if="slots.height && state.inspector.showRect" :to="slots.height">
      <NInputNumber :value="state.inspector.height" size="small" :show-button="false" :min="1" :max="state.inspector.maxHeight" :aria-label="labels.height" @update:value="emitNumber('height', $event)" />
    </Teleport>
    <Teleport v-if="slots['graphic-content'] && state.inspector.showStatusStyle" :to="slots['graphic-content']">
      <NSelect
        :value="state.inspector.graphicContent"
        :options="[
          { label: labels.textContent, value: 'text' },
          { label: labels.customGraphic, value: 'custom' },
        ]"
        :aria-label="labels.statusContent"
        @update:value="emit('update', 'graphic-content', $event)"
      />
    </Teleport>
    <Teleport v-if="slots['graphic-filled'] && state.inspector.showStatusStyle" :to="slots['graphic-filled']">
      <NCheckbox :checked="state.inspector.graphicFilled" @update:checked="emit('update', 'graphic-filled', $event)">{{ labels.fillWhenConnected }}</NCheckbox>
    </Teleport>
    <Teleport v-if="slots['graphic-bordered'] && state.inspector.showStatusStyle" :to="slots['graphic-bordered']">
      <NCheckbox :checked="state.inspector.graphicBordered" @update:checked="emit('update', 'graphic-bordered', $event)">{{ labels.borderWhenConnected }}</NCheckbox>
    </Teleport>
    <Teleport v-if="slots['graphic-width'] && state.inspector.customPixels" :to="slots['graphic-width']">
      <NInputNumber :value="state.inspector.width" size="small" :show-button="false" :min="1" :max="state.inspector.maxWidth" :aria-label="labels.width" @update:value="emitNumber('graphic-width', $event)" />
    </Teleport>
    <Teleport v-if="slots['graphic-height'] && state.inspector.customPixels" :to="slots['graphic-height']">
      <NInputNumber :value="state.inspector.height" size="small" :show-button="false" :min="1" :max="state.inspector.maxHeight" :aria-label="labels.height" @update:value="emitNumber('graphic-height', $event)" />
    </Teleport>
    <Teleport v-if="slots['bitmap-actions'] && state.inspector.customPixels" :to="slots['bitmap-actions']">
      <NButton size="small" secondary @click="emit('update', 'bitmap-import-open', null)">{{ labels.importImage }}</NButton>
      <NButton size="small" secondary @click="emit('update', 'bitmap-clear', null)">{{ labels.clearPixels }}</NButton>
      <NButton size="small" secondary @click="emit('update', 'bitmap-fill', null)">{{ labels.fillPixels }}</NButton>
      <NButton size="small" secondary @click="emit('update', 'bitmap-invert', null)">{{ labels.invertPixels }}</NButton>
    </Teleport>
    <Teleport v-if="slots['animation-asset'] && state.inspector.showAnimation" :to="slots['animation-asset']">
      <NSelect :value="state.inspector.animationAsset" :options="state.inspector.animationOptions" :aria-label="labels.animationAsset" @update:value="emit('update', 'animation-asset', $event)" />
    </Teleport>
    <Teleport v-if="slots['animation-width'] && state.inspector.showAnimation" :to="slots['animation-width']">
      <NInputNumber :value="state.inspector.width" size="small" :show-button="false" :min="1" :max="state.inspector.maxWidth" :aria-label="labels.width" @update:value="emitNumber('animation-width', $event)" />
    </Teleport>
    <Teleport v-if="slots['animation-height'] && state.inspector.showAnimation" :to="slots['animation-height']">
      <NInputNumber :value="state.inspector.height" size="small" :show-button="false" :min="1" :max="state.inspector.maxHeight" :aria-label="labels.height" @update:value="emitNumber('animation-height', $event)" />
    </Teleport>
  </template>

  <template v-if="state.bitmapImport">
    <Teleport v-if="slots['bitmap-fixed']" :to="slots['bitmap-fixed']">
      <NCheckbox :checked="state.bitmapImport.fixedCrop" @update:checked="emit('update', 'bitmap-fixed-crop', $event)">{{ labels.fixedCropSize }}</NCheckbox>
    </Teleport>
    <Teleport v-if="slots['bitmap-crop-width']" :to="slots['bitmap-crop-width']">
      <NInputNumber :value="state.bitmapImport.cropWidth" size="small" :show-button="false" :min="1" :max="state.bitmapImport.cropMaxWidth" :disabled="!state.bitmapImport.fixedCrop" :aria-label="labels.cropWidth" @update:value="emitNumber('bitmap-crop-width', $event)" />
    </Teleport>
    <Teleport v-if="slots['bitmap-crop-height']" :to="slots['bitmap-crop-height']">
      <NInputNumber :value="state.bitmapImport.cropHeight" size="small" :show-button="false" :min="1" :max="state.bitmapImport.cropMaxHeight" :disabled="!state.bitmapImport.fixedCrop" :aria-label="labels.cropHeight" @update:value="emitNumber('bitmap-crop-height', $event)" />
    </Teleport>
    <Teleport v-if="slots['bitmap-output-width']" :to="slots['bitmap-output-width']">
      <NInputNumber :value="state.bitmapImport.outputWidth" size="small" :show-button="false" :min="1" :max="state.bitmapImport.outputMaxWidth" :aria-label="labels.outputWidth" @update:value="emitNumber('bitmap-output-width', $event)" />
    </Teleport>
    <Teleport v-if="slots['bitmap-output-height']" :to="slots['bitmap-output-height']">
      <NInputNumber :value="state.bitmapImport.outputHeight" size="small" :show-button="false" :min="1" :max="state.bitmapImport.outputMaxHeight" :aria-label="labels.outputHeight" @update:value="emitNumber('bitmap-output-height', $event)" />
    </Teleport>
    <Teleport v-if="slots['bitmap-trim']" :to="slots['bitmap-trim']">
      <NCheckbox :checked="state.bitmapImport.trimBlank" @update:checked="emit('update', 'bitmap-trim', $event)">{{ labels.trimBlank }}</NCheckbox>
    </Teleport>
    <Teleport v-if="slots['bitmap-import-actions']" :to="slots['bitmap-import-actions']">
      <NButton size="small" secondary @click="emit('update', 'bitmap-cancel', null)">{{ labels.cancel }}</NButton>
      <NButton size="small" type="primary" @click="emit('update', 'bitmap-apply', null)">{{ labels.applyImage }}</NButton>
    </Teleport>
  </template>
</template>
