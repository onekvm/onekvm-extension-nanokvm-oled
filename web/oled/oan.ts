export const OAN_FRAME = 1
export const OAN_SLEEP = 2
export const OAN_INVERT_X = 3
export const OAN_INVERT_Y = 4
export const OAN_LOOP = 5
export const OAN_MAX_BYTES = 32 * 1024 * 1024
export const OAN_MAX_EVENTS = 65536
export const OAN_NO_LOOP = 0xffffffff

export interface OanStep {
  delayMs: number
  offset: number
}

export interface ParsedOan {
  version2: boolean
  width: number
  height: number
  fps: number
  frames: number
  durationMs: number
  frameBytes: number
  loopIndex: number
  steps: OanStep[]
  pixels: Uint8Array
}

function u16(view: DataView, at: number) {
  return view.getUint16(at, true)
}

function u32(view: DataView, at: number) {
  return view.getUint32(at, true)
}

function dimensionsOk(width: number, height: number) {
  return width >= 8 && width <= 256 && height >= 8 && height <= 256 && height % 8 === 0
}

function invertX(buf: Uint8Array, width: number, height: number, x: number) {
  if (x < 0 || x >= width) return
  const pages = height / 8
  for (let page = 0; page < pages; page += 1) buf[page * width + x] ^= 0xff
}

function invertY(buf: Uint8Array, width: number, height: number, y: number) {
  if (y < 0 || y >= height) return
  const bit = 1 << (y & 7)
  const page = y >> 3
  for (let x = 0; x < width; x += 1) buf[page * width + x] ^= bit
}

export function parseOan(data: Uint8Array): ParsedOan {
  if (data.byteLength < 14 || data.byteLength > OAN_MAX_BYTES) throw new Error('invalid OAN')
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  if (view.getUint8(0) !== 0x4f || view.getUint8(1) !== 0x41 || view.getUint8(2) !== 0x4e) throw new Error('invalid OAN')
  const version2 = view.getUint8(3) === 0x32
  if (!version2 && view.getUint8(3) !== 0x31) throw new Error('invalid OAN')
  const width = u16(view, 4)
  const height = u16(view, 6)
  const fps = u16(view, 8)
  const events = u32(view, 10)
  if (!dimensionsOk(width, height) || events < 1 || events > OAN_MAX_EVENTS) throw new Error('invalid OAN')
  const frameBytes = width * height / 8
  if (!version2) {
    if (fps < 1 || fps > 120 || data.byteLength !== 14 + events * frameBytes) throw new Error('invalid OAN')
    const period = Math.max(1, Math.floor(1000 / fps))
    const steps: OanStep[] = []
    const pixels = data.slice(14)
    for (let i = 0; i < events; i += 1) steps.push({ delayMs: period, offset: i * frameBytes })
    return { version2, width, height, fps, frames: events, durationMs: period * events, frameBytes, loopIndex: OAN_NO_LOOP, steps, pixels }
  }
  if (fps > 120) throw new Error('invalid OAN')
  type Event = { op: number, delayMs: number, arg: number, src: number }
  const program: Event[] = []
  let offset = 14
  let stored = 0
  for (let i = 0; i < events; i += 1) {
    if (offset >= data.byteLength) throw new Error('invalid OAN')
    const op = data[offset]
    offset += 1
    if (op === OAN_FRAME) {
      if (offset + 2 + frameBytes > data.byteLength) throw new Error('invalid OAN')
      const delayMs = u16(view, offset)
      offset += 2
      program.push({ op, delayMs, arg: 0, src: offset })
      offset += frameBytes
      stored += 1
      continue
    }
    if (op === OAN_SLEEP) {
      if (offset + 2 > data.byteLength) throw new Error('invalid OAN')
      program.push({ op, delayMs: u16(view, offset), arg: 0, src: -1 })
      offset += 2
      continue
    }
    if (op === OAN_INVERT_X || op === OAN_INVERT_Y) {
      if (offset + 4 > data.byteLength) throw new Error('invalid OAN')
      const arg = u16(view, offset)
      const delayMs = u16(view, offset + 2)
      if (op === OAN_INVERT_X && arg >= width) throw new Error('invalid OAN')
      if (op === OAN_INVERT_Y && arg >= height) throw new Error('invalid OAN')
      program.push({ op, delayMs, arg, src: -1 })
      offset += 4
      continue
    }
    if (op === OAN_LOOP) {
      if (offset + 2 > data.byteLength) throw new Error('invalid OAN')
      const arg = u16(view, offset)
      if (arg >= events || arg === i) throw new Error('invalid OAN')
      program.push({ op, delayMs: 0, arg, src: -1 })
      offset += 2
      continue
    }
    throw new Error('invalid OAN')
  }
  if (offset !== data.byteLength || stored < 1) throw new Error('invalid OAN')
  const buf = new Uint8Array(frameBytes)
  const snapshots: Uint8Array[] = []
  const steps: OanStep[] = []
  let have = false
  let last = -1
  const push = (delayMs: number) => {
    if (!have) return
    const same = last >= 0 && snapshots[last].every((value, index) => value === buf[index])
    if (!same) {
      last = snapshots.length
      snapshots.push(Uint8Array.from(buf))
    }
    steps.push({ delayMs, offset: last * frameBytes })
  }
  let pc = 0
  let takenBack = false
  let loopEvent = -1
  let loopIndex = OAN_NO_LOOP
  let guard = 0
  while (pc < program.length && guard++ < OAN_MAX_EVENTS * 2) {
    const event = program[pc]
    if (event.op === OAN_LOOP) {
      if (event.arg > pc) { pc = event.arg; continue }
      if (!takenBack) {
        takenBack = true
        loopEvent = pc
        loopIndex = steps.length
        pc = event.arg
        continue
      }
      if (pc === loopEvent) break
      pc = event.arg
      continue
    }
    if (event.op === OAN_FRAME) {
      buf.set(data.subarray(event.src, event.src + frameBytes))
      have = true
      push(event.delayMs)
    } else if (event.op === OAN_SLEEP) {
      push(event.delayMs)
    } else if (event.op === OAN_INVERT_X) {
      if (!have) throw new Error('invalid OAN')
      invertX(buf, width, height, event.arg)
      push(event.delayMs)
    } else if (event.op === OAN_INVERT_Y) {
      if (!have) throw new Error('invalid OAN')
      invertY(buf, width, height, event.arg)
      push(event.delayMs)
    }
    pc += 1
  }
  if (!steps.length) throw new Error('invalid OAN')
  const pixels = new Uint8Array(snapshots.length * frameBytes)
  snapshots.forEach((snapshot, index) => pixels.set(snapshot, index * frameBytes))
  const durationMs = steps.reduce((sum, step) => sum + step.delayMs, 0)
  return { version2, width, height, fps, frames: snapshots.length, durationMs, frameBytes, loopIndex: loopIndex >= steps.length ? OAN_NO_LOOP : loopIndex, steps, pixels }
}

export function frameAt(parsed: ParsedOan, elapsedMs: number) {
  const { steps, durationMs, loopIndex } = parsed
  if (!steps.length) return steps[0]
  let cursor = elapsedMs
  let start = 0
  if (loopIndex !== OAN_NO_LOOP && loopIndex < steps.length) {
    const prefixMs = steps.slice(0, loopIndex).reduce((sum, step) => sum + step.delayMs, 0)
    const cycleMs = steps.slice(loopIndex).reduce((sum, step) => sum + step.delayMs, 0)
    if (elapsedMs < prefixMs) {
      cursor = elapsedMs
      start = 0
    } else if (!cycleMs) {
      return steps[steps.length - 1]
    } else {
      cursor = (elapsedMs - prefixMs) % cycleMs
      start = loopIndex
    }
  } else if (durationMs && elapsedMs >= durationMs) {
    return steps[steps.length - 1]
  }
  let remaining = cursor
  for (let i = start; i < steps.length; i += 1) {
    if (remaining < steps[i].delayMs) return steps[i]
    remaining -= steps[i].delayMs
  }
  return steps[steps.length - 1]
}

function emitDelay(chunks: number[], delayMs: number, pixels: Uint8Array | null, counts: { events: number }) {
  let remaining = Math.max(pixels ? 1 : 0, delayMs)
  let first = pixels !== null
  while (remaining > 0 || first) {
    const part = Math.min(Math.max(remaining, first ? 1 : 0), 65535)
    if (first && pixels) {
      chunks.push(OAN_FRAME, part & 255, (part >> 8) & 255)
      for (let i = 0; i < pixels.length; i += 1) chunks.push(pixels[i])
      first = false
    } else {
      chunks.push(OAN_SLEEP, part & 255, (part >> 8) & 255)
    }
    counts.events += 1
    remaining -= part
    if (!first && remaining <= 0) break
  }
}

export function encodeOan2(width: number, height: number, fps: number, sampled: Uint8Array[]) {
  if (!sampled.length) throw new Error('invalid OAN')
  const period = Math.max(1, Math.floor(1000 / Math.max(1, fps)))
  const chunks: number[] = []
  const counts = { events: 0 }
  let pending = sampled[0]
  let delay = period
  const sameAs = (left: Uint8Array, right: Uint8Array) => {
    if (left.length !== right.length) return false
    for (let i = 0; i < left.length; i += 1) if (left[i] !== right[i]) return false
    return true
  }
  for (let i = 1; i < sampled.length; i += 1) {
    if (sameAs(pending, sampled[i])) {
      delay += period
      continue
    }
    emitDelay(chunks, delay, pending, counts)
    pending = sampled[i]
    delay = period
  }
  emitDelay(chunks, delay, pending, counts)
  if (counts.events > OAN_MAX_EVENTS) throw new Error('invalid OAN')
  const output = new Uint8Array(14 + chunks.length)
  if (output.byteLength > OAN_MAX_BYTES) throw new Error('asset too large')
  const header = new DataView(output.buffer)
  header.setUint8(0, 0x4f)
  header.setUint8(1, 0x41)
  header.setUint8(2, 0x4e)
  header.setUint8(3, 0x32)
  header.setUint16(4, width, true)
  header.setUint16(6, height, true)
  header.setUint16(8, fps, true)
  header.setUint32(10, counts.events, true)
  output.set(chunks, 14)
  return output
}
