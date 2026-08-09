# Adding a locale

Add one `*.ts` file in this directory. The i18n runtime discovers locale files
automatically, so business components and the locale registry do not need to be
edited.

Use `en.ts` as the source schema and export a default definition containing:

- `locale`: the canonical locale tag;
- `htmlLang`: the value applied to the rendered page's `lang` attribute;
- `aliases`: optional browser or host locale aliases;
- `messages`: every key defined by the English source language.

Declare the message object with `satisfies Messages`, imported from `en.ts`.
`npm run check:web` will then report missing or unknown translation keys.
