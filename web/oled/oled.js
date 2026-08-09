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
  let layerDragID = null
  let cjkFont = null
  let liveIndicatorOn = true
  let copiedElements = []

  const element = (id) => root.querySelector(`#${CSS.escape(id)}`)
  const preview = element('preview')

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
    syncGlobalForm()
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
    if (!item || !['text', 'line', 'rect', 'graphic'].includes(item.type) || typeof item.id !== 'string') return null
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
    list.replaceChildren()
    ;[...layout.elements].reverse().forEach((item) => {
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
    element('text-properties').classList.toggle('hidden', item.type !== 'text')
    element('line-properties').classList.toggle('hidden', item.type !== 'line')
    element('rect-properties').classList.toggle('hidden', item.type !== 'rect')
    element('graphic-properties').classList.toggle('hidden', item.type !== 'graphic')
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
    toolbar.classList.toggle('hidden', selectedCount === 0)
    if (selectedCount === 0) return
    const multiple = selectedCount > 1
    element('toolbar-name-control').classList.toggle('hidden', multiple)
    element('toolbar-coordinates').classList.toggle('hidden', multiple)
    element('toolbar-content-control').classList.toggle('hidden', multiple)
    element('toolbar-scale-control').classList.toggle('hidden', multiple)
    element('toolbar-font-width-control').classList.toggle('hidden', multiple)
    element('toolbar-align-control').classList.toggle('hidden', multiple)
    element('toolbar-filled-control').classList.toggle('hidden', multiple)
    element('toolbar-mode-control').classList.toggle('hidden', multiple)
    if (multiple) {
      element('toolbar-kind').textContent = text.selectedCount.replace('{count}', String(selectedCount))
      return
    }
    const editableText = item.type === 'text' || item.type === 'rect' || (item.type === 'graphic' && item.mode !== 'custom' && item.content === 'text')
    const scalableText = editableText && !(item.type === 'graphic' && item.mode === 'live')
    element('toolbar-kind').textContent = text[item.type]
    element('toolbar-name').value = item.name || ''
    element('toolbar-name').placeholder = layerDisplayName(item)
    element('toolbar-x').value = String(item.x)
    element('toolbar-y').value = String(item.y)
    element('toolbar-x').disabled = item.type === 'text' && item.full_width
    element('toolbar-content-control').classList.toggle('hidden', !editableText)
    element('toolbar-scale-control').classList.toggle('hidden', !scalableText)
    element('toolbar-font-width-control').classList.toggle('hidden', !editableText)
    element('toolbar-align-control').classList.toggle('hidden', item.type !== 'text' && item.type !== 'rect')
    element('toolbar-filled-control').classList.toggle('hidden', item.type !== 'rect')
    element('toolbar-mode-control').classList.toggle('hidden', item.type !== 'graphic')
    if (editableText) {
      element('toolbar-content').value = item.text
      element('toolbar-scale').value = String(item.scale)
      element('toolbar-font-width').value = String(item.font_width)
    }
    if (item.type === 'text' || item.type === 'rect') element('toolbar-align').value = item.align
    if (item.type === 'rect') element('toolbar-filled').checked = item.filled
    if (item.type === 'graphic') element('toolbar-mode').value = item.mode
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
    const targetID = fieldID || (editableText ? 'toolbar-content' : 'toolbar-name')
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
    } else if (item.type === 'graphic' && (item.mode === 'custom' || item.content === 'custom')) {
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
    }
    return String(value || '').replace(/\{(ip|hostname|codec|fps|resolution|stream|uptime)\}/g, (_, key) => values[key])
  }

  function compactDuration(seconds) {
    const minutes = Math.floor(seconds / 60)
    if (minutes >= 60) return `${Math.floor(minutes / 60)}H${String(minutes % 60).padStart(2, '0')}M`
    return `${minutes}M`
  }

  function render() {
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

  function bind() {
    element('display-enabled').addEventListener('change', () => { collectGlobalForm(); render() })
    element('sleep').addEventListener('change', () => { collectGlobalForm(); notifySettingsState() })
    element('burn-in').addEventListener('change', () => { collectGlobalForm(); notifySettingsState() })
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
    element('toolbar-content').addEventListener('input', (event) => updateSelected('text', [...event.target.value].slice(0, 128).join('')))
    element('toolbar-scale').addEventListener('change', (event) => updateSelected('scale', Number(event.target.value) === 2 ? 2 : 1))
    element('toolbar-font-width').addEventListener('change', (event) => {
      const width = Number(event.target.value)
      updateSelected('font_width', [3, 4, 5].includes(width) ? width : 4)
    })
    element('toolbar-align').addEventListener('change', (event) => updateSelected('align', textAlignments.includes(event.target.value) ? event.target.value : 'left'))
    element('toolbar-filled').addEventListener('change', (event) => updateSelected('filled', event.target.checked))
    element('element-text-width').addEventListener('input', (event) => updateSelected('width', clamp(integer(event.target.value, 1), 1, logicalSize().width)))
    element('element-full-width').addEventListener('change', (event) => updateSelected('full_width', event.target.checked))
    element('toolbar-mode').addEventListener('change', (event) => setGraphicMode(event.target.value))
    element('element-graphic-content').addEventListener('change', (event) => updateSelected('content', graphicContents.includes(event.target.value) ? event.target.value : 'text'))
    element('element-graphic-filled').addEventListener('change', (event) => updateSelected('filled', event.target.checked))
    element('element-graphic-bordered').addEventListener('change', (event) => updateSelected('bordered', event.target.checked))
    element('element-graphic-width').addEventListener('input', (event) => setGraphicSize('width', clamp(integer(event.target.value, 1), 1, logicalSize().width)))
    element('element-graphic-height').addEventListener('input', (event) => setGraphicSize('height', clamp(integer(event.target.value, 1), 1, logicalSize().height)))
    element('bitmap-clear').addEventListener('click', () => transformGraphicPixels(() => false))
    element('bitmap-fill').addEventListener('click', () => transformGraphicPixels(() => true))
    element('bitmap-invert').addEventListener('click', () => transformGraphicPixels((enabled) => !enabled))
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
        element('toolbar-content').focus()
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
      const collected = collectSettings()
      if (new TextEncoder().encode(collected.layout_json).length > maxLayoutBytes) throw new Error(text.layoutTooLarge)
      return collected
    },
    addElement,
    exportLayers,
    importLayers,
    restoreDefaultCanvas,
    setRotation,
    setLocale,
    resetSettings,
    dispose: () => {
      clearInterval(statusInterval)
      clearInterval(liveInterval)
      root.removeEventListener('keydown', handleShortcut)
      preview.removeEventListener('keydown', handleCanvasKey)
    },
  }
}
