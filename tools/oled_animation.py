#!/usr/bin/env python3
"""Convert a video to the compact page-major format used by the OLED player.

The output starts with ``OAN1`` followed by width (u16), height (u16), fps
(u16), and frame count (u32), all little endian. Each frame is SSD1306 page
major, least-significant bit first, exactly as the NanoKVM driver writes it.
"""
from __future__ import annotations

import argparse
import struct
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--width", type=int, default=128)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--fps", type=int, default=30)
    args = parser.parse_args()
    if args.width <= 0 or args.width > 128 or args.height <= 0 or args.height % 8:
        parser.error("dimensions must be positive, width <= 128 and height divisible by 8")
    frame_bytes = args.width * args.height // 8
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", args.input,
        "-vf", f"fps={args.fps},scale={args.width}:{args.height}:flags=lanczos,format=gray",
        "-f", "rawvideo", "-pix_fmt", "gray", "-",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    count = 0
    try:
        with open(args.output, "wb") as output:
            output.write(struct.pack("<4sHHHI", b"OAN1", args.width, args.height,
                                     args.fps, 0))
            while True:
                raw = bytearray()
                while len(raw) < args.width * args.height:
                    chunk = process.stdout.read(args.width * args.height - len(raw))
                    if not chunk:
                        break
                    raw.extend(chunk)
                if not raw:
                    break
                if len(raw) != args.width * args.height:
                    raise RuntimeError("ffmpeg ended in the middle of a frame")
                frame = bytearray(frame_bytes)
                for y in range(args.height):
                    page = y // 8
                    for x in range(args.width):
                        if raw[y * args.width + x] >= 128:
                            frame[page * args.width + x] |= 1 << (y % 8)
                output.write(frame)
                count += 1
            output.seek(10)
            output.write(struct.pack("<I", count))
    finally:
        process.stdout.close()
        status = process.wait()
    if status:
        return status
    print(f"{count} frames, {args.width}x{args.height} @ {args.fps} fps -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
