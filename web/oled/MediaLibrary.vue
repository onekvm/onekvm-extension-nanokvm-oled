<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { NAlert, NButton, NCard, NCollapse, NCollapseItem, NInput, NInputNumber, NModal, NPopconfirm, NProgress, NSelect, NTag } from 'naive-ui'
import { Download, LockKeyhole, UnlockKeyhole, Upload } from '@lucide/vue'
import { translate, type MessageKey } from './i18n'
import OledAssetPreview, { type OledAssetPreviewData } from './OledAssetPreview.vue'

interface ExtensionHost { extension: Readonly<{ value: { id?: string } }> }

interface Props {
  host: ExtensionHost
  locale: string
  deviceVariant?: string
}

interface Asset extends OledAssetPreviewData {
  duration_seconds?: number
}

const props = defineProps<Props>()
const t = (key: MessageKey) => translate(props.locale, key)
const fileInput = ref<HTMLInputElement | null>(null)
const assetFileInput = ref<HTMLInputElement | null>(null)
const selectedFile = ref<File | null>(null)
const pending = ref<Asset | null>(null)
const assets = ref<Asset[]>([])
const search = ref('')
const initialResolution = props.deviceVariant === 'pcie' ? '64x32' : '128x64'
const displayWidth = computed(() => props.deviceVariant === 'pcie' ? 64 : 128)
const displayHeight = computed(() => props.deviceVariant === 'pcie' ? 32 : 64)
const resolution = ref(initialResolution)
const width = ref(initialResolution === '64x32' ? 64 : 128)
const height = ref(initialResolution === '64x32' ? 32 : 64)
const fps = ref(30)
const fit = ref('cover')
const ratioLocked = ref(true)
const aspectRatio = ref(2)
const busy = ref(false)
const converting = ref(false)
const conversionCancelled = ref(false)
const progress = ref(0)
const totalFrames = ref(0)
const convertedFrames = ref(0)
const etaSeconds = ref(0)
const conversionStartedAt = ref(0)
const progressDebounceDelay = 120
const progressDebounceMaxWait = 500
const status = ref('')
const error = ref('')
const deleting = ref<string | null>(null)
const previewing = ref<string | null>(null)
const savedPreview = ref<Asset | null>(null)
const renameOpen = ref(false)
const renameTarget = ref<Asset | null>(null)
const renameName = ref('')
const renaming = ref<string | null>(null)
const downloading = ref<string | null>(null)

const resolutionOptions = computed(() => [
  { label: '128 × 64 (Cube)', value: '128x64' },
  { label: '64 × 32 (PCIe)', value: '64x32' },
  { label: t('mediaCustomResolution'), value: 'custom' },
])
const fpsOptions = [30, 20, 15].map((value) => ({ label: `${value} FPS`, value }))
const fitOptions = computed(() => [
  { label: t('fitContain'), value: 'contain' },
  { label: t('fitCover'), value: 'cover' },
])
const filteredAssets = computed(() => {
  const query = search.value.trim().toLowerCase()
  return query ? assets.value.filter((asset) => asset.name.toLowerCase().includes(query)) : assets.value
})
const fileName = computed(() => selectedFile.value?.name || t('noVideo'))
const canConvert = computed(() => Boolean(selectedFile.value) && !busy.value && !pending.value)
const bytes = (value: number) => value < 1024 ? `${value} B` : value < 1024 * 1024 ? `${(value / 1024).toFixed(1)} KB` : `${(value / (1024 * 1024)).toFixed(1)} MB`
const duration = (value: number) => `${Math.floor(Math.max(0, Math.round(value)) / 60)}:${String(Math.max(0, Math.round(value)) % 60).padStart(2, '0')}`

watch(width, (value) => {
  if (resolution.value === 'custom' && ratioLocked.value && aspectRatio.value > 0) {
    height.value = Math.max(8, Math.min(256, Math.round((Number(value) / aspectRatio.value) / 8) * 8))
  }
})

function route(path = '') {
  const extensionID = props.host.extension.value.id || 'nanokvm-oled'
  return `${window.location.origin}/api/plugins/${encodeURIComponent(extensionID)}/assets${path}`
}

function setFile(file: File | null) {
  if (!file || busy.value || pending.value) return
  if (!String(file.type || '').startsWith('video/') && !/\.(mp4|webm|mov)$/i.test(file.name)) return
  selectedFile.value = file
  status.value = ''
  error.value = ''
}

function chooseFile() { fileInput.value?.click() }
function handleFileChange(event: Event) {
  const input = event.target as HTMLInputElement
  setFile(input.files?.[0] || null)
  input.value = ''
}
function handleDrop(event: DragEvent) { setFile(event.dataTransfer?.files?.[0] || null) }
function handleDropzoneKey(event: KeyboardEvent) {
  if (event.key === 'Enter' || event.key === ' ') {
    event.preventDefault()
    chooseFile()
  }
}

function chooseAssetFile() { assetFileInput.value?.click() }

function decodeAssetFile(file: File, data: Uint8Array): Asset {
  if (data.byteLength < 14 || data[0] !== 0x4f || data[1] !== 0x41 || data[2] !== 0x4e || data[3] !== 0x31) throw new Error(t('invalidAssetFile'))
  const header = new DataView(data.buffer, data.byteOffset, data.byteLength)
  const assetWidth = header.getUint16(4, true)
  const assetHeight = header.getUint16(6, true)
  const assetFps = header.getUint16(8, true)
  const assetFrames = header.getUint32(10, true)
  const expectedSize = 14 + assetFrames * assetWidth * assetHeight / 8
  if (assetWidth < 8 || assetWidth > 256 || assetHeight < 8 || assetHeight > 256 || assetHeight % 8 || assetFps < 1 || assetFps > 120 || assetFrames < 1 || expectedSize !== data.byteLength || data.byteLength > 32 * 1024 * 1024) throw new Error(t('invalidAssetFile'))
  const name = (file.name.replace(/\.oan$/i, '') || 'animation').replace(/[^a-zA-Z0-9_.-]/g, '_').slice(0, 92) || 'animation'
  return { id: 'pending', name, width: assetWidth, height: assetHeight, fps: assetFps, frames: assetFrames, duration_seconds: assetFrames / assetFps, size: data.byteLength, data }
}

async function handleAssetFileChange(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (!file || busy.value || pending.value) return
  error.value = ''
  status.value = ''
  try {
    const data = new Uint8Array(await file.arrayBuffer())
    pending.value = decodeAssetFile(file, data)
    selectedFile.value = null
    status.value = t('previewReady').replace('{frames}', String(pending.value.frames)).replace('{size}', bytes(pending.value.size))
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : t('invalidAssetFile')
  }
}

function seek(video: HTMLVideoElement, time: number) {
  return new Promise<void>((resolve, reject) => {
    if (Math.abs(video.currentTime - time) < 0.0005) return resolve()
    const done = () => { video.removeEventListener('seeked', done); video.removeEventListener('error', fail); resolve() }
    const fail = () => { video.removeEventListener('seeked', done); video.removeEventListener('error', fail); reject(video.error || new Error('Video seek failed')) }
    video.addEventListener('seeked', done, { once: true }); video.addEventListener('error', fail, { once: true }); video.currentTime = time
  })
}

async function convert() {
  if (!selectedFile.value || busy.value || pending.value) return
  busy.value = true; converting.value = true; conversionCancelled.value = false; error.value = ''; status.value = ''; progress.value = 0; totalFrames.value = 0; convertedFrames.value = 0; etaSeconds.value = 0; conversionStartedAt.value = performance.now()
  let progressTimer: number | null = null
  let progressFirstPendingAt = 0
  let pendingFrame = 0
  let pendingTimestamp = conversionStartedAt.value
  let frameCount = 1
  const flushProgress = () => {
    if (progressTimer !== null) window.clearTimeout(progressTimer)
    progressTimer = null
    progressFirstPendingAt = 0
    convertedFrames.value = pendingFrame
    progress.value = pendingFrame / frameCount
    const elapsed = (pendingTimestamp - conversionStartedAt.value) / 1000
    etaSeconds.value = progress.value > 0 ? Math.max(0, elapsed * (1 - progress.value) / progress.value) : 0
  }
  const scheduleProgress = (frame: number, timestamp: number) => {
    pendingFrame = frame
    pendingTimestamp = timestamp
    if (!progressFirstPendingAt) progressFirstPendingAt = timestamp
    if (progressTimer !== null) window.clearTimeout(progressTimer)
    const waited = timestamp - progressFirstPendingAt
    progressTimer = window.setTimeout(flushProgress, waited >= progressDebounceMaxWait ? 0 : progressDebounceDelay)
  }
  const video = document.createElement('video'); video.muted = true; video.playsInline = true
  const canvas = document.createElement('canvas'); const context = canvas.getContext('2d', { willReadFrequently: true })
  if (!context) { busy.value = false; error.value = 'Canvas is unavailable'; return }
  const objectURL = URL.createObjectURL(selectedFile.value)
  try {
    video.src = objectURL
    await new Promise<void>((resolve, reject) => { video.onloadedmetadata = () => resolve(); video.onerror = () => reject(video.error || new Error('Video cannot be decoded')) })
    if (conversionCancelled.value) throw new Error('conversion cancelled')
    const videoDuration = Number.isFinite(video.duration) ? video.duration : 0
    if (!videoDuration) throw new Error('Video has no duration')
    aspectRatio.value = video.videoWidth / video.videoHeight
    let [targetWidth, targetHeight] = resolution.value === 'custom' ? [Number(width.value), Number(height.value)] : resolution.value.split('x').map(Number)
    if (resolution.value === 'custom' && ratioLocked.value) { targetHeight = Math.max(8, Math.round((targetWidth / aspectRatio.value) / 8) * 8); height.value = targetHeight }
    if (!Number.isInteger(targetWidth) || !Number.isInteger(targetHeight) || targetWidth < 8 || targetWidth > 256 || targetHeight < 8 || targetHeight > 256 || targetHeight % 8) throw new Error('Invalid OLED resolution')
    frameCount = Math.max(1, Math.floor(videoDuration * fps.value)); totalFrames.value = frameCount; const outputSize = 14 + frameCount * targetWidth * targetHeight / 8
    if (outputSize > 32 * 1024 * 1024) throw new Error(t('assetTooLarge'))
    canvas.width = targetWidth; canvas.height = targetHeight
    const output = new Uint8Array(outputSize); const header = new DataView(output.buffer)
    header.setUint8(0, 0x4f); header.setUint8(1, 0x41); header.setUint8(2, 0x4e); header.setUint8(3, 0x31); header.setUint16(4, targetWidth, true); header.setUint16(6, targetHeight, true); header.setUint16(8, fps.value, true); header.setUint32(10, frameCount, true)
    for (let frame = 0; frame < frameCount; frame += 1) {
      if (conversionCancelled.value) throw new Error('conversion cancelled')
      await seek(video, Math.min(videoDuration - 0.001, frame / fps.value)); context.fillStyle = '#000'; context.fillRect(0, 0, targetWidth, targetHeight)
      if (conversionCancelled.value) throw new Error('conversion cancelled')
      const scale = fit.value === 'cover' ? Math.max(targetWidth / video.videoWidth, targetHeight / video.videoHeight) : Math.min(targetWidth / video.videoWidth, targetHeight / video.videoHeight)
      context.drawImage(video, (targetWidth - video.videoWidth * scale) / 2, (targetHeight - video.videoHeight * scale) / 2, video.videoWidth * scale, video.videoHeight * scale)
      const pixels = context.getImageData(0, 0, targetWidth, targetHeight).data; const offset = 14 + frame * targetWidth * targetHeight / 8
      for (let y = 0; y < targetHeight; y += 1) for (let x = 0; x < targetWidth; x += 1) { const pixel = (y * targetWidth + x) * 4; if (pixels[pixel] * 0.2126 + pixels[pixel + 1] * 0.7152 + pixels[pixel + 2] * 0.0722 >= 128) output[offset + (y >> 3) * targetWidth + x] |= 1 << (y & 7) }
      const now = performance.now()
      if (frame === frameCount - 1) {
        pendingFrame = frame + 1
        pendingTimestamp = now
        flushProgress()
      } else scheduleProgress(frame + 1, now)
      // Yield on every frame so the browser can paint the progress element and
      // handle cancellation/input while a long video is being converted.
      await new Promise<void>((resolve) => window.requestAnimationFrame(() => resolve()))
    }
    const name = `${(selectedFile.value.name.replace(/\.[^.]+$/, '') || 'animation').replace(/[^a-zA-Z0-9_.-]/g, '_')}-${targetWidth}x${targetHeight}-${fps.value}.oan`
    pending.value = { id: 'pending', name: name.replace(/\.oan$/, ''), width: targetWidth, height: targetHeight, fps: fps.value, frames: frameCount, duration_seconds: frameCount / fps.value, size: output.byteLength, data: output }
    status.value = t('previewReady').replace('{frames}', String(frameCount)).replace('{size}', bytes(output.byteLength))
  } catch (reason) {
    if (conversionCancelled.value) status.value = t('conversionCancelled')
    else error.value = reason instanceof Error ? reason.message : String(reason)
  } finally {
    if (progressTimer !== null) window.clearTimeout(progressTimer)
    URL.revokeObjectURL(objectURL); busy.value = false; converting.value = false; conversionCancelled.value = false; progress.value = 0; totalFrames.value = 0; convertedFrames.value = 0; etaSeconds.value = 0; conversionStartedAt.value = 0
  }
}

function cancelConversion() {
  if (converting.value) conversionCancelled.value = true
}

function formatEta(value: number) {
  const seconds = Math.max(0, Math.ceil(value || 0))
  if (seconds < 60) return `${seconds}s`
  return `${Math.floor(seconds / 60)}m ${String(seconds % 60).padStart(2, '0')}s`
}

async function refresh() {
  try {
    const response = await fetch(route('/'), { credentials: 'include' })
    if (!response.ok) throw new Error('asset list failed')
    assets.value = await response.json()
    window.dispatchEvent(new CustomEvent('onekvm-oled-assets-changed'))
  } catch (_) { error.value = t('assetLibraryError') }
}

async function loadAssetData(asset: Asset) {
  if (!asset.id || asset.data) return asset.data
  const response = await fetch(route(`/${encodeURIComponent(asset.id)}`), { credentials: 'include' })
  if (!response.ok) throw new Error(`asset preview failed (${response.status})`)
  const data = new Uint8Array(await response.arrayBuffer())
  return data
}

async function preview(asset: Asset) {
  if (!asset.id || previewing.value) return
  previewing.value = asset.id
  error.value = ''
  try {
    const data = await loadAssetData(asset)
    if (data) savedPreview.value = { ...asset, data }
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : t('assetLibraryError')
  } finally { previewing.value = null }
}

async function play(asset: Asset) {
  try {
    const response = await fetch(route(`/play?name=${encodeURIComponent(asset.id || '')}`), { method: 'POST', credentials: 'include' })
    if (!response.ok) { const detail = (await response.text()).trim(); throw new Error(`OLED playback failed (${response.status})${detail ? `: ${detail}` : ''}`) }
    status.value = t('playingAsset')
  } catch (reason) { error.value = reason instanceof Error ? reason.message : t('assetLibraryError') }
}

async function downloadAsset(asset: Asset) {
  if (!asset.id || downloading.value) return
  downloading.value = asset.id
  error.value = ''
  try {
    const data = await loadAssetData(asset)
    if (!data) throw new Error(t('assetLibraryError'))
    const content = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer
    const url = URL.createObjectURL(new Blob([content], { type: 'application/octet-stream' }))
    const link = document.createElement('a')
    link.href = url
    link.download = asset.id
    link.click()
    window.setTimeout(() => URL.revokeObjectURL(url), 0)
    status.value = t('assetDownloaded')
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : t('assetLibraryError')
  } finally { downloading.value = null }
}

async function assetMutationError(response: Response, fallback: string) {
  const detail = (await response.text()).trim()
  try {
    const code = (JSON.parse(detail) as { error?: string }).error
    if (code === 'asset_in_use') return t('assetInUse')
    if (code === 'asset_exists') return t('assetNameExists')
  } catch (_) { /* Non-JSON backend errors retain their diagnostic text. */ }
  return detail || fallback
}

async function removeAsset(asset: Asset) {
  if (!asset.id || deleting.value) return
  deleting.value = asset.id
  error.value = ''
  try {
    const response = await fetch(route(`/${encodeURIComponent(asset.id)}`), { method: 'DELETE', credentials: 'include' })
    if (!response.ok) {
      throw new Error(await assetMutationError(response, `asset delete failed (${response.status})`))
    }
    assets.value = assets.value.filter((candidate) => candidate.id !== asset.id)
    if (savedPreview.value?.id === asset.id) savedPreview.value = null
    window.dispatchEvent(new CustomEvent('onekvm-oled-assets-changed'))
    status.value = t('assetDeleted')
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : t('assetLibraryError')
  } finally { deleting.value = null }
}

function startRename(asset: Asset) {
  renameTarget.value = asset
  renameName.value = asset.name
  renameOpen.value = true
}

async function confirmRename() {
  const asset = renameTarget.value
  if (!asset?.id || renaming.value) return
  const base = renameName.value.trim().replace(/\.oan$/i, '')
  if (!base || base.length > 92 || !/^[a-zA-Z0-9_.-]+$/.test(base)) {
    error.value = t('invalidAssetName')
    return
  }
  const nextID = `${base}.oan`
  renaming.value = asset.id
  error.value = ''
  try {
    const response = await fetch(route(`/rename?name=${encodeURIComponent(asset.id)}&new_name=${encodeURIComponent(nextID)}`), { method: 'POST', credentials: 'include' })
    if (!response.ok) throw new Error(await assetMutationError(response, `asset rename failed (${response.status})`))
    assets.value = assets.value.map((candidate) => candidate.id === asset.id ? { ...candidate, id: nextID, name: base } : candidate)
    if (savedPreview.value?.id === asset.id) savedPreview.value = { ...savedPreview.value, id: nextID, name: base }
    window.dispatchEvent(new CustomEvent('onekvm-oled-assets-changed'))
    status.value = t('assetRenamed')
    renameOpen.value = false
    renameTarget.value = null
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : t('assetLibraryError')
  } finally { renaming.value = null }
}

async function uploadPending() {
  if (!pending.value?.data || busy.value) return
  busy.value = true; error.value = ''
  try {
    const response = await fetch(route(`/upload?name=${encodeURIComponent(`${pending.value.name}.oan`)}`), { method: 'POST', credentials: 'include', headers: { 'Content-Type': 'application/octet-stream' }, body: pending.value.data as unknown as BodyInit })
    if (!response.ok) { const detail = (await response.text()).trim(); throw new Error(`asset upload failed (${response.status})${detail ? `: ${detail}` : ''}`) }
    status.value = t('assetReady').replace('{frames}', String(pending.value.frames)).replace('{size}', bytes(pending.value.size)); pending.value = null; selectedFile.value = null; await refresh()
  } catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason) } finally { busy.value = false }
}

function resetPending() { pending.value = null; status.value = '' }
onMounted(refresh)
</script>

<template>
  <section class="media-vue-stack">
    <NAlert v-if="error" type="error" closable @close="error = ''">{{ error }}</NAlert>
    <NAlert v-if="status" type="success" closable @close="status = ''">{{ status }}</NAlert>
    <NCard class="media-vue-card" size="small">
      <NCollapse>
        <NCollapseItem name="upload" :title="t('uploadCardTitle')">
          <p class="media-vue-hint">{{ t('mediaHint') }}</p>
          <div class="media-vue-file-transfer">
            <div><strong>{{ t('uploadAssetFile') }}</strong><span>{{ t('uploadAssetFileHint') }}</span></div>
            <input ref="assetFileInput" class="hidden" type="file" accept=".oan,application/octet-stream" @change="handleAssetFileChange">
            <NButton secondary :disabled="busy || Boolean(pending)" @click="chooseAssetFile"><template #icon><Upload :size="16" aria-hidden="true" /></template>{{ t('chooseAssetFile') }}</NButton>
          </div>
          <div class="media-vue-converter">
            <input ref="fileInput" class="hidden" type="file" accept="video/mp4,video/webm,video/quicktime" @change="handleFileChange">
            <div class="media-vue-dropzone" :class="{ disabled: busy || Boolean(pending) }" tabindex="0" role="button" @click="chooseFile" @keydown="handleDropzoneKey" @drop.prevent="handleDrop" @dragover.prevent>
              <div class="media-vue-drop-icon">⇧</div><NButton :disabled="busy || Boolean(pending)" @click.stop="chooseFile">{{ t('chooseVideo') }}</NButton><span>{{ t('dropVideo') }}</span>
            </div>
            <span class="media-vue-file-name">{{ fileName }}</span>
            <label class="media-vue-resolution-field"><span>{{ t('mediaResolution') }}</span><NSelect v-model:value="resolution" :options="resolutionOptions" :disabled="busy || Boolean(pending)" /></label>
            <div v-if="resolution === 'custom'" class="media-vue-custom-resolution">
              <label><span>{{ t('mediaCustomWidth') }}</span><NInputNumber v-model:value="width" :min="8" :max="256" :step="1" :disabled="busy || Boolean(pending)" /></label>
              <NButton :disabled="busy || Boolean(pending)" :aria-label="t(ratioLocked ? 'ratioLocked' : 'ratioUnlocked')" @click="ratioLocked = !ratioLocked"><template #icon><LockKeyhole v-if="ratioLocked" :size="16" aria-hidden="true" /><UnlockKeyhole v-else :size="16" aria-hidden="true" /></template></NButton>
              <label><span>{{ t('mediaCustomHeight') }}</span><NInputNumber v-model:value="height" :min="8" :max="256" :step="8" :disabled="busy || Boolean(pending)" /></label>
            </div>
            <details class="media-vue-advanced"><summary>{{ t('mediaAdvanced') }}</summary><div class="media-vue-advanced-fields"><label><span>{{ t('mediaFps') }}</span><NSelect v-model:value="fps" :options="fpsOptions" :disabled="busy || Boolean(pending)" /></label><label><span>{{ t('mediaFit') }}</span><NSelect v-model:value="fit" :options="fitOptions" :disabled="busy || Boolean(pending)" /></label></div></details>
            <div class="media-vue-convert-actions"><NButton block :loading="converting" :disabled="!canConvert" @click="convert">{{ t('convertVideo') }}</NButton><NButton v-if="converting" secondary :disabled="conversionCancelled" @click="cancelConversion">{{ t('cancelConversion') }}</NButton></div>
            <div v-if="converting && totalFrames > 0" class="media-vue-progress">
              <NProgress type="line" :percentage="Math.round(progress * 100)" :processing="true" />
              <div class="media-vue-progress-meta"><span>{{ t('convertingPercent').replace('{percent}', String(Math.round(progress * 100))) }}</span><span>{{ t('convertingFrames').replace('{frames}', String(convertedFrames)).replace('{total}', String(totalFrames)) }}</span><span>{{ t('convertingEta').replace('{eta}', formatEta(etaSeconds)) }}</span></div>
            </div>
          </div>
          <div v-if="pending" class="media-vue-pending"><OledAssetPreview :asset="pending" large :display-width="displayWidth" :display-height="displayHeight" /><div class="media-vue-actions"><NButton :loading="busy" @click="uploadPending">{{ t('uploadAsset') }}</NButton><NButton :disabled="busy" @click="resetPending">{{ t('reuploadAsset') }}</NButton></div></div>
        </NCollapseItem>
      </NCollapse>
    </NCard>
    <NCard class="media-vue-card" size="small" :title="t('libraryTitle')">
      <template #header-extra><NTag>{{ assets.length }} {{ t('savedAsset') }}</NTag></template>
      <div class="media-vue-library-toolbar"><NInput v-model:value="search" clearable :placeholder="t('searchAssets')" /><NButton quaternary @click="refresh">{{ t('refreshAssets') }}</NButton></div>
      <div v-if="!filteredAssets.length" class="media-vue-empty">{{ t('noAssets') }}</div>
      <div v-else class="media-vue-list">
        <article v-for="asset in filteredAssets" :key="asset.id" class="media-vue-asset-row">
          <OledAssetPreview :asset="asset" bare :display-width="displayWidth" :display-height="displayHeight" />
          <div class="media-vue-asset-info">
            <strong>{{ asset.name }}</strong>
            <span>{{ asset.width }}×{{ asset.height }} · {{ asset.fps }} FPS · {{ duration(asset.duration_seconds ?? asset.frames / asset.fps) }} · {{ bytes(asset.size) }}</span>
          </div>
          <div class="media-vue-asset-actions">
            <NButton size="small" :loading="previewing === asset.id" @click="preview(asset)">{{ t('previewAsset') }}</NButton>
            <NButton size="small" secondary @click="play(asset)">{{ t('playAsset') }}</NButton>
            <NButton size="small" :loading="downloading === asset.id" @click="downloadAsset(asset)"><template #icon><Download :size="15" aria-hidden="true" /></template>{{ t('downloadAsset') }}</NButton>
            <NButton size="small" @click="startRename(asset)">{{ t('renameAsset') }}</NButton>
            <NPopconfirm :negative-text="t('cancel')" :positive-text="t('confirmDelete')" @positive-click="removeAsset(asset)">
              <template #trigger>
                <NButton size="small" type="error" secondary :loading="deleting === asset.id">{{ t('deleteAsset') }}</NButton>
              </template>
              {{ t('deleteAssetConfirm') }}
            </NPopconfirm>
          </div>
        </article>
      </div>
      <div v-if="savedPreview" class="media-vue-saved-preview">
        <div class="media-vue-preview-header"><strong>{{ savedPreview.name }}</strong><NButton size="small" @click="savedPreview = null">{{ t('closePreview') }}</NButton></div>
        <OledAssetPreview :asset="savedPreview" large :display-width="displayWidth" :display-height="displayHeight" />
      </div>
    </NCard>
    <NModal v-model:show="renameOpen" preset="card" class="media-vue-rename-modal" :title="t('renameAsset')">
      <label class="media-vue-rename-field"><span>{{ t('assetName') }}</span><NInput v-model:value="renameName" :maxlength="92" /></label>
      <div class="media-vue-actions"><NButton :disabled="Boolean(renaming)" @click="renameOpen = false">{{ t('cancel') }}</NButton><NButton :loading="Boolean(renaming)" @click="confirmRename">{{ t('confirmRename') }}</NButton></div>
    </NModal>
  </section>
</template>
