<script setup lang="ts">
import { NButton, NList, NListItem } from 'naive-ui'
import { Eye, EyeOff, GripVertical } from '@lucide/vue'

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

let dragging = ''

function dragStart(id: string, event: DragEvent) {
  dragging = id
  if (event.dataTransfer) {
    event.dataTransfer.effectAllowed = 'move'
    event.dataTransfer.setData('text/plain', id)
  }
}

function drop(target: string, event: DragEvent) {
  event.preventDefault()
  if (!dragging || dragging === target) return
  const row = event.currentTarget as HTMLElement
  const before = event.clientY < row.getBoundingClientRect().top + row.offsetHeight / 2
  emit('reorder', dragging, target, before)
  dragging = ''
}
</script>

<template>
  <NList v-if="props.layers.length" hoverable clickable>
    <NListItem
      v-for="layer in props.layers"
      :key="layer.id"
      draggable="true"
      @click="emit('select', layer.id, $event.shiftKey || $event.ctrlKey || $event.metaKey)"
      @dragstart="dragStart(layer.id, $event)"
      @dragover.prevent
      @drop="drop(layer.id, $event)"
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
          <img class="layer-list-thumbnail" :src="layer.thumbnail" :alt="layer.name">
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
