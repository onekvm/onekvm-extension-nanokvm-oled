<script setup lang="ts">
import { computed, shallowRef } from 'vue'
import { NButton, NList, NListItem } from 'naive-ui'
import { Eye, EyeOff, GripVertical } from '@lucide/vue'
import { useListReorderMotion } from './useListReorderMotion'

export interface LayerListItem {
  id: string
  name: string
  type: string
  x: number
  y: number
  hidden: boolean
  thumbnail: string
}

const props = defineProps<{ layers: LayerListItem[]; labels: Record<string, string> }>()
const emit = defineEmits<{
  select: [id: string, additive: boolean]
  toggle: [id: string]
  reorder: [source: string, target: string, before: boolean]
}>()

const dragging = shallowRef('')
const dropTarget = shallowRef<{ id: string; before: boolean } | null>(null)
let slots: { id: string; center: number }[] = []
let listElement: HTMLElement | null = null

const previewLayers = computed(() => {
  const layers = [...props.layers]
  const source = layers.findIndex(layer => layer.id === dragging.value)
  const target = dropTarget.value
  if (source < 0 || !target || target.id === dragging.value)
    return layers
  const [layer] = layers.splice(source, 1)
  const index = layers.findIndex(layer => layer.id === target.id)
  if (index >= 0)
    layers.splice(index + (target.before ? 0 : 1), 0, layer!)
  return layers
})
useListReorderMotion(previewLayers, () => listElement, '[data-layer-id]')

function dragEnd() {
  dragging.value = ''
  dropTarget.value = null
  slots = []
}

function dragStart(id: string, event: DragEvent) {
  const row = event.currentTarget as HTMLElement
  const list = row.parentElement!
  listElement = list
  // Hit-test the original slots so animated rows cannot move the drop target.
  slots = Array.from(list.querySelectorAll<HTMLElement>('[data-layer-id]')).map(row => ({
    id: row.dataset.layerId!,
    center: row.offsetTop + row.offsetHeight / 2 + list.clientTop - list.scrollTop,
  }))
  dragging.value = id
  dropTarget.value = null
  if (event.dataTransfer) {
    event.dataTransfer.effectAllowed = 'move'
    event.dataTransfer.setData('text/plain', id)
  }
}

function dragOver(event: DragEvent) {
  if (!dragging.value || !slots.length)
    return
  event.preventDefault()
  if (event.dataTransfer)
    event.dataTransfer.dropEffect = 'move'
  const y = event.clientY - (event.currentTarget as HTMLElement).getBoundingClientRect().top
  const slot = slots.reduce((best, slot) => Math.abs(slot.center - y) < Math.abs(best.center - y) ? slot : best)
  dropTarget.value = { id: slot.id, before: y < slot.center }
}

function drop(event: DragEvent) {
  dragOver(event)
  const target = dropTarget.value
  if (dragging.value && target && dragging.value !== target.id)
    emit('reorder', dragging.value, target.id, target.before)
  dragEnd()
}
</script>

<template>
  <NList v-if="props.layers.length" class="layer-sort-list" hoverable clickable @dragover="dragOver" @drop="drop">
    <NListItem
      v-for="layer in previewLayers"
      :key="layer.id"
      class="layer-list-row"
      :class="{ 'is-dragging': dragging === layer.id }"
      :data-layer-id="layer.id"
      draggable="true"
      @click="emit('select', layer.id, $event.shiftKey || $event.ctrlKey || $event.metaKey)"
      @dragstart="dragStart(layer.id, $event)"
      @dragend="dragEnd"
    >
      <template #prefix>
        <NButton text :aria-label="layer.hidden ? labels.showLayer : labels.hideLayer" @click.stop="emit('toggle', layer.id)">
          <template #icon>
            <EyeOff v-if="layer.hidden" :size="16" aria-hidden="true" />
            <Eye v-else :size="16" aria-hidden="true" />
          </template>
        </NButton>
      </template>
      <template #default>
        <div class="layer-list-content">
          <img class="layer-list-thumbnail" :src="layer.thumbnail" :alt="layer.name" draggable="false">
          <div class="layer-list-info">
            <strong>{{ layer.name }}</strong>
            <small>{{ labels[layer.type] || layer.type }}</small>
          </div>
        </div>
      </template>
      <template #suffix><GripVertical :size="16" aria-hidden="true" /></template>
    </NListItem>
  </NList>
  <NList v-else>
    <NListItem>{{ labels.empty }}</NListItem>
  </NList>
</template>
