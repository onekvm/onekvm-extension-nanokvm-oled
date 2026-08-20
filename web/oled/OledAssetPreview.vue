<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { frameAt, parseOan } from './oan'

export interface OledAssetPreviewData {
  id?: string
  name: string
  width: number
  height: number
  fps: number
  frames: number
  size: number
  preview_base64?: string
  data?: Uint8Array
}

const props = defineProps<{ asset: OledAssetPreviewData; large?: boolean; bare?: boolean; displayWidth?: number; displayHeight?: number }>()
const screenWidth = computed(() => props.displayWidth || props.asset.width)
const screenHeight = computed(() => props.displayHeight || props.asset.height)
const canvas = ref<HTMLCanvasElement | null>(null)
let timer: number | null = null

function stop() {
  if (timer !== null) window.clearInterval(timer)
  timer = null
}

function decodePreview(asset: OledAssetPreviewData) {
  if (asset.data instanceof Uint8Array) return { bytes: asset.data, full: true }
  const binary = atob(asset.preview_base64 || '')
  return { bytes: Uint8Array.from(binary, (value) => value.charCodeAt(0)), full: false }
}

function draw() {
  stop()
  const target = canvas.value
  if (!target) return
  const asset = props.asset
  const displayWidth = screenWidth.value
  const displayHeight = screenHeight.value
  const { bytes, full } = decodePreview(asset)
  const frameBytes = asset.width * asset.height / 8
  let parsed = null
  if (full) {
    try { parsed = parseOan(bytes) } catch { parsed = null }
  }
  if (!parsed && bytes.length < frameBytes) return
  target.width = displayWidth
  target.height = displayHeight
  const context = target.getContext('2d')
  if (!context) return
  const image = context.createImageData(displayWidth, displayHeight)
  const started = performance.now()
  const paint = () => {
    let source = bytes
    let offset = 0
    let width = asset.width
    let height = asset.height
    if (parsed) {
      const step = frameAt(parsed, performance.now() - started)
      source = parsed.pixels
      offset = step.offset
      width = parsed.width
      height = parsed.height
    }
    image.data.fill(0)
    const originX = Math.floor((displayWidth - width) / 2)
    const originY = Math.floor((displayHeight - height) / 2)
    for (let y = 0; y < height; y += 1) for (let x = 0; x < width; x += 1) {
      const targetX = originX + x
      const targetY = originY + y
      if (targetX < 0 || targetX >= displayWidth || targetY < 0 || targetY >= displayHeight) continue
      const on = (source[offset + (y >> 3) * width + x] & (1 << (y & 7))) !== 0
      const pixel = (targetY * displayWidth + targetX) * 4
      image.data[pixel] = on ? 228 : 0
      image.data[pixel + 1] = on ? 255 : 5
      image.data[pixel + 2] = on ? 238 : 7
      image.data[pixel + 3] = 255
    }
    context.putImageData(image, 0, 0)
  }
  paint()
  if (parsed && parsed.steps.length > 1) timer = window.setInterval(paint, 33)
}

watch(() => [props.asset, props.displayWidth, props.displayHeight], draw, { deep: true, flush: 'post' })
onMounted(draw)
onBeforeUnmount(stop)
</script>

<template>
  <div :class="bare ? 'asset-thumbnail' : ['asset-screen', { 'asset-screen-large': large }]">
    <span v-if="!bare" class="asset-brand">NANOKVM · OLED</span>
    <div class="asset-glass" :style="{ aspectRatio: `${screenWidth} / ${screenHeight}` }">
      <canvas ref="canvas" :aria-label="asset.name" />
    </div>
    <i v-if="!bare" class="asset-led" aria-hidden="true" />
  </div>
</template>
