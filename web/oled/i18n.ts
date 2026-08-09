import fallbackMessages, { type Messages } from './locales/en'

export type MessageKey = keyof Messages
export type InterpolationValues = Record<string, string | number>

export interface LocaleDefinition {
  locale: string
  htmlLang: string
  aliases?: readonly string[]
  messages: Messages
}

const localeModules = import.meta.glob<LocaleDefinition>('./locales/*.ts', {
  eager: true,
  import: 'default',
})
const fallbackDefinition = fallbackMessages satisfies LocaleDefinition
const definitions = new Map<string, LocaleDefinition>()
const aliases = new Map<string, string>()

function normalizeLocale(value: string | null | undefined) {
  return String(value || '').trim().toLowerCase().replace(/_/g, '-')
}

function registerLocale(definition: LocaleDefinition) {
  const locale = normalizeLocale(definition.locale)
  if (!locale) return
  definitions.set(locale, definition)
  for (const alias of [definition.locale, ...(definition.aliases || [])]) {
    const normalized = normalizeLocale(alias)
    if (normalized) aliases.set(normalized, locale)
  }
}

registerLocale(fallbackDefinition)
for (const definition of Object.values(localeModules)) registerLocale(definition)

export const fallbackLocale = normalizeLocale(fallbackDefinition.locale)

export function availableLocales() {
  return [...definitions.values()].map(({ locale, htmlLang }) => ({ locale, htmlLang }))
}

function supportedLocale(value: string | null | undefined) {
  const normalized = normalizeLocale(value)
  if (!normalized) return ''
  const exact = aliases.get(normalized)
  if (exact) return exact
  return aliases.get(normalized.split('-')[0]) || ''
}

export function resolveLocale(candidates: readonly (string | null | undefined)[]) {
  for (const candidate of candidates) {
    const locale = supportedLocale(candidate)
    if (locale) return locale
  }
  return fallbackLocale
}

export function detectLocale() {
  const documentLocale = typeof document === 'undefined' ? '' : document.documentElement.lang
  const browserLocales = typeof navigator === 'undefined'
    ? []
    : (navigator.languages?.length ? navigator.languages : [navigator.language])
  return resolveLocale([documentLocale, ...browserLocales])
}

export function htmlLang(locale: string) {
  return definitions.get(resolveLocale([locale]))?.htmlLang || fallbackDefinition.htmlLang
}

export function messagesFor(locale: string) {
  return definitions.get(resolveLocale([locale]))?.messages || fallbackDefinition.messages
}

export function translate(
  locale: string,
  key: MessageKey,
  values: InterpolationValues = {},
) {
  const template = messagesFor(locale)[key] || fallbackDefinition.messages[key] || key
  return template.replace(/\{([^{}]+)\}/g, (placeholder, name: string) => (
    Object.prototype.hasOwnProperty.call(values, name) ? String(values[name]) : placeholder
  ))
}
