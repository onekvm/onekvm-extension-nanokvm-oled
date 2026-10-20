<script setup lang="ts">
import { reactive, ref } from 'vue'
import { NButton, NInput, NModal } from 'naive-ui'

import { translate, type MessageKey } from './i18n'
import type { OledChromeVariable } from './OledChrome.vue'

const props = defineProps<{
  locale: string
  variables: OledChromeVariable[]
  dirty: boolean
  save: () => Promise<void>
}>()

const emit = defineEmits<{ update: [kind: string, value: unknown] }>()
const keyDraft = reactive<Record<string, string>>({})
const devOpen = ref(false)
const saving = ref(false)
const t = (key: MessageKey) => translate(props.locale, key)

function variableSuffix(key: string) {
  return key.startsWith('c_') ? key.slice(2) : key
}

async function saveVariables() {
  for (const key of Object.keys(keyDraft)) commitKey(key)
  saving.value = true
  try {
    await props.save()
  } finally {
    saving.value = false
  }
}
type FieldRow = { rule: MessageKey } & ({ label: string } | { name: MessageKey })
const fieldRows: FieldRow[] = [
  { label: 'Key', rule: 'variablesDevKeyRule' },
  { label: 'Value', rule: 'variablesDevValueRule' },
  { name: 'variablesDevCount', rule: 'variablesDevCountRule' },
  { name: 'variablesDevText', rule: 'variablesDevTextRule' },
]

function bashDemoText() {
  return [
    '#!/bin/bash',
    'set -euo pipefail',
    '',
    `# ${t('variablesDevBashComment')}`,
    'oled_var_update() {',
    '  busctl call org.onekvm.NanoKvmOled1 /org/onekvm/NanoKvmOled1 \\',
    '    org.onekvm.NanoKvmOled1 VariableUpdate ss "$1" "$2"',
    '}',
    '',
    'oled_var_remove() {',
    '  busctl call org.onekvm.NanoKvmOled1 /org/onekvm/NanoKvmOled1 \\',
    '    org.onekvm.NanoKvmOled1 VariableRemove s "$1"',
    '}',
    '',
    'oled_var_update room A1',
    'oled_var_remove room',
  ].join('\n')
}

function commitKey(key: string) {
  const next = keyDraft[key]
  if (next !== undefined && next !== variableSuffix(key)) emit('update', 'variable-key', { key, next })
  delete keyDraft[key]
}
</script>

<template>
  <div class="variables-pane">
    <section class="card variables-editor">
      <div class="variables-head">
        <h2>{{ t('variables') }}</h2>
        <NButton size="small" secondary @click="devOpen = true">{{ t('variablesDevTitle') }}</NButton>
      </div>
      <p>{{ t('variablesHint') }}</p>
      <div class="oled-variable-stack">
        <div v-for="row in variables" :key="row.key" class="variable-row">
          <NInput
            :value="keyDraft[row.key] ?? variableSuffix(row.key)"
            :maxlength="30"
            :placeholder="t('variableKey')"
            :aria-label="t('variableKey')"
            @update:value="keyDraft[row.key] = $event"
            @blur="commitKey(row.key)"
          >
            <template #prefix>c_</template>
          </NInput>
          <NInput
            :value="row.value"
            :maxlength="64"
            :placeholder="t('variableValue')"
            :aria-label="t('variableValue')"
            @update:value="emit('update', 'variable-value', { key: row.key, value: $event })"
          />
          <NButton size="small" quaternary type="error" @click="emit('update', 'variable-remove', row.key)">
            {{ t('variableRemove') }}
          </NButton>
        </div>
      </div>
      <div class="oled-variable-actions">
        <NButton secondary @click="emit('update', 'variable-add', null)">{{ t('variableAdd') }}</NButton>
        <NButton type="primary" :loading="saving" :disabled="!dirty || saving" @click="saveVariables">
          {{ t('variablesSave') }}
        </NButton>
      </div>
    </section>

    <NModal v-model:show="devOpen" preset="card" to=".onekvm-oled-page .variables-pane" class="variables-dev-modal" :title="t('variablesDevTitle')">
      <div class="variables-dev">
        <p>{{ t('variablesDevLead') }}</p>
        <table class="variables-dev-fields">
          <tbody>
            <tr v-for="row in fieldRows" :key="row.rule">
              <th scope="row">{{ 'label' in row ? row.label : t(row.name) }}</th>
              <td>{{ t(row.rule) }}</td>
            </tr>
          </tbody>
        </table>
        <h3>{{ t('variablesDevBashTitle') }}</h3>
        <pre><code>{{ bashDemoText() }}</code></pre>
      </div>
    </NModal>
  </div>
</template>
