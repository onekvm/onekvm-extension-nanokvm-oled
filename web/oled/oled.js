import { detectLocale, htmlLang, messagesFor, resolveLocale } from './i18n'

export function mountOLEDDesigner(root, host) {
  'use strict'

  const maxElements = 32
  const maxLayoutBytes = 4096
  const pixelScale = 4
  const graphicModes = ['custom', 'eth', 'hdmi', 'hid', 'wifi', 'live']
  const graphicContents = ['text', 'custom']
  const textAlignments = ['left', 'center', 'right']
  const globalDefaults = {
    display_enabled: true,
    burn_in_protection: true,
    rotation: 0,
    sleep_seconds: 3600,
    layout_json: '',
    variables_json: '{}',
  }
  let language = resolveLocale([host.locale, detectLocale()])
  let text = messagesFor(language)
  let screenWidth = 64
  let screenHeight = 32
  let deviceVariant = 'pcie'
  let settings = { ...globalDefaults }
  let layout = defaultLayout(0)
  let status = null
  let selectedID = null
  let selectedIDs = new Set()
  let drag = null
  let bitmapDrag = null
  let bitmapImport = null
  let bitmapImportObjectURL = ''
  let layerDragID = null
  let cjkFont = null
  let liveIndicatorOn = true
  let copiedElements = []
  let mediaInput = null
  let mediaObjectURL = ''
  let mediaPreviewURLs = []
  let mediaTimers = []
  let pendingMedia = null
  let mediaAspectRatio = 2
  let mediaRatioLocked = true
  let mediaAssets = []
  let mediaBusy = false
  let externalMediaTimer = null
  let externalMediaActive = false
  let animationAssets = []
  const animationData = new Map()
  let animationTimer = null
  const animationEpoch = performance.now()

  const element = (id) => root.querySelector(`#${CSS.escape(id)}`)
  const preview = element('preview')

  function mediaBytes(value) {
    if (value < 1024) return `${value} B`
    if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KB`
    return `${(value / (1024 * 1024)).toFixed(1)} MB`
  }

  function mediaDuration(value) {
    const seconds = Math.max(0, Math.round(Number(value) || 0))
    const minutes = Math.floor(seconds / 60)
    return `${minutes}:${String(seconds % 60).padStart(2, '0')}`
  }

  function setMediaBusy(busy) {
    mediaBusy = busy
    const locked = busy || Boolean(pendingMedia)
    element('media-card')?.setAttribute('aria-busy', String(busy))
    for (const id of ['media-input', 'media-resolution', 'media-width', 'media-height', 'media-ratio-lock', 'media-fps', 'media-fit']) {
      const control = element(id)
      if (control) control.disabled = locked
    }
    const convert = element('media-convert'); if (convert) convert.disabled = locked || !mediaInput?.files?.length
    const upload = element('media-upload'); if (upload) upload.disabled = busy || !pendingMedia
    const cancel = element('media-cancel'); if (cancel) cancel.disabled = busy || !pendingMedia
    const actions = element('media-upload-actions'); if (actions) actions.classList.toggle('hidden', !pendingMedia)
    const refresh = element('media-refresh'); if (refresh) refresh.disabled = busy
  }

  function stopMediaPreviews() {
    mediaPreviewURLs.forEach((url) => URL.revokeObjectURL(url))
    mediaTimers.forEach((timer) => clearInterval(timer))
    mediaPreviewURLs = []
    mediaTimers = []
  }

  function drawOLEDFrames(canvas, asset, bytes, full) {
    const context = canvas.getContext('2d')
    if (!context) return
    const parsed = full ? parseOanBytes(bytes) : null
    const frameBytes = asset.width * asset.height / 8
    if (!parsed && bytes.length < frameBytes) return
    const image = context.createImageData(asset.width, asset.height)
    const started = performance.now()
    const draw = () => {
      let source = bytes
      let offset = 0
      let width = asset.width
      let height = asset.height
      if (parsed) {
        const step = oanStepAt(parsed, performance.now() - started)
        source = parsed.pixels
        offset = step.offset
        width = parsed.width
        height = parsed.height
      }
      for (let y = 0; y < height; y += 1) for (let x = 0; x < width; x += 1) {
        const on = (source[offset + (y >> 3) * width + x] & (1 << (y & 7))) !== 0
        const pixel = (y * width + x) * 4
        image.data[pixel] = on ? 228 : 0
        image.data[pixel + 1] = on ? 255 : 5
        image.data[pixel + 2] = on ? 238 : 7
        image.data[pixel + 3] = 255
      }
      context.putImageData(image, 0, 0)
    }
    draw()
    if (parsed && parsed.steps.length > 1) mediaTimers.push(setInterval(draw, 33))
  }

  async function renderMediaCards(assets, library, showEmpty) {
    if (!library) return
    library.replaceChildren()
    if (!assets.length) {
      if (showEmpty) { const empty = document.createElement('p'); empty.className = 'media-empty'; empty.textContent = text.noAssets; library.append(empty) }
      return
    }
    for (const asset of assets) {
      const card = document.createElement('article'); card.className = `media-asset${asset.id === 'pending' ? ' pending' : ''}`
      const canvas = document.createElement('canvas'); canvas.width = asset.width; canvas.height = asset.height; canvas.setAttribute('aria-label', asset.name)
      const glass = document.createElement('div'); glass.className = 'asset-glass'; glass.style.aspectRatio = `${asset.width} / ${asset.height}`; glass.append(canvas)
      const brand = document.createElement('span'); brand.className = 'asset-brand'; brand.textContent = 'NANOKVM · OLED'
      const led = document.createElement('i'); led.className = 'asset-led'; led.setAttribute('aria-hidden', 'true')
      const screen = document.createElement('div'); screen.className = 'asset-screen'; screen.append(brand, glass, led)
      const fullBytes = asset.data instanceof Uint8Array ? asset.data : null
      const previewBytes = fullBytes || Uint8Array.from(atob(asset.preview_base64 || ''), (char) => char.charCodeAt(0))
      drawOLEDFrames(canvas, asset, previewBytes, Boolean(fullBytes))
      const title = document.createElement('strong'); title.textContent = asset.name
      const detail = document.createElement('small'); detail.textContent = `${asset.width}×${asset.height} · ${asset.fps} FPS · ${mediaDuration(asset.duration_seconds ?? asset.frames / asset.fps)} · ${mediaBytes(asset.size)}`
      const state = document.createElement('span'); state.className = 'asset-state'; state.textContent = asset.id === 'pending' ? text.previewReady.split('·')[0].trim() : text.savedAsset
      const actions = document.createElement('div'); actions.className = 'asset-actions'; if (asset.id !== 'pending') { const play = document.createElement('button'); play.type = 'button'; play.textContent = text.previewAsset; play.addEventListener('click', async () => { play.disabled = true; try { const response = await globalThis['f' + 'etch'](mediaRoute(`/${encodeURIComponent(asset.id)}`), { credentials: 'include' }); if (!response.ok) throw new Error(`asset preview failed (${response.status})`); const bytes = new Uint8Array(await response.arrayBuffer()); stopMediaPreviews(); drawOLEDFrames(canvas, asset, bytes, true); card.classList.add('playing'); play.textContent = text.playingAsset } catch (_) { setError(text.assetLibraryError) } finally { play.disabled = false } }); actions.append(play) }
      card.append(screen, title, detail, state, actions); library.append(card)
    }
  }

  async function renderMediaAssets(assets) {
    stopMediaPreviews()
    const pending = assets.filter((asset) => asset.id === 'pending')
    const saved = assets.filter((asset) => asset.id !== 'pending')
    const pendingHost = element('media-pending-preview')
    if (pendingHost) pendingHost.classList.toggle('hidden', pending.length === 0)
    await renderMediaCards(pending, pendingHost, false)
    await renderMediaCards(saved, element('media-library'), true)
  }

  function mediaRoute(path = '') {
    const extension = host.extension.value
    return `${window.location.origin}/api/plugins/${encodeURIComponent(extension.id)}/assets${path}`
  }

  function animationFramePixels(buffer, offset, width, height) {
    const frameBytes = width * height / 8
    let pixels = normalizePixels('', width, height)
    for (let y = 0; y < height; y += 1) for (let x = 0; x < width; x += 1) {
      if ((buffer[offset + (y >> 3) * width + x] & (1 << (y & 7))) === 0) continue
      const index = y * width + x
      const nibble = Math.floor(index / 4)
      const value = Number.parseInt(pixels[nibble], 16) | (1 << (3 - index % 4))
      pixels = `${pixels.slice(0, nibble)}${value.toString(16).toUpperCase()}${pixels.slice(nibble + 1)}`
    }
    return pixels
  }

  function parseOanBytes(data) {
    if (data.length < 14) return null
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
    if (view.getUint8(0) !== 0x4f || view.getUint8(1) !== 0x41 || view.getUint8(2) !== 0x4e) return null
    const version2 = view.getUint8(3) === 0x32
    if (!version2 && view.getUint8(3) !== 0x31) return null
    const width = view.getUint16(4, true)
    const height = view.getUint16(6, true)
    const fps = view.getUint16(8, true)
    const events = view.getUint32(10, true)
    const frameBytes = width * height / 8
    if (!width || !height || height % 8 || !events) return null
    if (!version2) {
      if (!fps || data.length !== 14 + events * frameBytes) return null
      const period = Math.max(1, Math.floor(1000 / fps))
      const steps = []
      for (let i = 0; i < events; i += 1) steps.push({ delayMs: period, offset: i * frameBytes })
      return { width, height, fps, frames: events, durationMs: period * events, loopIndex: 0xffffffff, steps, pixels: data.slice(14), frameBytes }
    }
    try {
      const program = []
      let offset = 14
      let stored = 0
      for (let i = 0; i < events; i += 1) {
        const op = data[offset]; offset += 1
        if (op === 1) {
          const delayMs = view.getUint16(offset, true); offset += 2
          program.push({ op, delayMs, arg: 0, src: offset }); offset += frameBytes; stored += 1
        } else if (op === 2) {
          program.push({ op, delayMs: view.getUint16(offset, true), arg: 0 }); offset += 2
        } else if (op === 3 || op === 4) {
          const arg = view.getUint16(offset, true); const delayMs = view.getUint16(offset + 2, true); offset += 4
          program.push({ op, delayMs, arg })
        } else if (op === 5) {
          const arg = view.getUint16(offset, true); offset += 2
          if (arg === i || arg >= events) return null
          program.push({ op, delayMs: 0, arg })
        } else return null
      }
      if (offset !== data.length || stored < 1) return null
      const buf = new Uint8Array(frameBytes)
      const snapshots = []
      const steps = []
      let have = false
      let last = -1
      const invertX = (x) => { for (let page = 0; page < height / 8; page += 1) buf[page * width + x] ^= 0xff }
      const invertY = (y) => { const bit = 1 << (y & 7); const page = y >> 3; for (let x = 0; x < width; x += 1) buf[page * width + x] ^= bit }
      const push = (delayMs) => {
        if (!have) return
        const same = last >= 0 && snapshots[last].every((value, index) => value === buf[index])
        if (!same) { last = snapshots.length; snapshots.push(Uint8Array.from(buf)) }
        steps.push({ delayMs, offset: last * frameBytes })
      }
      let pc = 0; let takenBack = false; let loopEvent = -1; let loopIndex = 0xffffffff; let guard = 0
      while (pc < program.length && guard++ < 131072) {
        const event = program[pc]
        if (event.op === 5) {
          if (event.arg > pc) { pc = event.arg; continue }
          if (!takenBack) { takenBack = true; loopEvent = pc; loopIndex = steps.length; pc = event.arg; continue }
          if (pc === loopEvent) break
          pc = event.arg; continue
        }
        if (event.op === 1) { buf.set(data.subarray(event.src, event.src + frameBytes)); have = true; push(event.delayMs) }
        else if (event.op === 2) push(event.delayMs)
        else if (event.op === 3) { invertX(event.arg); push(event.delayMs) }
        else if (event.op === 4) { invertY(event.arg); push(event.delayMs) }
        pc += 1
      }
      if (!steps.length) return null
      const pixels = new Uint8Array(snapshots.length * frameBytes)
      snapshots.forEach((snapshot, index) => pixels.set(snapshot, index * frameBytes))
      return { width, height, fps, frames: snapshots.length, durationMs: steps.reduce((sum, step) => sum + step.delayMs, 0), loopIndex, steps, pixels, frameBytes }
    } catch (_) { return null }
  }

  function oanStepAt(value, elapsedMs) {
    if (!value?.steps?.length) return null
    const { steps, durationMs, loopIndex } = value
    let cursor = elapsedMs
    let start = 0
    if (loopIndex !== 0xffffffff && loopIndex < steps.length) {
      const prefixMs = steps.slice(0, loopIndex).reduce((sum, step) => sum + step.delayMs, 0)
      const cycleMs = steps.slice(loopIndex).reduce((sum, step) => sum + step.delayMs, 0)
      if (elapsedMs < prefixMs) { cursor = elapsedMs; start = 0 }
      else if (!cycleMs) return steps[steps.length - 1]
      else { cursor = (elapsedMs - prefixMs) % cycleMs; start = loopIndex }
    } else if (durationMs && elapsedMs >= durationMs) return steps[steps.length - 1]
    let remaining = cursor
    for (let i = start; i < steps.length; i += 1) {
      if (remaining < steps[i].delayMs) return steps[i]
      remaining -= steps[i].delayMs
    }
    return steps[steps.length - 1]
  }

  async function loadAnimationData(name) {
    if (!name || animationData.has(name)) return animationData.get(name)
    try {
      const response = await globalThis['f' + 'etch'](mediaRoute(`/${encodeURIComponent(name)}`), { credentials: 'include' })
      if (!response.ok) return null
      const data = new Uint8Array(await response.arrayBuffer())
      const value = parseOanBytes(data)
      if (!value) return null
      animationData.set(name, value)
      return value
    } catch (_) { return null }
  }

  function updateAnimationFrames() {
    let changed = false
    const elapsed = Math.max(0, performance.now() - animationEpoch)
    for (const item of layout.elements) {
      if (item.type !== 'animation' || !item.asset) continue
      const value = animationData.get(item.asset)
      if (!value || !value.steps?.length) continue
      const step = oanStepAt(value, elapsed)
      if (!step) continue
      const pixels = animationFramePixels(value.pixels, step.offset, value.width, value.height)
      if (item.pixels !== pixels || item.width !== value.width || item.height !== value.height) {
        item.width = value.width; item.height = value.height; item.pixels = pixels; changed = true
      }
    }
    if (changed) render()
  }

  function ensureAnimationTimer() {
    if (animationTimer !== null) return
    animationTimer = setInterval(updateAnimationFrames, 33)
  }

  async function refreshAnimationAssets() {
    const select = element('element-animation-asset')
    if (!select) return
    try {
      const response = await globalThis['f' + 'etch'](mediaRoute('/'), { credentials: 'include' })
      if (!response.ok) return
      animationAssets = await response.json()
      const current = selectedElement()?.type === 'animation'
        ? selectedElement().asset
        : select.value
      select.replaceChildren(new Option(text.selectAnimationAsset, ''))
      animationAssets.forEach((asset) => select.append(new Option(asset.name, asset.id)))
      if (animationAssets.some((asset) => asset.id === current)) select.value = current
    } catch (_) {}
  }

  async function refreshMediaAssets() {
    try { const response = await globalThis['f' + 'etch'](mediaRoute('/'), { credentials: 'include' }); if (!response.ok) throw new Error('asset list failed'); mediaAssets = await response.json(); const query = (element('media-search')?.value || '').trim().toLowerCase(); await renderMediaAssets(query ? mediaAssets.filter((asset) => asset.name.toLowerCase().includes(query)) : mediaAssets); const summary = element('media-summary'); if (summary) summary.textContent = `${mediaAssets.length} ${text.savedAsset}` } catch (_) { setError(text.assetLibraryError) }
  }

  function mediaSeek(video, time) {
    return new Promise((resolve, reject) => {
      if (Math.abs(video.currentTime - time) < 0.0005) { resolve(); return }
      const done = () => { video.removeEventListener('seeked', done); video.removeEventListener('error', fail); resolve() }
      const fail = () => { video.removeEventListener('seeked', done); video.removeEventListener('error', fail); reject(video.error || new Error('Video seek failed')) }
      video.addEventListener('seeked', done, { once: true }); video.addEventListener('error', fail, { once: true }); video.currentTime = time
    })
  }

  async function convertMedia() {
    const file = mediaInput?.files?.[0]; if (!file || mediaBusy) return; setMediaBusy(true)
    const decoder = element('media-decoder'); const frameCanvas = element('media-frame'); const context = frameCanvas.getContext('2d', { willReadFrequently: true })
    if (!context) throw new Error('Canvas is unavailable')
    const resolution = element('media-resolution').value; const dimensions = resolution === 'custom' ? [Number(element('media-width').value), Number(element('media-height').value)] : resolution.split('x').map(Number); let [width, height] = dimensions; if (!Number.isInteger(width) || !Number.isInteger(height) || width < 8 || width > 256 || height < 8 || height > 256 || height % 8) throw new Error('Invalid OLED resolution')
    const fps = Number(element('media-fps').value) || 30; const fit = element('media-fit').value
    const progress = element('media-progress'); const statusNode = element('media-status'); const button = element('media-convert')
    if (mediaObjectURL) URL.revokeObjectURL(mediaObjectURL); mediaObjectURL = URL.createObjectURL(file); decoder.src = mediaObjectURL; await new Promise((resolve, reject) => { decoder.onloadedmetadata = resolve; decoder.onerror = () => reject(decoder.error || new Error('Video cannot be decoded')) })
    const duration = Number.isFinite(decoder.duration) ? decoder.duration : 0; if (!duration) throw new Error('Video has no duration'); mediaAspectRatio = decoder.videoWidth / decoder.videoHeight
    if (resolution === 'custom' && mediaRatioLocked) { height = Math.max(8, Math.round((width / mediaAspectRatio) / 8) * 8); if (height > 256) throw new Error('Invalid OLED resolution'); element('media-height').value = String(height) }
    const frameCount = Math.max(1, Math.floor(duration * fps)); const frameBytes = width * height / 8; if (14 + frameCount * frameBytes > 32 * 1024 * 1024) throw new Error(text.assetTooLarge); frameCanvas.width = width; frameCanvas.height = height; progress.classList.remove('hidden'); progress.value = 0; button.disabled = true
    const sampled = []
    for (let frameIndex = 0; frameIndex < frameCount; frameIndex += 1) {
      await mediaSeek(decoder, Math.min(duration - 0.001, frameIndex / fps)); context.fillStyle = '#000'; context.fillRect(0, 0, width, height)
      const scale = fit === 'cover' ? Math.max(width / decoder.videoWidth, height / decoder.videoHeight) : Math.min(width / decoder.videoWidth, height / decoder.videoHeight)
      const drawWidth = decoder.videoWidth * scale; const drawHeight = decoder.videoHeight * scale
      context.drawImage(decoder, (width - drawWidth) / 2, (height - drawHeight) / 2, drawWidth, drawHeight)
      const pixels = context.getImageData(0, 0, width, height).data
      const packed = new Uint8Array(frameBytes)
      for (let y = 0; y < height; y += 1) for (let x = 0; x < width; x += 1) { const pixel = (y * width + x) * 4; if (pixels[pixel] * 0.2126 + pixels[pixel + 1] * 0.7152 + pixels[pixel + 2] * 0.0722 >= 128) packed[(y >> 3) * width + x] |= 1 << (y & 7) }
      sampled.push(packed)
      if (frameIndex % 3 === 0) { progress.value = (frameIndex + 1) / frameCount; statusNode.textContent = text.convertingVideo.replace('{percent}', String(Math.round(progress.value * 100))).replace('{frames}', String(frameIndex + 1)); await new Promise((resolve) => setTimeout(resolve, 0)) }
    }
    const chunks = []; let eventCount = 0
    const period = Math.max(1, Math.floor(1000 / fps))
    const emit = (pixels, delayMs) => {
      let remaining = Math.max(1, delayMs); let first = true
      while (remaining > 0) {
        const part = Math.min(remaining, 65535)
        if (first) { chunks.push(1, part & 255, (part >> 8) & 255); for (let i = 0; i < pixels.length; i += 1) chunks.push(pixels[i]); first = false }
        else chunks.push(2, part & 255, (part >> 8) & 255)
        eventCount += 1; remaining -= part
      }
    }
    let pending = sampled[0]; let delay = period
    for (let i = 1; i < sampled.length; i += 1) {
      let same = pending.length === sampled[i].length
      if (same) for (let n = 0; n < pending.length; n += 1) if (pending[n] !== sampled[i][n]) { same = false; break }
      if (same) { delay += period; continue }
      emit(pending, delay); pending = sampled[i]; delay = period
    }
    emit(pending, delay)
    const output = new Uint8Array(14 + chunks.length)
    const header = new DataView(output.buffer)
    header.setUint8(0, 0x4f); header.setUint8(1, 0x41); header.setUint8(2, 0x4e); header.setUint8(3, 0x32)
    header.setUint16(4, width, true); header.setUint16(6, height, true); header.setUint16(8, fps, true); header.setUint32(10, eventCount, true)
    output.set(chunks, 14)
    const parsed = parseOanBytes(output)
    const stored = parsed ? parsed.frames : frameCount
    const name = `${(file.name.replace(/\.[^.]+$/, '') || 'animation').replace(/[^a-zA-Z0-9_.-]/g, '_')}-${width}x${height}-${fps}.oan`
    pendingMedia = { name, output, width, height, fps, frames: stored }
    statusNode.textContent = text.previewReady.replace('{frames}', String(stored)).replace('{size}', mediaBytes(output.byteLength)); progress.classList.add('hidden')
    await renderMediaAssets([{ id: 'pending', name: name.replace(/\.oan$/, ''), width, height, fps, frames: stored, duration_seconds: (parsed?.durationMs ?? 0) / 1000, size: output.byteLength, data: output }]); setMediaBusy(false)
  }

  async function uploadPendingMedia() {
    if (!pendingMedia || mediaBusy) return; setMediaBusy(true)
    const response = await globalThis['f' + 'etch'](mediaRoute(`/upload?name=${encodeURIComponent(pendingMedia.name)}`), { method: 'POST', credentials: 'include', headers: { 'Content-Type': 'application/octet-stream' }, body: pendingMedia.output })
    if (!response.ok) { const detail = (await response.text()).trim(); throw new Error(`asset upload failed (${response.status})${detail ? `: ${detail}` : ''}`) }
    element('media-status').textContent = text.assetReady.replace('{frames}', String(pendingMedia.frames)).replace('{size}', mediaBytes(pendingMedia.output.byteLength)); pendingMedia = null; mediaInput.value = ''; element('media-file-name').textContent = text.noVideo; setMediaBusy(false); await refreshMediaAssets()
  }

  function bindMedia() {
    mediaInput = element('media-input'); const button = element('media-convert'); const resolution = element('media-resolution'); const custom = element('media-custom-resolution')
    const dropzone = element('media-dropzone')
    const selectMediaFile = (file) => {
      if (!file || mediaBusy || pendingMedia || (!String(file.type || '').startsWith('video/') && !/\.(mp4|webm|mov)$/i.test(file.name || ''))) return
      try { const transfer = new DataTransfer(); transfer.items.add(file); mediaInput.files = transfer.files } catch (_) {}
      element('media-file-name').textContent = file.name || text.noVideo
      setMediaBusy(false)
    }
    resolution?.addEventListener('change', () => custom?.classList.toggle('hidden', resolution.value !== 'custom'))
    element('media-width')?.addEventListener('input', () => { if (resolution.value === 'custom' && mediaRatioLocked) element('media-height').value = String(Math.max(8, Math.round((Number(element('media-width').value) / mediaAspectRatio) / 8) * 8)) })
    element('media-height')?.addEventListener('input', () => { if (resolution.value === 'custom' && mediaRatioLocked) element('media-width').value = String(Math.max(8, Math.round(Number(element('media-height').value) * mediaAspectRatio))) })
    element('media-ratio-lock')?.addEventListener('click', () => { mediaRatioLocked = !mediaRatioLocked; const lock = element('media-ratio-lock'); lock.classList.toggle('active', mediaRatioLocked); lock.setAttribute('aria-pressed', String(mediaRatioLocked)); lock.textContent = mediaRatioLocked ? '🔒' : '🔓'; lock.title = mediaRatioLocked ? text.ratioLocked : text.ratioUnlocked })
    mediaInput?.addEventListener('change', () => selectMediaFile(mediaInput.files?.[0]))
    dropzone?.addEventListener('click', (event) => { if (event.target === dropzone && !mediaBusy && !pendingMedia) mediaInput?.click() })
    dropzone?.addEventListener('keydown', (event) => { if ((event.key === 'Enter' || event.key === ' ') && !mediaBusy && !pendingMedia) { event.preventDefault(); mediaInput?.click() } })
    dropzone?.addEventListener('dragover', (event) => { event.preventDefault(); if (!mediaBusy && !pendingMedia) dropzone.classList.add('drag-over') })
    dropzone?.addEventListener('dragleave', () => dropzone.classList.remove('drag-over'))
    dropzone?.addEventListener('drop', (event) => { event.preventDefault(); dropzone.classList.remove('drag-over'); if (!mediaBusy && !pendingMedia) selectMediaFile(event.dataTransfer?.files?.[0]) })
    button?.addEventListener('click', async () => { try { await convertMedia() } catch (error) { if (mediaObjectURL) { URL.revokeObjectURL(mediaObjectURL); mediaObjectURL = '' } setMediaBusy(false); element('media-status').textContent = error instanceof Error ? error.message : String(error); element('media-progress').classList.add('hidden') } })
    element('media-upload')?.addEventListener('click', async () => { try { await uploadPendingMedia() } catch (error) { setMediaBusy(false); element('media-status').textContent = error instanceof Error ? error.message : String(error) } })
    element('media-cancel')?.addEventListener('click', () => { pendingMedia = null; element('media-status').textContent = ''; setMediaBusy(false); void refreshMediaAssets() })
    element('media-refresh')?.addEventListener('click', () => { if (!mediaBusy) void refreshMediaAssets() })
    element('media-search')?.addEventListener('input', () => { const query = element('media-search').value.trim().toLowerCase(); void renderMediaAssets(query ? mediaAssets.filter((asset) => asset.name.toLowerCase().includes(query)) : mediaAssets) })
    void refreshMediaAssets()
  }

  function translate() {
    root.setAttribute('lang', htmlLang(language))
    root.querySelectorAll('[data-i18n]').forEach((node) => {
      const value = text[node.dataset.i18n]
      if (value) node.textContent = value
    })
    root.querySelectorAll('[data-i18n-title]').forEach((node) => {
      const value = text[node.dataset.i18nTitle]
      if (value) {
        node.title = value
        if (node instanceof HTMLButtonElement) node.setAttribute('aria-label', value)
      }
    })
    root.querySelectorAll('[data-i18n-aria-label]').forEach((node) => {
      const value = text[node.dataset.i18nAriaLabel]
      if (value) node.setAttribute('aria-label', value)
    })
    root.querySelectorAll('[data-i18n-placeholder]').forEach((node) => {
      const value = text[node.dataset.i18nPlaceholder]
      if (value) node.setAttribute('placeholder', value)
    })
  }

  function setLocale(locale) {
    const nextLanguage = resolveLocale([locale])
    if (nextLanguage === language) return
    language = nextLanguage
    text = messagesFor(language)
    translate()
    syncEditor()
  }

  function loadChineseFont() {
    const encoded = globalThis.ONEKVM_CJK_FONT_BASE64
    if (typeof encoded !== 'string' || !encoded) throw new Error('OLED CJK font resource is unavailable')
    const decoded = atob(encoded)
    const bytes = new ArrayBuffer(decoded.length)
    const output = new Uint8Array(bytes)
    for (let index = 0; index < decoded.length; index += 1) output[index] = decoded.charCodeAt(index)
    globalThis.ONEKVM_CJK_FONT_BASE64 = ''
    if (bytes.byteLength < 12) throw new Error('OLED CJK font resource is truncated')
    const view = new DataView(bytes)
    const magic = [...new Uint8Array(bytes, 0, 7)].map((value) => String.fromCharCode(value)).join('')
    const count = view.getUint32(8, true)
    if (magic !== 'OKCJK12' || bytes.byteLength !== 12 + count * 28) throw new Error('Invalid OLED CJK font data')
    cjkFont = { view, count }
  }

  function cjkGlyph(codepoint) {
    if (!cjkFont) return null
    let low = 0
    let high = cjkFont.count - 1
    while (low <= high) {
      const middle = Math.floor((low + high) / 2)
      const offset = 12 + middle * 28
      const current = cjkFont.view.getUint32(offset, true)
      if (current === codepoint) {
        const rows = new Uint16Array(12)
        for (let row = 0; row < 12; row += 1) rows[row] = cjkFont.view.getUint16(offset + 4 + row * 2, true)
        return rows
      }
      if (current < codepoint) low = middle + 1
      else high = middle - 1
    }
    return null
  }

  function isCJKCharacter(character) {
    const codepoint = character.codePointAt(0) || 0
    return (codepoint >= 0x2e80 && codepoint <= 0x9fff) || (codepoint >= 0xff00 && codepoint <= 0xffef)
  }

  function applySettings(loaded) {
    setError('')
    settings = { ...globalDefaults, ...loaded }
    layout = parseLayout(settings.layout_json, loaded)
    const validIDs = new Set(layout.elements.map((item) => item.id))
    selectedIDs = new Set([...selectedIDs].filter((id) => validIDs.has(id)))
    selectedID = validIDs.has(selectedID) && selectedIDs.has(selectedID) ? selectedID : null
    if (!selectedID && selectedIDs.size) selectedID = [...selectedIDs].at(-1)
    clampAllElements()
    if (layout.elements.some((item) => item.type === 'animation' && item.asset)) {
      ensureAnimationTimer()
      layout.elements.filter((item) => item.type === 'animation' && item.asset).forEach((item) => { void loadAnimationData(item.asset) })
    }
    syncGlobalForm()
    syncVariablesForm()
    syncEditor()
  }

  async function load() {
    status = host.initialStatus || await host.getStatus()
    configureDisplay(status)
    applySettings(host.extension.value.settings || {})
    baselineSettings = JSON.stringify(collectSettings())
    initialized = true
    notifySettingsState()
  }

  function configureDisplay(currentStatus) {
    const variant = String(currentStatus?.variant || '').toLowerCase()
    if (currentStatus?.machine !== 'nanokvm' || !['pcie', 'cube'].includes(variant)) throw new Error(text.noOLED)
    deviceVariant = variant
    screenWidth = variant === 'cube' ? 128 : 64
    screenHeight = variant === 'cube' ? 64 : 32
  }

  async function refreshStatus() {
    try {
      status = await host.getStatus()
      refreshLayerThumbnails()
      refreshGraphicStatus()
      render()
    } catch (_) {}
  }

  function parseLayout(raw, legacy) {
    if (typeof raw === 'string' && raw.trim()) {
      try {
        const parsed = JSON.parse(raw)
        if (parsed?.version === 1 && Array.isArray(parsed.elements)) {
          return {
            version: 1,
            elements: parsed.elements.slice(0, maxElements).map(normalizeElement).filter(Boolean),
          }
        }
      } catch (_) {}
    }
    const legacyLines = [legacy.line1, legacy.line2, legacy.line3, legacy.line4]
    if (!legacyLines.some((value) => typeof value === 'string')) return defaultLayout(settings.rotation)
    const migrated = legacyLineLayout(settings.rotation)
    migrated.elements.forEach((item, index) => {
      if (typeof legacyLines[index] === 'string' && legacyLines[index]) item.text = legacyLines[index]
    })
    return migrated
  }

  function defaultLayout(rotation) {
    if (deviceVariant === 'pcie') return pcieDefaultLayout()
    if (deviceVariant === 'cube') return cubeDefaultLayout()
    return legacyLineLayout(rotation)
  }

  function pcieDefaultLayout() {
    return {
      version: 1,
      elements: [
        { id: 'text2', type: 'text', name: 'IP Address', x: 0, y: 25, text: '{ip}', scale: 1, font_width: 4, width: 24, full_width: true, align: 'center' },
        { id: 'graphic1', type: 'graphic', name: 'Video Status', x: 3, y: 15, mode: 'hdmi', content: 'text', text: 'VIDEO', scale: 1, font_width: 3, filled: true, bordered: false, width: 16, height: 8, pixels: 'FFFF800180018001800180018001FFFF' },
        { id: 'live1', type: 'graphic', name: 'LIVE Status', x: 41, y: 1, mode: 'live', content: 'text', text: 'LIVE', scale: 1, font_width: 3, filled: true, bordered: false, width: 5, height: 5, pixels: '77FFF70' },
        { id: 'line1', type: 'line', name: 'Line', x: 0, y: 23, x2: 63, y2: 23 },
        { id: 'text1', type: 'text', name: 'Video', x: 0, y: 9, text: '{codec} - {fps} FPS', scale: 1, font_width: 3, width: 37, full_width: true, align: 'center' },
        { id: 'graphic2', type: 'graphic', name: 'NET Status', x: 30, y: 15, mode: 'eth', content: 'text', text: 'ETH', scale: 1, font_width: 3, filled: true, bordered: false, width: 16, height: 8, pixels: 'FFFF800180018001800180018001FFFF' },
        { id: 'graphic3', type: 'graphic', name: 'HID Status', x: 48, y: 15, mode: 'hid', content: 'text', text: 'HID', scale: 1, font_width: 3, filled: true, bordered: false, width: 16, height: 8, pixels: 'FFFF800180018001800180018001FFFF' },
        { id: 'rect1', type: 'rect', x: 1, y: 1, width: 40, height: 7, filled: true, text: 'ONEKVM', scale: 1, font_width: 5, align: 'center' },
      ],
    }
  }

  function cubeDefaultLayout() {
    return {
      version: 1,
      elements: [
        { id: 'text2', type: 'text', name: 'IP', x: 73, y: 54, text: '{ip}', scale: 1, font_width: 4, width: 0, full_width: false, align: 'left' },
        { id: 'line1', type: 'line', x: 0, y: 49, x2: 127, y2: 49 },
        { id: 'graphic1', type: 'graphic', name: 'NET', x: 2, y: 53, mode: 'eth', content: 'text', text: 'ETH', scale: 1, font_width: 5, filled: true, bordered: false, width: 16, height: 8, pixels: 'FFFF800180018001800180018001FFFF' },
        { id: 'graphic2', type: 'graphic', name: 'HDMI', x: 23, y: 53, mode: 'hdmi', content: 'text', text: 'HDMI', scale: 1, font_width: 5, filled: true, bordered: false, width: 16, height: 8, pixels: 'FFFF800180018001800180018001FFFF' },
        { id: 'graphic3', type: 'graphic', name: 'Live Status', x: 96, y: 2, mode: 'live', content: 'text', text: 'LIVE', scale: 1, font_width: 5, filled: true, bordered: false, width: 16, height: 8, pixels: 'FFFF800180018001800180018001FFFF' },
        { id: 'graphic4', type: 'graphic', name: 'HID', x: 50, y: 53, mode: 'hid', content: 'text', text: 'HID', scale: 1, font_width: 5, filled: true, bordered: false, width: 16, height: 8, pixels: 'FFFF800180018001800180018001FFFF' },
        { id: 'text3', type: 'text', name: 'Hostname', x: 0, y: 42, text: '{hostname}', scale: 1, font_width: 5, width: 73, full_width: true, align: 'center' },
        { id: 'rect1', type: 'rect', name: 'Logo', x: 3, y: 2, width: 39, height: 7, filled: true, text: 'ONEKVM', scale: 1, font_width: 5, align: 'center' },
        { id: 'text1', type: 'text', name: 'Res', x: 0, y: 32, text: '{resolution} x {fps}HZ', scale: 1, font_width: 5, width: 60, full_width: true, align: 'center' },
        { id: 'rect2', type: 'rect', name: 'Codec', x: 56, y: 2, width: 20, height: 7, filled: true, text: '{codec}', scale: 1, font_width: 4, align: 'center' },
      ],
    }
  }

  function legacyLineLayout(rotation) {
    const logicalHeight = rotation === 90 || rotation === 270 ? screenWidth : screenHeight
    const lineHeight = Math.max(1, Math.floor(logicalHeight / 4))
    const values = ['ONEKVM', 'IP {ip}', '{codec} {fps}FPS', '{stream}']
    return {
      version: 1,
      elements: values.map((value, index) => ({
        id: `text${index + 1}`, type: 'text', x: 0, y: 1 + index * lineHeight, text: value, scale: 1,
        font_width: 4, width: 0, full_width: false, align: 'left',
      })),
    }
  }

  function restoreDefaultCanvas() {
    layout = defaultLayout(settings.rotation)
    replaceSelection(null)
    clampAllElements()
    setError('')
    syncEditor()
  }

  function statusDefaultText(mode) {
    if (mode === 'wifi') return 'WI-FI'
    if (mode === 'live') return 'LIVE'
    return String(mode || 'eth').toUpperCase()
  }

  function statusLabel(mode) {
    const keys = { eth: 'ethStatus', hdmi: 'hdmiStatus', hid: 'hidStatus', wifi: 'wifiStatus', live: 'liveStatus' }
    return text[keys[mode]] || String(mode).toUpperCase()
  }

  function normalizeElement(item) {
    if (!item || !['text', 'line', 'rect', 'graphic', 'animation'].includes(item.type) || typeof item.id !== 'string') return null
    const normalized = {
      id: item.id.slice(0, 32), type: item.type,
      name: typeof item.name === 'string' ? [...item.name].slice(0, 64).join('') : '',
      hidden: item.hidden === true,
      x: integer(item.x, 0), y: integer(item.y, 0),
    }
    if (item.type === 'text') {
      normalized.text = typeof item.text === 'string' ? [...item.text].slice(0, 128).join('') : ''
      normalized.scale = item.scale === 2 ? 2 : 1
      normalized.font_width = [3, 4, 5].includes(item.font_width) ? item.font_width : 4
      normalized.width = clamp(integer(item.width, 0), 0, Math.max(screenWidth, screenHeight))
      normalized.full_width = item.full_width === true
      normalized.align = textAlignments.includes(item.align) ? item.align : 'left'
    } else if (item.type === 'line') {
      normalized.x2 = integer(item.x2, normalized.x)
      normalized.y2 = integer(item.y2, normalized.y)
    } else if (item.type === 'rect') {
      normalized.width = integer(item.width, 12)
      normalized.height = integer(item.height, 8)
      normalized.filled = item.filled === true
      normalized.text = typeof item.text === 'string' ? [...item.text].slice(0, 128).join('') : ''
      normalized.scale = item.scale === 2 ? 2 : 1
      normalized.font_width = [3, 4, 5].includes(item.font_width) ? item.font_width : 4
      normalized.align = textAlignments.includes(item.align) ? item.align : 'center'
    } else if (item.type === 'animation') {
      normalized.asset = typeof item.asset === 'string' ? item.asset.slice(0, 96) : ''
      normalized.mode = 'custom'
      normalized.content = 'custom'
      normalized.width = clamp(integer(item.width, 16), 1, Math.max(screenWidth, screenHeight))
      normalized.height = clamp(integer(item.height, 8), 1, Math.max(screenWidth, screenHeight))
      normalized.pixels = normalizePixels(item.pixels, normalized.width, normalized.height)
    } else {
      normalized.mode = graphicModes.includes(item.mode) ? item.mode : 'custom'
      normalized.width = clamp(integer(item.width, 16), 1, Math.max(screenWidth, screenHeight))
      normalized.height = clamp(integer(item.height, 8), 1, Math.max(screenWidth, screenHeight))
      normalized.pixels = normalizePixels(item.pixels, normalized.width, normalized.height)
      normalized.content = graphicContents.includes(item.content) ? item.content : 'text'
      normalized.text = typeof item.text === 'string' ? [...item.text].slice(0, 128).join('') : statusDefaultText(normalized.mode)
      normalized.scale = item.scale === 2 ? 2 : 1
      normalized.font_width = [3, 4, 5].includes(item.font_width) ? item.font_width : 4
      normalized.filled = item.filled !== false
      normalized.bordered = item.bordered === true
      normalized.fixed_size = item.fixed_size === true && normalized.mode !== 'custom' && normalized.content !== 'custom'
    }
    return normalized
  }

  function normalizePixels(value, width, height) {
    const length = Math.ceil(width * height / 4)
    return String(value || '').replace(/[^0-9a-f]/gi, '').toUpperCase().slice(0, length).padEnd(length, '0')
  }

  function graphicPixel(item, x, y) {
    if (x < 0 || x >= item.width || y < 0 || y >= item.height) return false
    const index = y * item.width + x
    const nibble = Number.parseInt(item.pixels[Math.floor(index / 4)] || '0', 16)
    return Boolean(nibble & (1 << (3 - index % 4)))
  }

  function setGraphicPixel(item, x, y, enabled) {
    if (x < 0 || x >= item.width || y < 0 || y >= item.height) return
    const index = y * item.width + x
    const nibbleIndex = Math.floor(index / 4)
    const mask = 1 << (3 - index % 4)
    const current = Number.parseInt(item.pixels[nibbleIndex] || '0', 16)
    const next = enabled ? current | mask : current & ~mask
    item.pixels = `${item.pixels.slice(0, nibbleIndex)}${next.toString(16).toUpperCase()}${item.pixels.slice(nibbleIndex + 1)}`
  }

  function resizeGraphic(item, width, height) {
    const old = { width: item.width, height: item.height, pixels: item.pixels }
    const maximum = Math.max(screenWidth, screenHeight)
    item.width = clamp(integer(width, 1), 1, maximum)
    item.height = clamp(integer(height, 1), 1, maximum)
    item.pixels = normalizePixels('', item.width, item.height)
    for (let y = 0; y < Math.min(old.height, item.height); y += 1) {
      for (let x = 0; x < Math.min(old.width, item.width); x += 1) {
        if (graphicPixel(old, x, y)) setGraphicPixel(item, x, y, true)
      }
    }
  }

  function integer(value, fallback) {
    const parsed = Number(value)
    return Number.isInteger(parsed) ? parsed : fallback
  }

  function logicalSize() {
    const portrait = settings.rotation === 90 || settings.rotation === 270
    return { width: portrait ? screenHeight : screenWidth, height: portrait ? screenWidth : screenHeight, portrait }
  }

  function syncGlobalForm() {
    element('display-enabled').checked = settings.display_enabled === true
    element('sleep').value = String(settings.sleep_seconds)
    element('burn-in').checked = settings.burn_in_protection === true
    syncDimensionLimits()
    host.onRotationState?.(settings.rotation)
  }

  function syncDimensionLimits() {
    const size = logicalSize()
    for (const id of ['toolbar-x', 'element-x2']) element(id).max = String(size.width - 1)
    for (const id of ['toolbar-y', 'element-y2']) element(id).max = String(size.height - 1)
    for (const id of ['element-width', 'element-text-width', 'element-graphic-width']) element(id).max = String(size.width)
    for (const id of ['element-height', 'element-graphic-height']) element(id).max = String(size.height)
  }

  function collectGlobalForm() {
    settings.display_enabled = element('display-enabled').checked
    settings.burn_in_protection = element('burn-in').checked
    settings.sleep_seconds = clamp(integer(element('sleep').value, 3600), 0, 86400)
  }

  function syncEditor() {
    renderElementList()
    syncInspector()
    render()
  }

  function selectedElement() {
    return layout.elements.find((item) => item.id === selectedID) || null
  }

  function selectedElements() {
    return layout.elements.filter((item) => selectedIDs.has(item.id))
  }

  function replaceSelection(id) {
    selectedIDs = new Set()
    selectedID = null
    if (id && layout.elements.some((item) => item.id === id)) {
      selectedIDs.add(id)
      selectedID = id
    }
  }

  function additiveSelection(event) {
    return event.shiftKey || event.ctrlKey || event.metaKey
  }

  function renderElementList() {
    const list = element('elements-list')
    if (list) list.replaceChildren()
    ;[...layout.elements].reverse().forEach((item) => {
      if (!list) return
      const row = document.createElement('div')
      const selected = selectedIDs.has(item.id)
      row.className = `element-row${selected ? ' selected' : ''}${item.hidden ? ' layer-hidden' : ''}`
      row.setAttribute('role', 'option')
      row.setAttribute('aria-selected', String(selected))
      row.tabIndex = 0
      row.draggable = true
      row.dataset.layerId = item.id

      const visibility = document.createElement('button')
      visibility.type = 'button'
      visibility.className = 'layer-visibility'
      visibility.textContent = item.hidden ? '○' : '●'
      visibility.title = item.hidden ? text.showLayer : text.hideLayer
      visibility.setAttribute('aria-label', visibility.title)
      visibility.setAttribute('aria-pressed', String(!item.hidden))
      visibility.addEventListener('click', (event) => {
        event.stopPropagation()
        item.hidden = !item.hidden
        renderElementList()
        render()
      })

      const thumbnail = document.createElement('canvas')
      thumbnail.className = 'layer-thumbnail'
      thumbnail.width = 38
      thumbnail.height = 24
      thumbnail.setAttribute('aria-hidden', 'true')
      renderLayerThumbnail(thumbnail, item)

      const label = document.createElement('span')
      label.className = 'element-label'
      const name = document.createElement('span')
      name.className = 'layer-name'
      name.textContent = layerDisplayName(item)
      const kind = document.createElement('span')
      kind.className = 'layer-kind'
      kind.textContent = text[item.type]
      label.append(name, kind)
      const position = document.createElement('span')
      position.className = 'element-position'
      position.textContent = `${item.x},${item.y}`
      const handle = document.createElement('span')
      handle.className = 'layer-handle'
      handle.textContent = '⋮⋮'
      handle.setAttribute('aria-hidden', 'true')

      row.append(visibility, thumbnail, label, position, handle)
      row.addEventListener('click', (event) => selectElement(item.id, additiveSelection(event)))
      row.addEventListener('dblclick', (event) => {
        if (event.target.closest('button')) return
        event.preventDefault()
        editElement(item)
      })
      row.addEventListener('keydown', (event) => {
        if (event.target !== row) return
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault()
          selectElement(item.id, additiveSelection(event))
        }
      })
      row.addEventListener('dragstart', (event) => {
        layerDragID = item.id
        row.classList.add('dragging')
        event.dataTransfer.effectAllowed = 'move'
        event.dataTransfer.setData('text/plain', item.id)
      })
      row.addEventListener('dragover', (event) => {
        if (!layerDragID || layerDragID === item.id) return
        event.preventDefault()
        const before = event.clientY < row.getBoundingClientRect().top + row.offsetHeight / 2
        row.classList.toggle('drop-before', before)
        row.classList.toggle('drop-after', !before)
      })
      row.addEventListener('dragleave', () => row.classList.remove('drop-before', 'drop-after'))
      row.addEventListener('drop', (event) => {
        event.preventDefault()
        const before = event.clientY < row.getBoundingClientRect().top + row.offsetHeight / 2
        reorderLayerByDrop(layerDragID, item.id, before)
      })
      row.addEventListener('dragend', clearLayerDrag)
      list.append(row)
    })
    element('element-count').textContent = `${layout.elements.length} / ${maxElements}`
    const full = layout.elements.length >= maxElements
    host.onLayerState?.({ count: layout.elements.length, max: maxElements, full, empty: layout.elements.length === 0 })
    host.onLayersState?.(layout.elements.map((item) => {
      const thumbnail = document.createElement('canvas')
      thumbnail.width = 38
      thumbnail.height = 24
      renderLayerThumbnail(thumbnail, item)
      return { id: item.id, name: layerDisplayName(item), type: item.type, x: item.x, y: item.y, hidden: item.hidden, thumbnail: thumbnail.toDataURL('image/png') }
    }))
  }

  function layerDisplayName(item) {
    return item.name || ((item.type === 'text' || item.type === 'rect') && item.text ? item.text : text[item.type])
  }

  function renderLayerThumbnail(canvas, item) {
    const { width, height } = logicalSize()
    const pixels = new Uint8Array(width * height)
    drawElement(pixels, width, height, item)
    const context = canvas.getContext('2d')
    context.fillStyle = '#070b09'
    context.fillRect(0, 0, canvas.width, canvas.height)
    const scale = Math.min((canvas.width - 4) / width, (canvas.height - 4) / height)
    const offsetX = (canvas.width - width * scale) / 2
    const offsetY = (canvas.height - height * scale) / 2
    context.fillStyle = item.hidden ? '#42534a' : '#f4f7f6'
    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        if (pixels[y * width + x]) context.fillRect(offsetX + x * scale, offsetY + y * scale, Math.max(1, scale), Math.max(1, scale))
      }
    }
  }

  function refreshLayerThumbnails() {
    root.querySelectorAll('.element-row').forEach((row) => {
      const item = layout.elements.find((candidate) => candidate.id === row.dataset.layerId)
      const thumbnail = row.querySelector('.layer-thumbnail')
      if (item && thumbnail) renderLayerThumbnail(thumbnail, item)
    })
  }

  function clearLayerDrag() {
    layerDragID = null
    root.querySelectorAll('.element-row').forEach((row) => row.classList.remove('dragging', 'drop-before', 'drop-after'))
  }

  function reorderLayerByDrop(sourceID, targetID, before) {
    const sourceIndex = layout.elements.findIndex((item) => item.id === sourceID)
    if (sourceIndex < 0 || sourceID === targetID) {
      clearLayerDrag()
      return
    }
    const [source] = layout.elements.splice(sourceIndex, 1)
    const targetIndex = layout.elements.findIndex((item) => item.id === targetID)
    layout.elements.splice(before ? targetIndex + 1 : targetIndex, 0, source)
    replaceSelection(source.id)
    clearLayerDrag()
    syncEditor()
  }

  function syncInspector() {
    const items = selectedElements()
    const item = items.length === 1 ? selectedElement() : null
    if (bitmapImport && item?.id !== bitmapImport.targetID) closeBitmapImporter()
    element('toolbar-details').classList.toggle('hidden', !item)
    element('delete-element').disabled = items.length === 0
    element('copy-element').disabled = items.length === 0
    element('paste-element').disabled = layout.elements.length >= maxElements
    const index = item ? layout.elements.indexOf(item) : -1
    element('move-down').disabled = items.length !== 1 || index <= 0
    element('move-up').disabled = items.length !== 1 || index < 0 || index >= layout.elements.length - 1
    element('center-horizontal').disabled = items.length === 0
    element('center-vertical').disabled = items.length === 0
    element('distribute-horizontal').disabled = items.length < 3
    element('distribute-vertical').disabled = items.length < 3
    syncFloatingToolbar(item, items.length)
    if (!item) return
    syncTypeOptions(item)
    element('text-properties').classList.toggle('hidden', item.type !== 'text')
    element('line-properties').classList.toggle('hidden', item.type !== 'line')
    element('rect-properties').classList.toggle('hidden', item.type !== 'rect')
    element('graphic-properties').classList.toggle('hidden', item.type !== 'graphic')
    element('animation-properties').classList.toggle('hidden', item.type !== 'animation')
    if (item.type === 'text') {
      element('element-full-width').checked = item.full_width
      element('element-text-width').value = String(textRegionWidth(item, logicalSize().width))
      element('element-text-width').disabled = item.full_width
    } else if (item.type === 'line') {
      element('element-x2').value = String(item.x2)
      element('element-y2').value = String(item.y2)
    } else if (item.type === 'rect') {
      element('element-width').value = String(item.width)
      element('element-height').value = String(item.height)
    } else if (item.type === 'animation') {
      element('element-animation-asset').value = item.asset || ''
      element('element-animation-width').value = String(item.width)
      element('element-animation-height').value = String(item.height)
    } else {
      element('element-graphic-width').value = String(item.width)
      element('element-graphic-height').value = String(item.height)
      const statusGraphic = item.mode !== 'custom'
      const customPixels = item.mode === 'custom' || (item.mode !== 'live' && item.content === 'custom')
      element('graphic-custom-properties').classList.toggle('hidden', !customPixels)
      element('graphic-status').classList.toggle('hidden', !statusGraphic)
      element('graphic-status-properties').classList.toggle('hidden', !statusGraphic || item.mode === 'live')
      if (statusGraphic) {
        element('element-graphic-content').value = item.content
        element('element-graphic-filled').checked = item.filled
        element('element-graphic-bordered').checked = item.bordered
        element('graphic-status').textContent = graphicStatusText(item.mode)
      }
      if (customPixels) renderBitmapEditor(item)
    }
  }

  function syncFloatingToolbar(item, selectedCount) {
    const toolbar = element('floating-toolbar')
    toolbar.classList.toggle('empty', selectedCount === 0)
    if (selectedCount === 0) {
      element('toolbar-kind').textContent = layout.elements.length === 0 ? text.toolbarEmptyHint : text.toolbarSelectHint
      notifyToolbarState(null, 0)
      return
    }
    const multiple = selectedCount > 1
    element('toolbar-name-control').classList.toggle('hidden', multiple)
    element('toolbar-coordinates').classList.toggle('hidden', multiple)
    if (multiple) {
      element('toolbar-kind').textContent = text.selectedCount.replace('{count}', String(selectedCount))
      notifyToolbarState(null, selectedCount)
      return
    }
    element('toolbar-kind').textContent = text[item.type]
    element('toolbar-name').value = item.name || ''
    element('toolbar-name').placeholder = layerDisplayName(item)
    element('toolbar-x').value = String(item.x)
    element('toolbar-y').value = String(item.y)
    element('toolbar-x').disabled = item.type === 'text' && item.full_width
    notifyToolbarState(item, selectedCount)
  }

  function notifyToolbarState(item, selectedCount) {
    const index = item ? layout.elements.indexOf(item) : -1
    const size = logicalSize()
    host.onToolbarState?.({
      layerCount: layout.elements.length,
      selectedCount,
      type: item?.type || '',
      name: item?.name || '',
      x: item?.x || 0,
      y: item?.y || 0,
      maxX: size.width - 1,
      maxY: size.height - 1,
      xDisabled: Boolean(item?.type === 'text' && item.full_width),
      canDelete: selectedCount > 0,
      canCopy: selectedCount > 0,
      canPaste: layout.elements.length < maxElements,
      canMoveDown: selectedCount === 1 && index > 0,
      canMoveUp: selectedCount === 1 && index >= 0 && index < layout.elements.length - 1,
      canCenter: selectedCount > 0,
      canDistribute: selectedCount >= 3,
    })
  }

  function updateToolbar(field, value) {
    if (field === 'name') updateSelected('name', [...String(value)].slice(0, 64).join(''))
    else if (field === 'x') updateSelected('x', clamp(integer(value, 0), 0, logicalSize().width - 1))
    else if (field === 'y') updateSelected('y', clamp(integer(value, 0), 0, logicalSize().height - 1))
  }

  function runToolbarAction(action) {
    if (action === 'center-horizontal') centerSelection('horizontal')
    else if (action === 'center-vertical') centerSelection('vertical')
    else if (action === 'distribute-horizontal') distributeSelection('horizontal')
    else if (action === 'distribute-vertical') distributeSelection('vertical')
    else if (action === 'copy') copySelected()
    else if (action === 'paste') void pasteCopied()
    else if (action === 'move-down') reorderSelected(-1)
    else if (action === 'move-up') reorderSelected(1)
    else if (action === 'delete') deleteSelected()
  }

  function syncTypeOptions(item) {
    const editableText = item.type === 'text' || item.type === 'rect' || (item.type === 'graphic' && item.mode !== 'custom' && item.content === 'text')
    const scalableText = editableText && !(item.type === 'graphic' && item.mode === 'live')
    const alignedText = item.type === 'text' || item.type === 'rect'
    const filledRect = item.type === 'rect'
    const graphicMode = item.type === 'graphic'
    element('element-type-options').classList.toggle('hidden', !editableText && !filledRect && !graphicMode)
    element('element-content-control').classList.toggle('hidden', !editableText)
    element('element-scale-control').classList.toggle('hidden', !scalableText)
    element('element-font-width-control').classList.toggle('hidden', !editableText)
    element('element-align-control').classList.toggle('hidden', !alignedText)
    element('element-filled-control').classList.toggle('hidden', !filledRect)
    element('element-graphic-mode-control').classList.toggle('hidden', !graphicMode)
    if (editableText) {
      element('element-content').value = item.text
      element('element-scale').value = String(item.scale)
      element('element-font-width').value = String(item.font_width)
    }
    if (alignedText) element('element-align').value = item.align
    if (filledRect) element('element-filled').checked = item.filled
    if (graphicMode) element('element-graphic-mode').value = item.mode
  }

  function graphicConnected(mode) {
    if (mode === 'eth') return status?.network?.ethernet_connected ?? Boolean(status?.network?.ip)
    if (mode === 'hdmi') return status?.video?.hdmi_connected ?? Boolean(status?.video?.active)
    if (mode === 'hid') return Boolean(status?.hid?.connected)
    if (mode === 'wifi') return Boolean(status?.network?.wifi_connected)
    if (mode === 'live') return Boolean(status?.video?.active)
    return false
  }

  function graphicAvailable(mode) {
    if (mode === 'hid') return Boolean(status?.hid?.available)
    if (mode === 'wifi') return Boolean(status?.network?.wifi_available)
    return true
  }

  function graphicStatusText(mode) {
    const state = graphicAvailable(mode) ? (graphicConnected(mode) ? 'connected' : 'disconnected') : 'unavailable'
    return `${statusLabel(mode)}: ${text[state]}`
  }

  function refreshGraphicStatus() {
    const item = selectedElement()
    if (!item || item.type !== 'graphic' || item.mode === 'custom') return
    element('graphic-status').textContent = graphicStatusText(item.mode)
  }

  function textDimensions(value, scale, fontWidth = 4) {
    const characters = [...String(value || '')]
    const width = characters.reduce((total, character) => {
      const advance = isCJKCharacter(character) ? 13 : asciiGlyphWidth(character, fontWidth) + 1
      return total + advance * scale
    }, 0)
    const height = characters.some(isCJKCharacter) ? 12 * scale : 5 * scale
    return { width: Math.max(1, width - scale), height }
  }

  function textRegionWidth(item, canvasWidth) {
    if (item.full_width) return canvasWidth
    return item.width > 0 ? Math.min(item.width, canvasWidth) : textDimensions(expanded(item.text), item.scale, item.font_width).width
  }

  function graphicContentDimensions(item) {
    return item.content === 'custom'
      ? { width: item.width, height: item.height }
      : textDimensions(expanded(item.text), item.scale, item.font_width)
  }

  function naturalGraphicDimensions(item) {
    if (item.mode === 'custom') return { width: item.width, height: item.height }
    if (item.mode === 'live') return { width: 7 + textDimensions(expanded(item.text), 1, item.font_width).width, height: 7 }
    const content = graphicContentDimensions(item)
    const inset = item.filled || item.bordered ? 2 : 0
    return { width: content.width + inset, height: content.height + inset }
  }

  function graphicDimensions(item) {
    const natural = naturalGraphicDimensions(item)
    if (!item.fixed_size || item.mode === 'custom' || item.content === 'custom') return natural
    return { width: Math.max(natural.width, item.width), height: Math.max(natural.height, item.height) }
  }

  function renderBitmapEditor(item) {
    const canvas = element('bitmap-editor')
    const scale = Math.max(5, Math.min(18, Math.floor(480 / Math.max(item.width, item.height))))
    canvas.width = item.width * scale
    canvas.height = item.height * scale
    canvas.style.aspectRatio = `${item.width} / ${item.height}`
    const context = canvas.getContext('2d')
    context.fillStyle = '#080d0b'
    context.fillRect(0, 0, canvas.width, canvas.height)
    for (let y = 0; y < item.height; y += 1) {
      for (let x = 0; x < item.width; x += 1) {
        context.fillStyle = graphicPixel(item, x, y) ? '#f4f7f6' : '#18221e'
        context.fillRect(x * scale + 1, y * scale + 1, Math.max(1, scale - 2), Math.max(1, scale - 2))
      }
    }
  }

  function selectElement(id, additive = false) {
    if (!additive) {
      replaceSelection(id)
    } else if (selectedIDs.has(id)) {
      selectedIDs.delete(id)
      if (selectedID === id) selectedID = selectedElements().at(-1)?.id || null
    } else if (layout.elements.some((item) => item.id === id)) {
      selectedIDs.add(id)
      selectedID = id
    }
    root.querySelectorAll('.element-row').forEach((row) => {
      const selected = selectedIDs.has(row.dataset.layerId)
      row.classList.toggle('selected', selected)
      row.setAttribute('aria-selected', String(selected))
    })
    syncInspector()
    render()
  }

  function editElement(item, fieldID) {
    selectElement(item.id, false)
    const editableText = item.type === 'text' || item.type === 'rect' || (item.type === 'graphic' && item.mode !== 'custom' && item.content === 'text')
    const targetID = fieldID || (editableText ? 'element-content' : 'toolbar-name')
    const input = element(targetID)
    input.focus()
    if (typeof input.select === 'function') input.select()
  }

  function editCanvasElement(event) {
    const item = hitTest(canvasPoint(event))
    if (!item) return
    event.preventDefault()
    editElement(item)
  }

  function nextID(type) {
    let sequence = 1
    while (layout.elements.some((item) => item.id === `${type}${sequence}`)) sequence += 1
    return `${type}${sequence}`
  }

  function addElement(type) {
    if (layout.elements.length >= maxElements) return
    const { width, height } = logicalSize()
    let item
    if (type === 'text') {
      item = { id: nextID('text'), type, x: 2, y: 2, text: 'TEXT', scale: 1, font_width: 4, width: Math.min(24, width - 2), full_width: false, align: 'left' }
    } else if (type === 'line') {
      item = { id: nextID('line'), type, x: 3, y: Math.floor(height / 2), x2: Math.min(width - 4, 28), y2: Math.floor(height / 2) }
    } else if (type === 'rect') {
      item = {
        id: nextID('rect'), type, x: 3, y: 3, width: Math.min(20, width - 3), height: Math.min(12, height - 3), filled: false,
        text: '', scale: 1, font_width: 4, align: 'center',
      }
    } else if (type === 'animation') {
      item = { id: nextID('animation'), type, name: text.animation, x: 3, y: 3, asset: '', mode: 'custom', content: 'custom', width: Math.min(16, width - 3), height: Math.min(8, height - 3), pixels: '' }
      item.pixels = normalizePixels('', item.width, item.height)
    } else if (type === 'live') {
      item = {
        id: nextID('live'), type: 'graphic', name: 'ON LIVE', x: 3, y: 3, mode: 'live', content: 'text', text: 'LIVE', scale: 1,
        font_width: 4, filled: true, bordered: false, width: 5, height: 5, pixels: '7463170',
      }
    } else {
      item = {
        id: nextID('graphic'), type, x: 3, y: 3, mode: 'custom', content: 'text', text: 'ETH', scale: 1,
        font_width: 4, filled: true, bordered: false, width: Math.min(16, width - 3), height: Math.min(8, height - 3), pixels: '',
      }
      item.pixels = normalizePixels('', item.width, item.height)
      for (let y = 0; y < item.height; y += 1) {
        for (let x = 0; x < item.width; x += 1) {
          if (x === 0 || x === item.width - 1 || y === 0 || y === item.height - 1) setGraphicPixel(item, x, y, true)
        }
      }
    }
    layout.elements.push(item)
    replaceSelection(item.id)
    clampElement(item)
    syncEditor()
  }

  function setRotation(value) {
    const next = Number(value)
    if (![0, 90, 180, 270].includes(next) || next === settings.rotation) return
    if (externalMediaActive) stopExternalAssetPreview()
    settings.rotation = next
    clampAllElements()
    syncDimensionLimits()
    host.onRotationState?.(settings.rotation)
    syncEditor()
  }

  function deleteSelected() {
    if (!selectedIDs.size) return
    layout.elements = layout.elements.filter((item) => !selectedIDs.has(item.id))
    replaceSelection(null)
    syncEditor()
  }

  function editableTarget(target) {
    return target instanceof HTMLElement && Boolean(target.closest('input, select, textarea, [contenteditable="true"]'))
  }

  function copySelected() {
    const items = selectedElements()
    if (!items.length) return
    copiedElements = JSON.parse(JSON.stringify(items))
    const payload = JSON.stringify({ format: 'onekvm-oled-layers', elements: copiedElements })
    navigator.clipboard?.writeText(payload).catch(() => {})
  }

  async function pasteCopied() {
    if (layout.elements.length >= maxElements) return
    let sources = copiedElements
    if (!sources.length && navigator.clipboard?.readText) {
      try {
        const payload = JSON.parse(await navigator.clipboard.readText())
        if (payload?.format === 'onekvm-oled-layers' && Array.isArray(payload.elements)) sources = payload.elements
        else if (payload?.format === 'onekvm-oled-layer') sources = [payload.element]
      } catch (_) {}
    }
    const pasted = []
    for (const source of sources.slice(0, maxElements - layout.elements.length)) {
      const item = normalizeElement(source)
      if (!item) continue
      const prefix = item.type === 'graphic' && item.mode === 'live' ? 'live' : item.type
      item.id = nextID(prefix)
      if (item.name) item.name = `${item.name} ${text.copySuffix}`.slice(0, 64)
      item.x += 2
      item.y += 2
      if (item.type === 'line') {
        item.x2 += 2
        item.y2 += 2
      }
      layout.elements.push(item)
      clampElement(item)
      pasted.push(item)
    }
    if (!pasted.length) return
    selectedIDs = new Set(pasted.map((item) => item.id))
    selectedID = pasted[pasted.length - 1].id
    syncEditor()
  }

  function reorderSelected(delta) {
    const index = layout.elements.findIndex((item) => item.id === selectedID)
    const target = index + delta
    if (index < 0 || target < 0 || target >= layout.elements.length) return
    const [item] = layout.elements.splice(index, 1)
    layout.elements.splice(target, 0, item)
    syncEditor()
  }

  function clamp(value, minimum, maximum) {
    return Math.min(Math.max(value, minimum), Math.max(minimum, maximum))
  }

  function elementBounds(item) {
    if (item.type === 'text') {
      const dimensions = textDimensions(expanded(item.text), item.scale, item.font_width)
      return { x: item.full_width ? 0 : item.x, y: item.y, width: textRegionWidth(item, logicalSize().width), height: dimensions.height }
    }
    if (item.type === 'line') {
      const x = Math.min(item.x, item.x2)
      const y = Math.min(item.y, item.y2)
      return { x, y, width: Math.abs(item.x2 - item.x) + 1, height: Math.abs(item.y2 - item.y) + 1 }
    }
    if (item.type === 'graphic') {
      const size = graphicDimensions(item)
      return { x: item.x, y: item.y, width: size.width, height: size.height }
    }
    return { x: item.x, y: item.y, width: item.width, height: item.height }
  }

  function selectionBounds(items) {
    if (!items.length) return null
    const bounds = items.map(elementBounds)
    const x = Math.min(...bounds.map((item) => item.x))
    const y = Math.min(...bounds.map((item) => item.y))
    const right = Math.max(...bounds.map((item) => item.x + item.width))
    const bottom = Math.max(...bounds.map((item) => item.y + item.height))
    return { x, y, width: right - x, height: bottom - y }
  }

  function clampElement(item) {
    const { width, height } = logicalSize()
    if (item.type === 'text' && item.full_width) item.x = 0
    if (item.type === 'text' && item.width > 0) item.width = clamp(integer(item.width, 1), 1, width)
    if (item.type === 'line') {
      item.x = clamp(integer(item.x, 0), 0, width - 1)
      item.y = clamp(integer(item.y, 0), 0, height - 1)
      item.x2 = clamp(integer(item.x2, item.x), 0, width - 1)
      item.y2 = clamp(integer(item.y2, item.y), 0, height - 1)
      return
    }
    if (item.type === 'rect') {
      item.width = clamp(integer(item.width, 1), 1, width)
      item.height = clamp(integer(item.height, 1), 1, height)
    } else if (item.type === 'animation' || (item.type === 'graphic' && (item.mode === 'custom' || item.content === 'custom'))) {
      const nextWidth = clamp(integer(item.width, 1), 1, width)
      const nextHeight = clamp(integer(item.height, 1), 1, height)
      if (nextWidth !== item.width || nextHeight !== item.height) resizeGraphic(item, nextWidth, nextHeight)
      else item.pixels = normalizePixels(item.pixels, item.width, item.height)
    } else if (item.type === 'graphic' && item.fixed_size) {
      const natural = naturalGraphicDimensions(item)
      item.width = clamp(integer(item.width, natural.width), Math.min(natural.width, width), width)
      item.height = clamp(integer(item.height, natural.height), Math.min(natural.height, height), height)
    }
    const bounds = elementBounds(item)
    item.x = clamp(integer(item.x, 0), 0, width - Math.min(bounds.width, width))
    item.y = clamp(integer(item.y, 0), 0, height - Math.min(bounds.height, height))
  }

  function clampAllElements() {
    layout.elements.forEach(clampElement)
  }

  function expanded(value) {
    const values = {
      ip: status?.network?.ip || '--',
      hostname: status?.network?.hostname || '--',
      codec: (status?.video?.codec || 'auto').toUpperCase(),
		fps: String(status?.video?.actual_fps ?? status?.video?.fps ?? 0),
      resolution: status?.video?.input_width > 0 && status?.video?.input_height > 0
        ? `${status.video.input_width}x${status.video.input_height}`
        : (status?.video?.resolution ? `${status.video.resolution}P` : 'AUTO'),
      stream: status?.video?.active ? 'STREAM ACTIVE' : 'KVM READY',
      uptime: compactDuration(status?.uptime_seconds || 0),
      ...cpuUsage(status?.system?.cpu_percent),
      ...byteUsage('mem', status?.system?.memory_used_bytes, status?.system?.memory_total_bytes),
      ...byteUsage(
        'disk',
        (status?.system?.storage_total_bytes || 0) - (status?.system?.storage_available_bytes || 0),
        status?.system?.storage_total_bytes,
        status?.system?.storage_available_bytes,
      ),
    }
    const tokenNames = Object.keys(values).join('|')
    let output = String(value || '').replace(
      new RegExp(`\\{(${tokenNames})\\}`, 'g'),
      (_, key) => values[key],
    )
    const custom = parseVariables(settings.variables_json)
    Object.keys(custom).forEach((key) => {
      output = output.split(`{${key}}`).join(custom[key] || '--')
    })
    return output
  }

  function cpuUsage(percent) {
    if (percent == null) {
      return { cpu_used: '--', cpu_free: '--', cpu_available: '--', cpu_percent: '--' }
    }
    const used = Math.max(0, Math.round(percent))
    const free = Math.max(0, 100 - used)
    return {
      cpu_used: `${used}%`,
      cpu_free: `${free}%`,
      cpu_available: `${free}%`,
      cpu_percent: `${used}%`,
    }
  }

  function byteUsage(prefix, used, total, available) {
    const missing = '--'
    if (!total || used == null) {
      return {
        [`${prefix}_used`]: missing,
        [`${prefix}_free`]: missing,
        [`${prefix}_available`]: missing,
        [`${prefix}_percent`]: missing,
      }
    }
    const usedBytes = Number(used) || 0
    const freeBytes = available == null ? Math.max(0, total - usedBytes) : Number(available) || 0
    return {
      [`${prefix}_used`]: formatBytes(usedBytes),
      [`${prefix}_free`]: formatBytes(freeBytes),
      [`${prefix}_available`]: formatBytes(freeBytes),
      [`${prefix}_percent`]: `${Math.floor((usedBytes * 100) / total)}%`,
    }
  }

  function formatBytes(bytes) {
    const value = Number(bytes) || 0
    if (value >= 1024 * 1024 * 1024) return `${Math.floor(value / (1024 * 1024 * 1024))}G`
    if (value >= 1024 * 1024) return `${Math.floor(value / (1024 * 1024))}M`
    if (value >= 1024) return `${Math.floor(value / 1024)}K`
    return `${value}B`
  }

  function parseVariables(raw) {
    try {
      const parsed = JSON.parse(raw || '{}')
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return {}
      const output = {}
      Object.keys(parsed).forEach((key) => {
        if (!/^c_[A-Za-z0-9_]{1,30}$/.test(key)) return
        if (typeof parsed[key] !== 'string') return
        output[key] = parsed[key].slice(0, 64)
      })
      return output
    } catch {
      return {}
    }
  }

  function serializeVariables(values) {
    const output = {}
    Object.keys(values || {}).forEach((key) => {
      if (!/^c_[A-Za-z0-9_]{1,30}$/.test(key)) return
      output[key] = String(values[key] || '').slice(0, 64)
    })
    return JSON.stringify(output)
  }

  function normalizeVariableKey(raw) {
    const trimmed = String(raw || '').trim()
    const withPrefix = trimmed.startsWith('c_') ? trimmed : `c_${trimmed}`
    return withPrefix.replace(/[^A-Za-z0-9_]/g, '_').slice(0, 32)
  }

  function syncVariablesForm() {
    const list = element('variables-list')
    if (!list) return
    const values = parseVariables(settings.variables_json)
    const keys = Object.keys(values)
    list.replaceChildren()
    keys.forEach((key) => {
      const row = document.createElement('div')
      row.className = 'variable-row'
      const keyInput = document.createElement('input')
      keyInput.value = key
      keyInput.maxLength = 32
      keyInput.setAttribute('aria-label', text.variableKey || 'Key')
      const valueInput = document.createElement('input')
      valueInput.value = values[key]
      valueInput.maxLength = 64
      valueInput.setAttribute('aria-label', text.variableValue || 'Value')
      const remove = document.createElement('button')
      remove.type = 'button'
      remove.textContent = text.variableRemove || 'Remove'
      keyInput.addEventListener('change', () => {
        const nextKey = normalizeVariableKey(keyInput.value)
        if (!/^c_[A-Za-z0-9_]{1,30}$/.test(nextKey)) return
        const next = parseVariables(settings.variables_json)
        const current = next[key]
        delete next[key]
        next[nextKey] = current
        settings.variables_json = serializeVariables(next)
        syncVariablesForm()
        syncCustomTokens()
        notifySettingsState()
        render()
      })
      valueInput.addEventListener('input', () => {
        const next = parseVariables(settings.variables_json)
        next[key] = valueInput.value.slice(0, 64)
        settings.variables_json = serializeVariables(next)
        notifySettingsState()
        render()
      })
      remove.addEventListener('click', () => {
        const next = parseVariables(settings.variables_json)
        delete next[key]
        settings.variables_json = serializeVariables(next)
        syncVariablesForm()
        syncCustomTokens()
        notifySettingsState()
        render()
      })
      row.append(keyInput, valueInput, remove)
      list.append(row)
    })
    syncCustomTokens()
  }

  function syncCustomTokens() {
    const host = element('custom-tokens')
    if (!host) return
    host.replaceChildren()
    Object.keys(parseVariables(settings.variables_json)).forEach((key) => {
      const button = document.createElement('button')
      button.type = 'button'
      button.dataset.token = `{${key}}`
      button.textContent = `{${key}}`
      button.addEventListener('click', () => {
        const item = selectedElement()
        if (!item || item.type !== 'text') return
        item.text = `${item.text}${button.dataset.token}`.slice(0, 128)
        syncEditor()
        element('element-content').focus()
      })
      host.append(button)
    })
  }

  function compactDuration(seconds) {
    const minutes = Math.floor(seconds / 60)
    if (minutes >= 60) return `${Math.floor(minutes / 60)}H${String(minutes % 60).padStart(2, '0')}M`
    return `${minutes}M`
  }

  function render() {
    if (externalMediaActive) return
    const { width, height, portrait } = logicalSize()
    preview.width = width * pixelScale
    preview.height = height * pixelScale
    const shell = element('oled-shell')
    shell.classList.toggle('portrait', portrait)
    shell.classList.toggle('disabled', !settings.display_enabled)
    shell.dataset.rotation = String(settings.rotation)

    const pixels = new Uint8Array(width * height)
    layout.elements.filter((item) => !item.hidden).forEach((item) => drawElement(pixels, width, height, item))
    const context = preview.getContext('2d')
    context.fillStyle = '#020706'
    context.fillRect(0, 0, preview.width, preview.height)
    context.fillStyle = settings.display_enabled ? '#f4f7f6' : 'rgba(244, 247, 246, .34)'
    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        if (pixels[y * width + x]) context.fillRect(x * pixelScale, y * pixelScale, pixelScale - 1, pixelScale - 1)
      }
    }
    drawSelection(context, width, height)
    notifySettingsState()
  }

  function stopExternalAssetPreview() {
    if (externalMediaTimer !== null) clearInterval(externalMediaTimer)
    externalMediaTimer = null
    externalMediaActive = false
    render()
  }

  function previewAsset(asset, bytes) {
    if (!(bytes instanceof Uint8Array)) return
    if (externalMediaTimer !== null) clearInterval(externalMediaTimer)
    externalMediaTimer = null
    externalMediaActive = false
    const parsed = parseOanBytes(bytes)
    if (!parsed) {
      render()
      return
    }
    const { width, height } = logicalSize()
    externalMediaActive = true
    preview.width = width * pixelScale
    preview.height = height * pixelScale
    const context = preview.getContext('2d')
    if (!context) return
    const originX = Math.floor((width - parsed.width) / 2)
    const originY = Math.floor((height - parsed.height) / 2)
    const started = performance.now()
    const paint = () => {
      const step = oanStepAt(parsed, performance.now() - started)
      context.fillStyle = '#020706'
      context.fillRect(0, 0, preview.width, preview.height)
      context.fillStyle = settings.display_enabled ? '#f4f7f6' : 'rgba(244, 247, 246, .34)'
      for (let y = 0; y < parsed.height; y += 1) {
        for (let x = 0; x < parsed.width; x += 1) {
          if ((parsed.pixels[step.offset + (y >> 3) * parsed.width + x] & (1 << (y & 7))) === 0) continue
          const targetX = originX + x
          const targetY = originY + y
          if (targetX < 0 || targetX >= width || targetY < 0 || targetY >= height) continue
          context.fillRect(targetX * pixelScale, targetY * pixelScale, pixelScale - 1, pixelScale - 1)
        }
      }
    }
    paint()
    if (parsed.steps.length > 1) externalMediaTimer = setInterval(paint, 33)
  }

  function drawElement(pixels, width, height, item) {
    if (item.type === 'text') {
      const bounds = elementBounds(item)
      drawText(pixels, width, height, alignedTextX(item, width), item.y, expanded(item.text), item.scale, item.font_width, true, bounds)
    }
    else if (item.type === 'line') drawLine(pixels, width, height, item.x, item.y, item.x2, item.y2)
    else if (item.type === 'rect') drawRectangleElement(pixels, width, height, item)
    else drawGraphic(pixels, width, height, item)
  }

  function alignedTextX(item, canvasWidth) {
    const regionX = item.full_width ? 0 : item.x
    const regionWidth = textRegionWidth(item, canvasWidth)
    const contentWidth = textDimensions(expanded(item.text), item.scale, item.font_width).width
    if (item.align === 'right') return regionX + Math.max(0, regionWidth - contentWidth)
    if (item.align === 'center') return regionX + Math.max(0, Math.floor((regionWidth - contentWidth) / 2))
    return regionX
  }

  function setPixel(pixels, width, height, x, y, enabled = true) {
    if (x >= 0 && x < width && y >= 0 && y < height) pixels[y * width + x] = enabled ? 1 : 0
  }

  function drawText(pixels, width, height, startX, startY, value, scale, fontWidth = 4, enabled = true, clip = null) {
    let x = startX
    for (const character of value.toUpperCase()) {
      const wide = isCJKCharacter(character)
      const cjk = cjkGlyph(character.codePointAt(0))
      const glyph = cjk || latinFonts[fontWidth]?.[character] || commonFont[character] || commonFont['?']
      const glyphWidth = cjk ? 12 : asciiGlyphWidth(character, fontWidth)
      glyph.forEach((bits, row) => {
        for (let column = 0; column < glyphWidth; column += 1) {
          if (!(bits & (1 << (glyphWidth - 1 - column)))) continue
          for (let offsetY = 0; offsetY < scale; offsetY += 1) {
            for (let offsetX = 0; offsetX < scale; offsetX += 1) {
              const pixelX = x + column * scale + offsetX
              const pixelY = startY + row * scale + offsetY
              if (!clip || (pixelX >= clip.x && pixelX < clip.x + clip.width && pixelY >= clip.y && pixelY < clip.y + clip.height)) {
                setPixel(pixels, width, height, pixelX, pixelY, enabled)
              }
            }
          }
        }
      })
      x += (wide ? 13 : glyphWidth + 1) * scale
      if (x >= width) break
    }
  }

  function drawLine(pixels, width, height, x0, y0, x1, y1) {
    const dx = Math.abs(x1 - x0)
    const sx = x0 < x1 ? 1 : -1
    const dy = -Math.abs(y1 - y0)
    const sy = y0 < y1 ? 1 : -1
    let error = dx + dy
    while (true) {
      setPixel(pixels, width, height, x0, y0)
      if (x0 === x1 && y0 === y1) break
      const twice = 2 * error
      if (twice >= dy) { error += dy; x0 += sx }
      if (twice <= dx) { error += dx; y0 += sy }
    }
  }

  function drawRect(pixels, canvasWidth, canvasHeight, x, y, width, height, filled, enabled = true) {
    for (let row = 0; row < height; row += 1) {
      for (let column = 0; column < width; column += 1) {
        if (filled || row === 0 || row === height - 1 || column === 0 || column === width - 1) {
          setPixel(pixels, canvasWidth, canvasHeight, x + column, y + row, enabled)
        }
      }
    }
  }

  function drawRectangleElement(pixels, canvasWidth, canvasHeight, item) {
    drawRect(pixels, canvasWidth, canvasHeight, item.x, item.y, item.width, item.height, item.filled)
    if (!item.text || item.width <= 2 || item.height <= 2) return
    const content = expanded(item.text)
    const dimensions = textDimensions(content, item.scale, item.font_width)
    const region = { x: item.x + 1, y: item.y + 1, width: item.width - 2, height: item.height - 2 }
    let textX = region.x
    if (item.align === 'right') textX += Math.max(0, region.width - dimensions.width)
    else if (item.align === 'center') textX += Math.max(0, Math.floor((region.width - dimensions.width) / 2))
    const textY = region.y + Math.max(0, Math.floor((region.height - dimensions.height) / 2))
    drawText(pixels, canvasWidth, canvasHeight, textX, textY, content, item.scale, item.font_width, !item.filled, region)
  }

  function drawGraphic(pixels, canvasWidth, canvasHeight, item) {
    if (item.mode === 'custom') {
      drawGraphicPixels(pixels, canvasWidth, canvasHeight, item, item.x, item.y)
      return
    }
    if (item.mode === 'live') {
      const connected = graphicConnected('live')
      const content = expanded(item.text)
      const naturalWidth = 7 + textDimensions(content, 1, item.font_width).width
      const bounds = graphicDimensions(item)
      const contentX = item.x + Math.floor((bounds.width - naturalWidth) / 2)
      const contentY = item.y + Math.floor((bounds.height - 7) / 2)
      if (connected) drawRect(pixels, canvasWidth, canvasHeight, item.x, item.y, bounds.width, bounds.height, true)
      const enabled = !connected
      const textRegion = { x: contentX + 7, y: contentY + 1, width: naturalWidth - 7, height: 5 }
      drawText(pixels, canvasWidth, canvasHeight, textRegion.x, textRegion.y, content, 1, item.font_width, enabled, textRegion)
      const circleBorder = ['01110', '10001', '10001', '10001', '01110']
      circleBorder.forEach((row, y) => [...row].forEach((value, x) => {
        if (value === '1') setPixel(pixels, canvasWidth, canvasHeight, contentX + 1 + x, contentY + 1 + y, enabled)
      }))
      if (connected && liveIndicatorOn) drawRect(pixels, canvasWidth, canvasHeight, contentX + 2, contentY + 2, 3, 3, true, false)
      return
    }
    const content = graphicContentDimensions(item)
    const framed = item.filled || item.bordered
    const offset = framed ? 1 : 0
    const naturalWidth = content.width + (framed ? 2 : 0)
    const naturalHeight = content.height + (framed ? 2 : 0)
    const bounds = graphicDimensions(item)
    const contentX = item.x + Math.floor((bounds.width - naturalWidth) / 2) + offset
    const contentY = item.y + Math.floor((bounds.height - naturalHeight) / 2) + offset
    const connected = graphicConnected(item.mode)
    if (connected && item.filled) {
      drawRect(pixels, canvasWidth, canvasHeight, item.x, item.y, bounds.width, bounds.height, true)
    } else if (connected && item.bordered) {
      drawRect(pixels, canvasWidth, canvasHeight, item.x, item.y, bounds.width, bounds.height, false)
    }
    const enabled = !(connected && item.filled)
    if (item.content === 'custom') {
      drawGraphicPixels(pixels, canvasWidth, canvasHeight, item, contentX, contentY, enabled)
    } else {
      const region = { x: contentX, y: contentY, width: content.width, height: content.height }
      drawText(pixels, canvasWidth, canvasHeight, contentX, contentY, expanded(item.text), item.scale, item.font_width, enabled, region)
    }
  }

  function drawGraphicPixels(pixels, canvasWidth, canvasHeight, item, startX, startY, enabled = true) {
    for (let y = 0; y < item.height; y += 1) {
      for (let x = 0; x < item.width; x += 1) {
        if (graphicPixel(item, x, y)) setPixel(pixels, canvasWidth, canvasHeight, startX + x, startY + y, enabled)
      }
    }
  }

  function drawSelection(context, canvasWidth, canvasHeight) {
    const items = selectedElements()
    if (!items.length) return
    context.save()
    context.lineWidth = 1
    context.setLineDash([3, 2])
    items.forEach((item) => {
      const bounds = elementBounds(item)
      const x = clamp(bounds.x, 0, canvasWidth - 1)
      const y = clamp(bounds.y, 0, canvasHeight - 1)
      const width = Math.min(bounds.width, canvasWidth - x)
      const height = Math.min(bounds.height, canvasHeight - y)
      context.strokeStyle = item.id === selectedID ? '#ffd166' : '#63e9a3'
      context.strokeRect(x * pixelScale + .5, y * pixelScale + .5, Math.max(2, width * pixelScale - 1), Math.max(2, height * pixelScale - 1))
    })
    context.setLineDash([])
    if (items.length !== 1) {
      context.restore()
      return
    }
    const item = items[0]
    const bounds = elementBounds(item)
    const x = clamp(bounds.x, 0, canvasWidth - 1)
    const y = clamp(bounds.y, 0, canvasHeight - 1)
    const width = Math.min(bounds.width, canvasWidth - x)
    const height = Math.min(bounds.height, canvasHeight - y)
    context.fillStyle = '#171c1a'
    const handle = (handleX, handleY) => {
      context.fillRect(handleX * pixelScale - 2, handleY * pixelScale - 2, 5, 5)
      context.strokeRect(handleX * pixelScale - 2.5, handleY * pixelScale - 2.5, 6, 6)
    }
    if (item.type === 'line') {
      handle(item.x, item.y)
      handle(item.x2, item.y2)
    } else if (item.type === 'text') {
      handle(x + width - 1, y + Math.floor((height - 1) / 2))
    } else if (item.type === 'rect' || item.type === 'graphic') {
      handle(x + width - 1, y + height - 1)
    }
    context.restore()
  }

  function canvasPoint(event) {
    const rect = preview.getBoundingClientRect()
    const { width, height } = logicalSize()
    return {
      x: clamp(Math.floor((event.clientX - rect.left) * width / rect.width), 0, width - 1),
      y: clamp(Math.floor((event.clientY - rect.top) * height / rect.height), 0, height - 1),
    }
  }

  function hitTest(point) {
    for (let index = layout.elements.length - 1; index >= 0; index -= 1) {
      const item = layout.elements[index]
      if (item.hidden) continue
      const bounds = elementBounds(item)
      const padding = item.type === 'line' ? 2 : 0
      if (point.x >= bounds.x - padding && point.x < bounds.x + bounds.width + padding &&
          point.y >= bounds.y - padding && point.y < bounds.y + bounds.height + padding) return item
    }
    return null
  }

  function selectionHandle(point) {
    if (selectedIDs.size !== 1) return null
    const item = selectedElement()
    if (!item) return null
    const near = (x, y) => Math.abs(point.x - x) <= 1 && Math.abs(point.y - y) <= 1
    if (item.type === 'line') {
      if (near(item.x2, item.y2)) return 'line-end'
      if (near(item.x, item.y)) return 'line-start'
    } else if (item.type === 'text') {
      const bounds = elementBounds(item)
      if (near(bounds.x + bounds.width - 1, bounds.y + Math.floor((bounds.height - 1) / 2))) return 'text-resize'
    } else if (item.type === 'rect' || item.type === 'graphic') {
      const bounds = elementBounds(item)
      if (near(bounds.x + bounds.width - 1, bounds.y + bounds.height - 1)) return 'box-resize'
    }
    return null
  }

  function moveElement(item, deltaX, deltaY) {
    const { width, height } = logicalSize()
    if (item.type === 'line') {
      const appliedX = clamp(deltaX, -Math.min(item.x, item.x2), width - 1 - Math.max(item.x, item.x2))
      const appliedY = clamp(deltaY, -Math.min(item.y, item.y2), height - 1 - Math.max(item.y, item.y2))
      item.x += appliedX; item.x2 += appliedX
      item.y += appliedY; item.y2 += appliedY
    } else {
      const bounds = elementBounds(item)
      item.x = clamp(item.x + deltaX, 0, width - Math.min(bounds.width, width))
      item.y = clamp(item.y + deltaY, 0, height - Math.min(bounds.height, height))
    }
  }

  function moveElements(items, deltaX, deltaY) {
    const bounds = selectionBounds(items)
    if (!bounds) return
    const { width, height } = logicalSize()
    const appliedX = clamp(deltaX, -bounds.x, width - bounds.x - bounds.width)
    const appliedY = clamp(deltaY, -bounds.y, height - bounds.y - bounds.height)
    items.forEach((item) => moveElement(item, appliedX, appliedY))
  }

  function centerSelection(axis) {
    const items = selectedElements()
    const bounds = selectionBounds(items)
    if (!bounds) return
    const size = logicalSize()
    const deltaX = axis === 'horizontal' ? Math.floor((size.width - bounds.width) / 2) - bounds.x : 0
    const deltaY = axis === 'vertical' ? Math.floor((size.height - bounds.height) / 2) - bounds.y : 0
    moveElements(items, deltaX, deltaY)
    syncEditor()
  }

  function distributeSelection(axis) {
    const items = selectedElements()
    if (items.length < 3) return
    const horizontal = axis === 'horizontal'
    const entries = items.map((item) => ({ item, bounds: elementBounds(item) }))
      .sort((left, right) => {
        const position = horizontal ? left.bounds.x - right.bounds.x : left.bounds.y - right.bounds.y
        return position || left.item.id.localeCompare(right.item.id)
      })
    const start = horizontal ? entries[0].bounds.x : entries[0].bounds.y
    const last = entries[entries.length - 1].bounds
    const end = horizontal ? last.x + last.width : last.y + last.height
    const occupied = entries.reduce((total, entry) => total + (horizontal ? entry.bounds.width : entry.bounds.height), 0)
    const gap = (end - start - occupied) / (entries.length - 1)
    let cursor = start
    entries.forEach((entry, index) => {
      const extent = horizontal ? entry.bounds.width : entry.bounds.height
      const target = index === entries.length - 1 ? end - extent : Math.round(cursor)
      if (horizontal) moveElement(entry.item, target - entry.bounds.x, 0)
      else moveElement(entry.item, 0, target - entry.bounds.y)
      cursor += extent + gap
    })
    syncEditor()
  }

  function beginDrag(event) {
    if (externalMediaTimer !== null) stopExternalAssetPreview()
    const point = canvasPoint(event)
    const additive = additiveSelection(event)
    const handle = additive ? null : selectionHandle(point)
    if (handle) {
      const item = selectedElement()
      drag = { id: item.id, point, kind: handle, snapshot: { ...item } }
      preview.setPointerCapture(event.pointerId)
      event.preventDefault()
      return
    }
    const item = hitTest(point)
    if (!item) {
      if (!additive) replaceSelection(null)
      syncEditor()
      return
    }
    if (additive) {
      selectElement(item.id, true)
      if (!selectedIDs.has(item.id)) return
    } else if (selectedIDs.size > 1 && selectedIDs.has(item.id)) {
      selectedID = item.id
    } else {
      replaceSelection(item.id)
    }
    const items = selectedElements()
    drag = { id: item.id, point, kind: 'move', snapshots: items.map((selected) => [selected.id, { ...selected }]) }
    preview.setPointerCapture(event.pointerId)
    syncEditor()
  }

  function continueDrag(event) {
    if (!drag) {
      const handle = selectionHandle(canvasPoint(event))
      preview.style.cursor = handle === 'text-resize' ? 'ew-resize' : handle === 'box-resize' ? 'nwse-resize' : handle ? 'crosshair' : 'crosshair'
      return
    }
    const point = canvasPoint(event)
    const item = selectedElement()
    if (!item || item.id !== drag.id) return
    if (drag.kind === 'move') {
      drag.snapshots.forEach(([id, snapshot]) => {
        const selected = layout.elements.find((candidate) => candidate.id === id)
        if (selected) Object.assign(selected, snapshot)
      })
      moveElements(selectedElements(), point.x - drag.point.x, point.y - drag.point.y)
    } else {
      Object.assign(item, drag.snapshot)
    }
    if (drag.kind === 'line-start') {
      item.x = point.x
      item.y = point.y
    } else if (drag.kind === 'line-end') {
      item.x2 = point.x
      item.y2 = point.y
    } else if (drag.kind === 'text-resize') {
      item.full_width = false
      item.width = clamp(point.x - item.x + 1, 1, logicalSize().width - item.x)
    } else if (drag.kind === 'box-resize') {
      const size = logicalSize()
      const wantedWidth = clamp(point.x - item.x + 1, 1, size.width - item.x)
      const wantedHeight = clamp(point.y - item.y + 1, 1, size.height - item.y)
      if (item.type === 'rect') {
        item.width = wantedWidth
        item.height = wantedHeight
      } else if (item.mode === 'custom') {
        resizeGraphic(item, wantedWidth, wantedHeight)
      } else if (item.content === 'custom') {
        const inset = item.filled || item.bordered ? 2 : 0
        resizeGraphic(item, Math.max(1, wantedWidth - inset), Math.max(1, wantedHeight - inset))
      } else {
        const natural = naturalGraphicDimensions(item)
        item.fixed_size = true
        item.width = Math.max(Math.min(natural.width, size.width - item.x), wantedWidth)
        item.height = Math.max(Math.min(natural.height, size.height - item.y), wantedHeight)
      }
      clampElement(item)
    }
    syncInspector()
    renderElementList()
    render()
  }

  function endDrag(event) {
    if (preview.hasPointerCapture(event.pointerId)) preview.releasePointerCapture(event.pointerId)
    drag = null
  }

  function updateSelected(name, value) {
    if (selectedIDs.size !== 1) return
    const item = selectedElement()
    if (!item) return
    item[name] = value
    clampElement(item)
    syncEditor()
  }

  function serializeLayout() {
    return JSON.stringify({
      version: 1,
      elements: layout.elements.map((item) => {
        const layer = { id: item.id, type: item.type }
        if (item.name) layer.name = item.name
        if (item.hidden) layer.hidden = true
        if (item.type === 'text') {
          return {
            ...layer, x: item.x, y: item.y, text: item.text, scale: item.scale, font_width: item.font_width,
            ...(item.width > 0 ? { width: item.width } : {}),
            ...((item.full_width || item.align !== 'left') ? { full_width: item.full_width, align: item.align } : {}),
          }
        }
        if (item.type === 'line') return { ...layer, x: item.x, y: item.y, x2: item.x2, y2: item.y2 }
        if (item.type === 'rect') {
          return {
            ...layer, x: item.x, y: item.y, width: item.width, height: item.height, filled: item.filled,
            text: item.text, scale: item.scale, font_width: item.font_width, align: item.align,
          }
        }
        if (item.type === 'animation') {
          return { ...layer, x: item.x, y: item.y, asset: item.asset, width: item.width, height: item.height }
        }
        if (item.mode === 'custom') {
          return { ...layer, x: item.x, y: item.y, mode: item.mode, width: item.width, height: item.height, pixels: item.pixels }
        }
        return {
          ...layer, x: item.x, y: item.y, mode: item.mode, content: item.content,
          text: item.text, scale: item.scale, font_width: item.font_width, filled: item.filled, bordered: item.bordered,
          width: item.width, height: item.height, pixels: item.pixels,
          ...(item.fixed_size ? { fixed_size: true } : {}),
        }
      }),
    })
  }

  function exportLayers() {
    const document = JSON.parse(serializeLayout())
    return {
      filename: `onekvm-oled-${deviceVariant}-layers.json`,
      content: JSON.stringify({
        format: 'onekvm-oled-layers',
        version: 1,
        screen: { width: screenWidth, height: screenHeight },
        elements: document.elements,
      }, null, 2),
    }
  }

  function validImportedID(value) {
    return typeof value === 'string' && /^[A-Za-z0-9][A-Za-z0-9_-]{0,31}$/.test(value)
  }

  function importLayers(raw) {
    let document
    try {
      document = typeof raw === 'string' ? JSON.parse(raw) : raw
    } catch (_) {
      throw new Error(text.invalidImport)
    }
    if (!document || typeof document !== 'object' ||
        (document.format !== undefined && document.format !== 'onekvm-oled-layers') ||
        document.version !== 1 || !Array.isArray(document.elements)) throw new Error(text.invalidImport)
    if (document.elements.length > maxElements) throw new Error(text.importTooMany)
    const ids = new Set()
    const imported = document.elements.map((source) => {
      if (!validImportedID(source?.id) || ids.has(source.id)) throw new Error(text.invalidImport)
      ids.add(source.id)
      const item = normalizeElement(source)
      if (!item) throw new Error(text.invalidImport)
      return item
    })
    layout = { version: 1, elements: imported }
    replaceSelection(null)
    clampAllElements()
    setError('')
    syncEditor()
    return text.imported
  }

  function collectSettings() {
    collectGlobalForm()
    return {
      display_enabled: settings.display_enabled,
      burn_in_protection: settings.burn_in_protection,
      rotation: settings.rotation,
      sleep_seconds: settings.sleep_seconds,
      layout_json: serializeLayout(),
      variables_json: serializeVariables(parseVariables(settings.variables_json)),
    }
  }

  let initialized = false
  let baselineSettings = ''

  function notifySettingsState() {
    if (!initialized) return
    const collected = collectSettings()
    const valid = new TextEncoder().encode(collected.layout_json).length <= maxLayoutBytes
    host.onSettingsState({
      dirty: JSON.stringify(collected) !== baselineSettings,
      valid,
      error: valid ? '' : text.layoutTooLarge,
    })
  }

  function resetSettings(nextSettings) {
    initialized = false
    applySettings(nextSettings || {})
    baselineSettings = JSON.stringify(collectSettings())
    initialized = true
    notifySettingsState()
  }

  function setError(message) {
    const alert = element('error')
    if (alert) {
      alert.textContent = message
      alert.classList.toggle('hidden', !message)
    }
    host.onError(message)
  }

  function asciiGlyphWidth(character, fontWidth = 4) {
    const upper = String(character || '').toUpperCase()
    return upper >= 'A' && upper <= 'Z' && [3, 4, 5].includes(fontWidth) ? fontWidth : 3
  }

  const font3 = {
    A: [2, 5, 7, 5, 5], B: [6, 5, 6, 5, 6], C: [3, 4, 4, 4, 3], D: [6, 5, 5, 5, 6],
    E: [7, 4, 6, 4, 7], F: [7, 4, 6, 4, 4], G: [3, 4, 5, 5, 3], H: [5, 5, 7, 5, 5],
    I: [7, 2, 2, 2, 7], J: [1, 1, 1, 5, 2], K: [5, 5, 6, 5, 5], L: [4, 4, 4, 4, 7],
    M: [5, 7, 7, 5, 5], N: [5, 7, 7, 7, 5], O: [2, 5, 5, 5, 2], P: [6, 5, 6, 4, 4],
    Q: [2, 5, 5, 3, 1], R: [6, 5, 6, 5, 5], S: [3, 4, 2, 1, 6], T: [7, 2, 2, 2, 2],
    U: [5, 5, 5, 5, 7], V: [5, 5, 5, 5, 2], W: [5, 5, 7, 7, 5], X: [5, 5, 2, 5, 5],
    Y: [5, 5, 2, 2, 2], Z: [7, 1, 2, 4, 7],
  }

  const font4 = {
    A: [6, 9, 15, 9, 9], B: [14, 9, 14, 9, 14], C: [7, 8, 8, 8, 7],
    D: [14, 9, 9, 9, 14], E: [15, 8, 14, 8, 15], F: [15, 8, 14, 8, 8], G: [7, 8, 11, 9, 7],
    H: [9, 9, 15, 9, 9], I: [15, 2, 2, 2, 15], J: [1, 1, 1, 9, 6], K: [9, 10, 12, 10, 9],
    L: [8, 8, 8, 8, 15], M: [9, 15, 15, 9, 9], N: [9, 13, 11, 9, 9], O: [6, 9, 9, 9, 6],
    P: [14, 9, 14, 8, 8], Q: [6, 9, 9, 11, 7], R: [14, 9, 14, 10, 9], S: [7, 8, 6, 1, 14],
    T: [15, 2, 2, 2, 2], U: [9, 9, 9, 9, 6], V: [9, 9, 9, 6, 6], W: [9, 9, 15, 15, 9],
    X: [9, 9, 6, 9, 9], Y: [9, 9, 6, 2, 2], Z: [15, 1, 2, 4, 15],
  }

  const font5 = {
    A: [14, 17, 31, 17, 17], B: [30, 17, 30, 17, 30], C: [15, 16, 16, 16, 15],
    D: [30, 17, 17, 17, 30], E: [31, 16, 30, 16, 31], F: [31, 16, 30, 16, 16], G: [15, 16, 23, 17, 15],
    H: [17, 17, 31, 17, 17], I: [31, 4, 4, 4, 31], J: [1, 1, 1, 17, 14], K: [17, 18, 28, 18, 17],
    L: [16, 16, 16, 16, 31], M: [17, 27, 21, 17, 17], N: [17, 25, 21, 19, 17], O: [14, 17, 17, 17, 14],
    P: [30, 17, 30, 16, 16], Q: [14, 17, 21, 19, 15], R: [30, 17, 30, 18, 17], S: [15, 16, 14, 1, 30],
    T: [31, 4, 4, 4, 4], U: [17, 17, 17, 17, 14], V: [17, 17, 17, 10, 4], W: [17, 17, 21, 27, 17],
    X: [17, 10, 4, 10, 17], Y: [17, 10, 4, 4, 4], Z: [31, 2, 4, 8, 31],
  }

  const commonFont = {
    ' ': [0, 0, 0, 0, 0],
    0: [7, 5, 5, 5, 7], 1: [2, 6, 2, 2, 7], 2: [6, 1, 7, 4, 7], 3: [6, 1, 3, 1, 6],
    4: [5, 5, 7, 1, 1], 5: [7, 4, 6, 1, 6], 6: [3, 4, 7, 5, 7], 7: [7, 1, 2, 2, 2],
    8: [7, 5, 7, 5, 7], 9: [7, 5, 7, 1, 6], '.': [0, 0, 0, 0, 2], ':': [0, 2, 0, 2, 0],
    '-': [0, 0, 7, 0, 0], '+': [0, 2, 7, 2, 0], '/': [1, 1, 2, 4, 4], '?': [6, 1, 2, 0, 2],
  }
  const latinFonts = { 3: font3, 4: font4, 5: font5 }

  function bindNumber(id, property, minimum, maximum) {
    element(id).addEventListener('input', (event) => {
      const upper = typeof maximum === 'function' ? maximum() : maximum
      const value = clamp(integer(event.target.value, minimum), minimum, upper)
      updateSelected(property, value)
    })
  }

  function bitmapPoint(event, item) {
    const canvas = element('bitmap-editor')
    const rect = canvas.getBoundingClientRect()
    return {
      x: clamp(Math.floor((event.clientX - rect.left) * item.width / rect.width), 0, item.width - 1),
      y: clamp(Math.floor((event.clientY - rect.top) * item.height / rect.height), 0, item.height - 1),
    }
  }

  function refreshGraphicEditor(item) {
    renderBitmapEditor(item)
    refreshLayerThumbnails()
    render()
  }

  function paintBitmap(event) {
    if (!bitmapDrag) return
    const item = selectedElement()
    if (!item || item.id !== bitmapDrag.id || item.type !== 'graphic' || (item.mode !== 'custom' && item.content !== 'custom')) return
    const point = bitmapPoint(event, item)
    const key = `${point.x},${point.y}`
    if (key === bitmapDrag.last) return
    bitmapDrag.last = key
    setGraphicPixel(item, point.x, point.y, bitmapDrag.enabled)
    refreshGraphicEditor(item)
  }

  function beginBitmap(event) {
    const item = selectedElement()
    if (!item || item.type !== 'graphic' || (item.mode !== 'custom' && item.content !== 'custom')) return
    event.preventDefault()
    const point = bitmapPoint(event, item)
    bitmapDrag = { id: item.id, enabled: !graphicPixel(item, point.x, point.y), last: '' }
    element('bitmap-editor').setPointerCapture(event.pointerId)
    paintBitmap(event)
  }

  function endBitmap(event) {
    const canvas = element('bitmap-editor')
    if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId)
    bitmapDrag = null
  }

  function setGraphicSize(property, value) {
    const item = selectedElement()
    if (!item || item.type !== 'graphic' || (item.mode !== 'custom' && item.content !== 'custom')) return
    const size = logicalSize()
    const width = property === 'width' ? clamp(value, 1, size.width) : item.width
    const height = property === 'height' ? clamp(value, 1, size.height) : item.height
    resizeGraphic(item, width, height)
    clampElement(item)
    syncEditor()
  }

  function setGraphicMode(value) {
    const item = selectedElement()
    if (!item || item.type !== 'graphic') return
    const previousMode = item.mode
    item.mode = graphicModes.includes(value) ? value : 'custom'
    const defaultLabels = graphicModes.filter((mode) => mode !== 'custom').map(statusDefaultText)
    if (item.mode !== 'custom' && item.content === 'text' && (defaultLabels.includes(item.text) || !item.text)) {
      item.text = statusDefaultText(item.mode)
    }
    if (previousMode === 'custom' && item.mode !== 'custom' && !graphicContents.includes(item.content)) item.content = 'text'
    clampElement(item)
    syncEditor()
  }

  function transformGraphicPixels(transform) {
    const item = selectedElement()
    if (!item || item.type !== 'graphic' || (item.mode !== 'custom' && item.content !== 'custom')) return
    for (let y = 0; y < item.height; y += 1) {
      for (let x = 0; x < item.width; x += 1) {
        setGraphicPixel(item, x, y, transform(graphicPixel(item, x, y)))
      }
    }
    refreshGraphicEditor(item)
  }

  function closeBitmapImporter() {
    element('bitmap-import-panel').classList.add('hidden')
    element('bitmap-import-file').value = ''
    bitmapImport = null
    if (bitmapImportObjectURL) URL.revokeObjectURL(bitmapImportObjectURL)
    bitmapImportObjectURL = ''
  }

  function bitmapImportPoint(event) {
    const canvas = element('bitmap-import-source')
    const rect = canvas.getBoundingClientRect()
    return {
      x: clamp(Math.floor((event.clientX - rect.left) * bitmapImport.image.naturalWidth / rect.width), 0, bitmapImport.image.naturalWidth - 1),
      y: clamp(Math.floor((event.clientY - rect.top) * bitmapImport.image.naturalHeight / rect.height), 0, bitmapImport.image.naturalHeight - 1),
    }
  }

  function importedBitmap() {
    if (!bitmapImport) return { width: 1, height: 1, pixels: normalizePixels('', 1, 1) }
    const width = bitmapImport.outputWidth
    const height = bitmapImport.outputHeight
    const frame = document.createElement('canvas')
    frame.width = width
    frame.height = height
    const context = frame.getContext('2d', { willReadFrequently: true })
    context.fillStyle = '#000'
    context.fillRect(0, 0, width, height)
    context.imageSmoothingEnabled = true
    const crop = bitmapImport.crop
    context.drawImage(bitmapImport.image, crop.x, crop.y, crop.width, crop.height, 0, 0, width, height)
    const rgba = context.getImageData(0, 0, width, height).data
    const output = { width, height, pixels: normalizePixels('', width, height) }
    for (let y = 0; y < height; y += 1) for (let x = 0; x < width; x += 1) {
      const pixel = (y * width + x) * 4
      const luminance = rgba[pixel] * 0.2126 + rgba[pixel + 1] * 0.7152 + rgba[pixel + 2] * 0.0722
      if (rgba[pixel + 3] >= 128 && luminance >= 128) setGraphicPixel(output, x, y, true)
    }
    return output
  }

  function trimBlankBitmap(output) {
    let left = output.width
    let top = output.height
    let right = -1
    let bottom = -1
    for (let y = 0; y < output.height; y += 1) for (let x = 0; x < output.width; x += 1) {
      if (!graphicPixel(output, x, y)) continue
      left = Math.min(left, x)
      top = Math.min(top, y)
      right = Math.max(right, x)
      bottom = Math.max(bottom, y)
    }
    if (right < left || bottom < top) return output
    const trimmed = { width: right - left + 1, height: bottom - top + 1, pixels: '' }
    trimmed.pixels = normalizePixels('', trimmed.width, trimmed.height)
    for (let y = 0; y < trimmed.height; y += 1) for (let x = 0; x < trimmed.width; x += 1) {
      if (graphicPixel(output, left + x, top + y)) setGraphicPixel(trimmed, x, y, true)
    }
    return trimmed
  }

  function processedBitmapImport() {
    const output = importedBitmap()
    return bitmapImport?.trimBlank ? trimBlankBitmap(output) : output
  }

  function renderBitmapImport() {
    if (!bitmapImport) return
    const source = element('bitmap-import-source')
    const maximumWidth = 520
    const maximumHeight = 280
    const scale = Math.min(1, maximumWidth / bitmapImport.image.naturalWidth, maximumHeight / bitmapImport.image.naturalHeight)
    source.width = Math.max(1, Math.round(bitmapImport.image.naturalWidth * scale))
    source.height = Math.max(1, Math.round(bitmapImport.image.naturalHeight * scale))
    source.style.aspectRatio = `${bitmapImport.image.naturalWidth} / ${bitmapImport.image.naturalHeight}`
    const context = source.getContext('2d')
    context.drawImage(bitmapImport.image, 0, 0, source.width, source.height)
    context.fillStyle = 'rgba(0, 0, 0, .58)'
    context.fillRect(0, 0, source.width, source.height)
    const crop = bitmapImport.crop
    const cropX = crop.x * source.width / bitmapImport.image.naturalWidth
    const cropY = crop.y * source.height / bitmapImport.image.naturalHeight
    const cropWidth = crop.width * source.width / bitmapImport.image.naturalWidth
    const cropHeight = crop.height * source.height / bitmapImport.image.naturalHeight
    context.save()
    context.beginPath()
    context.rect(cropX, cropY, cropWidth, cropHeight)
    context.clip()
    context.drawImage(bitmapImport.image, 0, 0, source.width, source.height)
    context.restore()
    context.strokeStyle = '#f4f7f6'
    context.lineWidth = 2
    context.strokeRect(cropX + 1, cropY + 1, Math.max(1, cropWidth - 2), Math.max(1, cropHeight - 2))
    source.classList.toggle('fixed-crop', bitmapImport.fixedCrop)
    element('bitmap-import-crop-width').value = String(crop.width)
    element('bitmap-import-crop-height').value = String(crop.height)

    const rendered = processedBitmapImport()
    bitmapImport.outputPixels = rendered.pixels
    bitmapImport.renderedWidth = rendered.width
    bitmapImport.renderedHeight = rendered.height
    const output = element('bitmap-import-output')
    const outputScale = Math.max(3, Math.min(12, Math.floor(240 / Math.max(rendered.width, rendered.height))))
    output.width = rendered.width * outputScale
    output.height = rendered.height * outputScale
    output.style.aspectRatio = `${rendered.width} / ${rendered.height}`
    element('bitmap-import-output-size').textContent = `${rendered.width}×${rendered.height}`
    const outputContext = output.getContext('2d')
    outputContext.fillStyle = '#080d0b'
    outputContext.fillRect(0, 0, output.width, output.height)
    const pixels = rendered
    outputContext.fillStyle = '#f4f7f6'
    for (let y = 0; y < pixels.height; y += 1) for (let x = 0; x < pixels.width; x += 1) {
      if (graphicPixel(pixels, x, y)) outputContext.fillRect(x * outputScale, y * outputScale, outputScale, outputScale)
    }
  }

  function loadBitmapImage(event) {
    const file = event.target.files?.[0]
    const item = selectedElement()
    if (!file || !String(file.type || '').startsWith('image/') || !item || item.type !== 'graphic' || (item.mode !== 'custom' && item.content !== 'custom')) return
    if (bitmapImportObjectURL) URL.revokeObjectURL(bitmapImportObjectURL)
    bitmapImportObjectURL = URL.createObjectURL(file)
    const image = new Image()
    image.onload = () => {
      const size = logicalSize()
      const outputWidth = clamp(item.width, 1, size.width)
      const outputHeight = clamp(item.height, 1, size.height)
      const targetRatio = outputWidth / outputHeight
      let cropWidth = image.naturalWidth
      let cropHeight = Math.round(cropWidth / targetRatio)
      if (cropHeight > image.naturalHeight) {
        cropHeight = image.naturalHeight
        cropWidth = Math.round(cropHeight * targetRatio)
      }
      bitmapImport = {
        targetID: item.id,
        image,
        crop: { x: Math.floor((image.naturalWidth - cropWidth) / 2), y: Math.floor((image.naturalHeight - cropHeight) / 2), width: cropWidth, height: cropHeight },
        outputWidth,
        outputHeight,
        outputPixels: '',
        renderedWidth: outputWidth,
        renderedHeight: outputHeight,
        fixedCrop: false,
        trimBlank: false,
        drag: null,
      }
      element('bitmap-import-crop-width').max = String(image.naturalWidth)
      element('bitmap-import-crop-height').max = String(image.naturalHeight)
      element('bitmap-import-crop-width').value = String(cropWidth)
      element('bitmap-import-crop-height').value = String(cropHeight)
      element('bitmap-import-crop-width').disabled = true
      element('bitmap-import-crop-height').disabled = true
      element('bitmap-import-fixed-crop').checked = false
      element('bitmap-import-trim-blank').checked = false
      element('bitmap-import-width').max = String(size.width)
      element('bitmap-import-height').max = String(size.height)
      element('bitmap-import-width').value = String(outputWidth)
      element('bitmap-import-height').value = String(outputHeight)
      element('bitmap-import-panel').classList.remove('hidden')
      renderBitmapImport()
    }
    image.onerror = closeBitmapImporter
    image.src = bitmapImportObjectURL
  }

  function beginBitmapCrop(event) {
    if (!bitmapImport) return
    event.preventDefault()
    const point = bitmapImportPoint(event)
    if (bitmapImport.fixedCrop) {
      const crop = bitmapImport.crop
      const inside = point.x >= crop.x && point.x < crop.x + crop.width && point.y >= crop.y && point.y < crop.y + crop.height
      const offsetX = inside ? point.x - crop.x : Math.floor(crop.width / 2)
      const offsetY = inside ? point.y - crop.y : Math.floor(crop.height / 2)
      crop.x = clamp(point.x - offsetX, 0, bitmapImport.image.naturalWidth - crop.width)
      crop.y = clamp(point.y - offsetY, 0, bitmapImport.image.naturalHeight - crop.height)
      bitmapImport.drag = { mode: 'move', offsetX, offsetY }
    } else {
      bitmapImport.drag = { mode: 'select', start: point }
      bitmapImport.crop = { x: point.x, y: point.y, width: 1, height: 1 }
    }
    element('bitmap-import-source').setPointerCapture(event.pointerId)
    renderBitmapImport()
  }

  function moveBitmapCrop(event) {
    if (!bitmapImport?.drag) return
    const point = bitmapImportPoint(event)
    if (bitmapImport.drag.mode === 'move') {
      const crop = bitmapImport.crop
      crop.x = clamp(point.x - bitmapImport.drag.offsetX, 0, bitmapImport.image.naturalWidth - crop.width)
      crop.y = clamp(point.y - bitmapImport.drag.offsetY, 0, bitmapImport.image.naturalHeight - crop.height)
    } else {
      const start = bitmapImport.drag.start
      bitmapImport.crop = {
        x: Math.min(start.x, point.x),
        y: Math.min(start.y, point.y),
        width: Math.abs(point.x - start.x) + 1,
        height: Math.abs(point.y - start.y) + 1,
      }
    }
    renderBitmapImport()
  }

  function endBitmapCrop(event) {
    const source = element('bitmap-import-source')
    if (source.hasPointerCapture(event.pointerId)) source.releasePointerCapture(event.pointerId)
    if (bitmapImport) bitmapImport.drag = null
  }

  function setBitmapCropFixed(enabled) {
    if (!bitmapImport) return
    bitmapImport.fixedCrop = enabled
    element('bitmap-import-crop-width').disabled = !enabled
    element('bitmap-import-crop-height').disabled = !enabled
    renderBitmapImport()
  }

  function setBitmapCropSize(property, value) {
    if (!bitmapImport) return
    if (String(value).trim() === '') return
    const crop = bitmapImport.crop
    const maximum = property === 'width' ? bitmapImport.image.naturalWidth : bitmapImport.image.naturalHeight
    const next = clamp(integer(value, 1), 1, maximum)
    const center = property === 'width' ? crop.x + crop.width / 2 : crop.y + crop.height / 2
    crop[property] = next
    if (property === 'width') crop.x = clamp(Math.round(center - next / 2), 0, bitmapImport.image.naturalWidth - next)
    else crop.y = clamp(Math.round(center - next / 2), 0, bitmapImport.image.naturalHeight - next)
    renderBitmapImport()
  }

  function setBitmapTrimBlank(enabled) {
    if (!bitmapImport) return
    bitmapImport.trimBlank = enabled
    renderBitmapImport()
  }

  function setBitmapImportSize(property, value) {
    if (!bitmapImport) return
    const size = logicalSize()
    bitmapImport[property] = clamp(integer(value, 1), 1, property === 'outputWidth' ? size.width : size.height)
    renderBitmapImport()
  }

  function applyBitmapImport() {
    if (!bitmapImport) return
    const item = layout.elements.find((candidate) => candidate.id === bitmapImport.targetID)
    if (!item || item.type !== 'graphic' || (item.mode !== 'custom' && item.content !== 'custom')) {
      closeBitmapImporter()
      return
    }
    const output = bitmapImport.outputPixels ? {
      width: bitmapImport.renderedWidth,
      height: bitmapImport.renderedHeight,
      pixels: bitmapImport.outputPixels,
    } : processedBitmapImport()
    item.width = output.width
    item.height = output.height
    item.pixels = output.pixels
    closeBitmapImporter()
    clampElement(item)
    syncEditor()
  }

  function bind() {
    element('display-enabled').addEventListener('change', () => { collectGlobalForm(); render() })
    element('sleep').addEventListener('change', () => { collectGlobalForm(); notifySettingsState() })
    element('burn-in').addEventListener('change', () => { collectGlobalForm(); notifySettingsState() })
    element('variable-add')?.addEventListener('click', () => {
      const next = parseVariables(settings.variables_json)
      let index = 1
      let key = 'c_var'
      while (next[key]) {
        index += 1
        key = `c_var${index}`
      }
      next[key] = ''
      settings.variables_json = serializeVariables(next)
      syncVariablesForm()
      notifySettingsState()
    })
    element('delete-element').addEventListener('click', deleteSelected)
    element('copy-element').addEventListener('click', copySelected)
    element('paste-element').addEventListener('click', pasteCopied)
    element('center-horizontal').addEventListener('click', () => centerSelection('horizontal'))
    element('center-vertical').addEventListener('click', () => centerSelection('vertical'))
    element('distribute-horizontal').addEventListener('click', () => distributeSelection('horizontal'))
    element('distribute-vertical').addEventListener('click', () => distributeSelection('vertical'))
    element('move-down').addEventListener('click', () => reorderSelected(-1))
    element('move-up').addEventListener('click', () => reorderSelected(1))
    bindNumber('toolbar-x', 'x', 0, () => logicalSize().width - 1)
    bindNumber('toolbar-y', 'y', 0, () => logicalSize().height - 1)
    bindNumber('element-x2', 'x2', 0, () => logicalSize().width - 1)
    bindNumber('element-y2', 'y2', 0, () => logicalSize().height - 1)
    bindNumber('element-width', 'width', 1, () => logicalSize().width)
    bindNumber('element-height', 'height', 1, () => logicalSize().height)
    element('toolbar-name').addEventListener('input', (event) => updateSelected('name', [...event.target.value].slice(0, 64).join('')))
    element('element-content').addEventListener('input', (event) => updateSelected('text', [...event.target.value].slice(0, 128).join('')))
    element('element-scale').addEventListener('change', (event) => updateSelected('scale', Number(event.target.value) === 2 ? 2 : 1))
    element('element-font-width').addEventListener('change', (event) => {
      const width = Number(event.target.value)
      updateSelected('font_width', [3, 4, 5].includes(width) ? width : 4)
    })
    element('element-align').addEventListener('change', (event) => updateSelected('align', textAlignments.includes(event.target.value) ? event.target.value : 'left'))
    element('element-filled').addEventListener('change', (event) => updateSelected('filled', event.target.checked))
    element('element-text-width').addEventListener('input', (event) => updateSelected('width', clamp(integer(event.target.value, 1), 1, logicalSize().width)))
    element('element-full-width').addEventListener('change', (event) => updateSelected('full_width', event.target.checked))
    element('element-graphic-mode').addEventListener('change', (event) => setGraphicMode(event.target.value))
    element('element-graphic-content').addEventListener('change', (event) => updateSelected('content', graphicContents.includes(event.target.value) ? event.target.value : 'text'))
    element('element-graphic-filled').addEventListener('change', (event) => updateSelected('filled', event.target.checked))
    element('element-graphic-bordered').addEventListener('change', (event) => updateSelected('bordered', event.target.checked))
    element('element-graphic-width').addEventListener('input', (event) => setGraphicSize('width', clamp(integer(event.target.value, 1), 1, logicalSize().width)))
    element('element-graphic-height').addEventListener('input', (event) => setGraphicSize('height', clamp(integer(event.target.value, 1), 1, logicalSize().height)))
    element('element-animation-asset').addEventListener('change', async (event) => {
      const item = selectedElement()
      if (!item || item.type !== 'animation') return
      item.asset = event.target.value
      const metadata = animationAssets.find((asset) => asset.id === item.asset)
      if (metadata) { item.width = metadata.width; item.height = metadata.height; clampElement(item) }
      ensureAnimationTimer()
      await loadAnimationData(item.asset)
      updateAnimationFrames()
      syncEditor()
    })
    element('element-animation-width').addEventListener('input', (event) => updateSelected('width', clamp(integer(event.target.value, 1), 1, logicalSize().width)))
    element('element-animation-height').addEventListener('input', (event) => updateSelected('height', clamp(integer(event.target.value, 1), 1, logicalSize().height)))
    element('bitmap-clear').addEventListener('click', () => transformGraphicPixels(() => false))
    element('bitmap-fill').addEventListener('click', () => transformGraphicPixels(() => true))
    element('bitmap-invert').addEventListener('click', () => transformGraphicPixels((enabled) => !enabled))
    element('bitmap-import-open').addEventListener('click', () => element('bitmap-import-file').click())
    element('bitmap-import-file').addEventListener('change', loadBitmapImage)
    element('bitmap-import-width').addEventListener('input', (event) => setBitmapImportSize('outputWidth', event.target.value))
    element('bitmap-import-height').addEventListener('input', (event) => setBitmapImportSize('outputHeight', event.target.value))
    element('bitmap-import-fixed-crop').addEventListener('change', (event) => setBitmapCropFixed(event.target.checked))
    element('bitmap-import-crop-width').addEventListener('input', (event) => setBitmapCropSize('width', event.target.value))
    element('bitmap-import-crop-height').addEventListener('input', (event) => setBitmapCropSize('height', event.target.value))
    element('bitmap-import-crop-width').addEventListener('change', (event) => event.target.value.trim() === '' ? renderBitmapImport() : setBitmapCropSize('width', event.target.value))
    element('bitmap-import-crop-height').addEventListener('change', (event) => event.target.value.trim() === '' ? renderBitmapImport() : setBitmapCropSize('height', event.target.value))
    element('bitmap-import-trim-blank').addEventListener('change', (event) => setBitmapTrimBlank(event.target.checked))
    element('bitmap-import-cancel').addEventListener('click', closeBitmapImporter)
    element('bitmap-import-apply').addEventListener('click', applyBitmapImport)
    element('bitmap-import-source').addEventListener('pointerdown', beginBitmapCrop)
    element('bitmap-import-source').addEventListener('pointermove', moveBitmapCrop)
    element('bitmap-import-source').addEventListener('pointerup', endBitmapCrop)
    element('bitmap-import-source').addEventListener('pointercancel', endBitmapCrop)
    element('bitmap-editor').addEventListener('pointerdown', beginBitmap)
    element('bitmap-editor').addEventListener('pointermove', paintBitmap)
    element('bitmap-editor').addEventListener('pointerup', endBitmap)
    element('bitmap-editor').addEventListener('pointercancel', endBitmap)
    root.querySelectorAll('[data-token]').forEach((button) => {
      button.addEventListener('click', () => {
        const item = selectedElement()
        if (!item || item.type !== 'text') return
        item.text = `${item.text}${button.dataset.token}`.slice(0, 128)
        syncEditor()
        element('element-content').focus()
      })
    })
    preview.addEventListener('pointerdown', beginDrag)
    preview.addEventListener('pointermove', continueDrag)
    preview.addEventListener('pointerup', endDrag)
    preview.addEventListener('pointercancel', endDrag)
    preview.addEventListener('dblclick', editCanvasElement)
    root.addEventListener('keydown', handleShortcut)
    preview.addEventListener('keydown', handleCanvasKey)
  }

  function handleShortcut(event) {
      if (!(event.ctrlKey || event.metaKey) || event.altKey || editableTarget(event.target)) return
      if (event.key.toLowerCase() === 'c') {
        event.preventDefault()
        copySelected()
      } else if (event.key.toLowerCase() === 'v') {
        event.preventDefault()
        pasteCopied()
      } else if (event.key.toLowerCase() === 'a') {
        event.preventDefault()
        selectedIDs = new Set(layout.elements.map((item) => item.id))
        selectedID = layout.elements[layout.elements.length - 1]?.id || null
        syncEditor()
      }
  }

  function handleCanvasKey(event) {
      if (event.key === 'Delete' || event.key === 'Backspace') {
        event.preventDefault(); deleteSelected(); return
      }
      const directions = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }
      if (!directions[event.key]) return
      event.preventDefault()
      const items = selectedElements()
      if (!items.length) return
      const multiplier = event.shiftKey ? 5 : 1
      moveElements(items, directions[event.key][0] * multiplier, directions[event.key][1] * multiplier)
      syncEditor()
  }

  translate()
  bind()
  window.addEventListener('onekvm-oled-assets-changed', refreshAnimationAssets)
  void refreshAnimationAssets()
  if (element('media-card')) bindMedia()
  Promise.all([Promise.resolve().then(loadChineseFont), load()])
    .then(() => syncEditor())
    .catch((error) => setError(error.message || String(error)))
  const statusInterval = setInterval(refreshStatus, 2000)
  const liveInterval = setInterval(() => {
    const blinking = Boolean(status?.video?.active) && layout.elements.some((item) => !item.hidden && item.type === 'graphic' && item.mode === 'live')
    liveIndicatorOn = blinking ? !liveIndicatorOn : true
    if (blinking) {
      refreshLayerThumbnails()
      render()
    }
  }, 1000)

  return {
    collectSettings: () => {
      if (layout.elements.some((item) => item.type === 'animation' && !item.asset)) throw new Error(text.selectAnimationAsset)
      const collected = collectSettings()
      if (new TextEncoder().encode(collected.layout_json).length > maxLayoutBytes) throw new Error(text.layoutTooLarge)
      return collected
    },
    addElement,
    selectLayer: (id, additive = false) => selectElement(id, additive),
    toggleLayer: (id) => {
      const item = layout.elements.find((candidate) => candidate.id === id)
      if (!item) return
      item.hidden = !item.hidden
      syncEditor()
    },
    reorderLayer: reorderLayerByDrop,
    updateToolbar,
    runToolbarAction,
    exportLayers,
    importLayers,
    restoreDefaultCanvas,
    setRotation,
    setLocale,
    resetSettings,
    dispose: () => {
      clearInterval(statusInterval)
      clearInterval(liveInterval)
      if (animationTimer !== null) clearInterval(animationTimer)
      animationTimer = null
      if (externalMediaTimer !== null) clearInterval(externalMediaTimer)
      externalMediaTimer = null
      root.removeEventListener('keydown', handleShortcut)
      window.removeEventListener('onekvm-oled-assets-changed', refreshAnimationAssets)
      preview.removeEventListener('keydown', handleCanvasKey)
      if (mediaObjectURL) URL.revokeObjectURL(mediaObjectURL)
      if (bitmapImportObjectURL) URL.revokeObjectURL(bitmapImportObjectURL)
      mediaPreviewURLs.forEach((url) => URL.revokeObjectURL(url))
    },
    previewAsset,
    stopAssetPreview: stopExternalAssetPreview,
  }
}
