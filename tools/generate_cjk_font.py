#!/usr/bin/env python3
"""Generate OneKVM's fixed-size GB2312 OLED bitmap font from an OFL font."""

import argparse
import base64
import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


MAGIC = b"OKCJK12\0"
SIZE = 12


def codepoints():
    values = set()
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                character = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            values.add(ord(character))
    return sorted(values)


def rasterize(font, character, threshold):
    image = Image.new("L", (SIZE, SIZE))
    draw = ImageDraw.Draw(image)
    left, top, right, bottom = draw.textbbox((0, 0), character, font=font)
    x = (SIZE - (right - left)) // 2 - left
    y = (SIZE - (bottom - top)) // 2 - top
    draw.text((x, y), character, font=font, fill=255)
    rows = []
    for row in range(SIZE):
        bits = 0
        for column in range(SIZE):
            if image.getpixel((column, row)) >= threshold:
                bits |= 1 << (SIZE - 1 - column)
        rows.append(bits)
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="Noto Sans CJK .ttc/.otf source")
    parser.add_argument("output", type=Path)
    parser.add_argument("--javascript-output", type=Path)
    parser.add_argument("--index", type=int, default=2, help="TTC face index (default: Simplified Chinese)")
    parser.add_argument("--threshold", type=int, default=80)
    args = parser.parse_args()

    font = ImageFont.truetype(str(args.source), SIZE, index=args.index)
    values = codepoints()
    output = io.BytesIO()
    output.write(struct.pack("<8sI", MAGIC, len(values)))
    for codepoint in values:
        rows = rasterize(font, chr(codepoint), args.threshold)
        output.write(struct.pack("<I12H", codepoint, *rows))
    data = output.getvalue()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    if args.javascript_output:
        args.javascript_output.parent.mkdir(parents=True, exist_ok=True)
        encoded = base64.b64encode(data).decode("ascii")
        args.javascript_output.write_text(
            f'globalThis.ONEKVM_CJK_FONT_BASE64="{encoded}";\n', encoding="ascii"
        )


if __name__ == "__main__":
    main()
