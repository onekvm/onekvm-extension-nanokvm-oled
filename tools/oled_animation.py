#!/usr/bin/env python3
"""Convert a video to the compact page-major format used by the OLED player.

OAN2 starts with ``OAN2`` followed by width (u16), height (u16), fps (u16),
and event count (u32), all little endian. Events:

- ``FRAME`` (0x01): delay u16, pixels
- ``SLEEP`` (0x02): delay u16, keep the current frame
- ``INVERT_X`` (0x03): column u16, delay u16
- ``INVERT_Y`` (0x04): row u16, delay u16
- ``LOOP`` (0x05): event index u16, a goto to that operation

This converter emits FRAME/SLEEP only. Consecutive identical frames become a
longer delay so a long hold does not store every copy.
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
    period_ms = max(1, 1000 // args.fps)
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", args.input,
        "-vf", f"fps={args.fps},scale={args.width}:{args.height}:flags=lanczos,format=gray",
        "-f", "rawvideo", "-pix_fmt", "gray", "-",
    ]
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    events: list[tuple[int, bytes | None, int]] = []
    pending: bytearray | None = None
    pending_delay = 0
    sampled = 0
    try:
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
            sampled += 1
            if pending is not None and pending == frame:
                pending_delay += period_ms
                continue
            if pending is not None:
                remaining = pending_delay
                events.append((1, bytes(pending), min(remaining, 65535)))
                remaining -= min(remaining, 65535)
                while remaining > 0:
                    chunk_delay = min(remaining, 65535)
                    events.append((2, None, chunk_delay))
                    remaining -= chunk_delay
            pending = frame
            pending_delay = period_ms
        if pending is not None:
            remaining = max(1, pending_delay)
            events.append((1, bytes(pending), min(remaining, 65535)))
            remaining -= min(remaining, 65535)
            while remaining > 0:
                chunk_delay = min(remaining, 65535)
                events.append((2, None, chunk_delay))
                remaining -= chunk_delay
    finally:
        process.stdout.close()
        status = process.wait()
    if status:
        return status
    if not events:
        raise RuntimeError("video produced no frames")
    with open(args.output, "wb") as output:
        output.write(struct.pack("<4sHHHI", b"OAN2", args.width, args.height,
                                 args.fps, len(events)))
        stored = 0
        for op, pixels, delay in events:
            output.write(struct.pack("<BH", op, delay))
            if op == 1 and pixels is not None:
                output.write(pixels)
                stored += 1
    print(f"{sampled} sampled, {stored} unique, {len(events)} events, "
          f"{args.width}x{args.height} @ {args.fps} fps -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
