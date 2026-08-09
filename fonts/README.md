# OLED bitmap font / OLED 点阵字体

## English

`font-cjk-12.bin` is the 12×12 Chinese bitmap font used by the OLED renderer. `web/oled/font-cjk-12.js` contains the same data for the browser preview.

The font is named **OneKVM CJK Dot 12** and is a bitmap adaptation of **Noto Sans CJK SC**.

Copyright 2010–2012 Google Corporation

It is distributed under the **SIL Open Font License 1.1**. The full license text is available in [`LICENSE-OFL-1.1.txt`](LICENSE-OFL-1.1.txt).

To regenerate both font assets from a local Noto Sans CJK `.ttc` or `.otf` file:

```sh
python3 tools/generate_cjk_font.py \
  /path/to/NotoSansCJK.ttc \
  fonts/font-cjk-12.bin \
  --javascript-output web/oled/font-cjk-12.js
```

The generator requires Pillow. Its default TTC face index is `2`, which selects Simplified Chinese in the expected Noto Sans CJK collection. Use `--index` if your source font is arranged differently.

## 简体中文

`font-cjk-12.bin` 是 OLED 渲染器使用的 12×12 中文点阵字库，`web/oled/font-cjk-12.js` 则保存同一份数据，供网页预览使用。

该字体名为 **OneKVM CJK Dot 12**，由 **Noto Sans CJK SC** 改编而来。

版权所有 © 2010–2012 Google Corporation

本字体采用 **SIL Open Font License 1.1**，完整授权条款见 [`LICENSE-OFL-1.1.txt`](LICENSE-OFL-1.1.txt)。

如果本地有 Noto Sans CJK 的 `.ttc` 或 `.otf` 源字体，可以用下面的命令重新生成两份字体资源：

```sh
python3 tools/generate_cjk_font.py \
  /path/to/NotoSansCJK.ttc \
  fonts/font-cjk-12.bin \
  --javascript-output web/oled/font-cjk-12.js
```

生成脚本依赖 Pillow。默认 TTC 字体索引为 `2`，对应常见 Noto Sans CJK 字体合集中的简体中文；如果源字体的内部顺序不同，可以通过 `--index` 指定。
