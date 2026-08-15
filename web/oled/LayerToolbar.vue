<script setup lang="ts">
import { NButton, NInput, NInputNumber, NTag, NText, NTooltip } from 'naive-ui'
import {
  AlignHorizontalDistributeCenter,
  AlignHorizontalJustifyCenter,
  AlignVerticalDistributeCenter,
  AlignVerticalJustifyCenter,
  ArrowDown,
  ArrowUp,
  ClipboardPaste,
  Copy,
  Trash2,
} from '@lucide/vue'

export interface LayerToolbarState {
  layerCount: number
  selectedCount: number
  type: string
  name: string
  x: number
  y: number
  maxX: number
  maxY: number
  xDisabled: boolean
  canDelete: boolean
  canCopy: boolean
  canPaste: boolean
  canMoveDown: boolean
  canMoveUp: boolean
  canCenter: boolean
  canDistribute: boolean
}

const props = defineProps<{
  state: LayerToolbarState
  labels: Record<string, string>
}>()

const emit = defineEmits<{
  update: [field: 'name' | 'x' | 'y', value: string | number]
  action: [action: 'center-horizontal' | 'center-vertical' | 'distribute-horizontal' | 'distribute-vertical' | 'copy' | 'paste' | 'move-down' | 'move-up' | 'delete']
}>()

function updateNumber(field: 'x' | 'y', value: number | null) {
  if (value !== null) emit('update', field, value)
}
</script>

<template>
  <div v-if="state.selectedCount === 0" class="layer-toolbar layer-toolbar-empty">
    <NText depth="3">{{ state.layerCount === 0 ? labels.empty : labels.select }}</NText>
  </div>
  <div v-else class="layer-toolbar">
    <NTag size="small">
      {{ state.selectedCount > 1 ? labels.selected.replace('{count}', String(state.selectedCount)) : (labels[state.type] || state.type) }}
    </NTag>

    <template v-if="state.selectedCount === 1">
      <NInput
        size="small"
        class="layer-toolbar-name"
        :value="state.name"
        :placeholder="labels.layerName"
        :aria-label="labels.layerName"
        @update:value="emit('update', 'name', $event)"
      />
      <div class="layer-toolbar-coordinates">
        <span>X</span>
        <NInputNumber
          size="small"
          :value="state.x"
          :min="0"
          :max="state.maxX"
          :show-button="false"
          :disabled="state.xDisabled"
          :input-props="{ 'aria-label': 'X' }"
          @update:value="updateNumber('x', $event)"
        />
        <span>Y</span>
        <NInputNumber
          size="small"
          :value="state.y"
          :min="0"
          :max="state.maxY"
          :show-button="false"
          :input-props="{ 'aria-label': 'Y' }"
          @update:value="updateNumber('y', $event)"
        />
      </div>
    </template>

    <div class="layer-toolbar-group">
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canCenter" :aria-label="labels.centerHorizontal" @click="emit('action', 'center-horizontal')"><template #icon><AlignHorizontalJustifyCenter :size="16" /></template></NButton></template>{{ labels.centerHorizontal }}</NTooltip>
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canCenter" :aria-label="labels.centerVertical" @click="emit('action', 'center-vertical')"><template #icon><AlignVerticalJustifyCenter :size="16" /></template></NButton></template>{{ labels.centerVertical }}</NTooltip>
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canDistribute" :aria-label="labels.distributeHorizontal" @click="emit('action', 'distribute-horizontal')"><template #icon><AlignHorizontalDistributeCenter :size="16" /></template></NButton></template>{{ labels.distributeHorizontal }}</NTooltip>
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canDistribute" :aria-label="labels.distributeVertical" @click="emit('action', 'distribute-vertical')"><template #icon><AlignVerticalDistributeCenter :size="16" /></template></NButton></template>{{ labels.distributeVertical }}</NTooltip>
    </div>

    <div class="layer-toolbar-group">
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canCopy" :aria-label="labels.copy" @click="emit('action', 'copy')"><template #icon><Copy :size="16" /></template></NButton></template>{{ labels.copy }}</NTooltip>
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canPaste" :aria-label="labels.paste" @click="emit('action', 'paste')"><template #icon><ClipboardPaste :size="16" /></template></NButton></template>{{ labels.paste }}</NTooltip>
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canMoveDown" :aria-label="labels.moveDown" @click="emit('action', 'move-down')"><template #icon><ArrowDown :size="16" /></template></NButton></template>{{ labels.moveDown }}</NTooltip>
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" :disabled="!state.canMoveUp" :aria-label="labels.moveUp" @click="emit('action', 'move-up')"><template #icon><ArrowUp :size="16" /></template></NButton></template>{{ labels.moveUp }}</NTooltip>
      <NTooltip trigger="hover"><template #trigger><NButton circle quaternary size="small" type="error" :disabled="!state.canDelete" :aria-label="labels.delete" @click="emit('action', 'delete')"><template #icon><Trash2 :size="16" /></template></NButton></template>{{ labels.delete }}</NTooltip>
    </div>
  </div>
</template>
