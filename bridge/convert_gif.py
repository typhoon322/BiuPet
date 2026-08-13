#!/usr/bin/env python3
"""Convert GIF assets to 128x128 RGB565 frames + PNG preview collages."""
from __future__ import annotations

import os
import struct
import sys

from PIL import Image, ImageSequence

FRAME_SIZE = 128


def to_rgb565(im: Image.Image) -> bytes:
    im = im.convert("RGBA")
    im.thumbnail((FRAME_SIZE, FRAME_SIZE), Image.LANCZOS)
    canvas = Image.new("RGBA", (FRAME_SIZE, FRAME_SIZE), (0, 0, 0, 255))
    x = (FRAME_SIZE - im.width) // 2
    y = (FRAME_SIZE - im.height) // 2
    canvas.alpha_composite(im, (x, y))
    px = canvas.load()
    out = bytearray()
    for yy in range(FRAME_SIZE):
        for xx in range(FRAME_SIZE):
            r, g, b, a = px[xx, yy]
            if a < 16:
                r = g = b = 0
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out += struct.pack(">H", c)
    return bytes(out)


def convert(name: str, src: str, out_dir: str, max_frames: int = 0) -> int:
    im = Image.open(src)
    seq = list(ImageSequence.Iterator(im))
    durations = [fr.info.get("duration", 80) or 80 for fr in seq]
    frames = [fr.convert("RGBA") for fr in seq]
    total = len(frames)
    if max_frames and total > max_frames:
        # evenly sample max_frames
        idxs = sorted(set(round(i * (total - 1) / (max_frames - 1)) for i in range(max_frames)))
        frames = [frames[i] for i in idxs]
        durations = [durations[i] for i in idxs]
    frames_dir = os.path.join(out_dir, name, "frames")
    os.makedirs(frames_dir, exist_ok=True)
    avg_ms = round(sum(durations) / len(durations)) if durations else 100
    with open(os.path.join(frames_dir, "delay_ms.txt"), "w") as f:
        f.write(str(avg_ms))
    count = 0
    for i, rgba in enumerate(frames):
        rgba.save(os.path.join(frames_dir, f"frame-{i:02d}.png"))
        rgb = to_rgb565(rgba)
        with open(os.path.join(frames_dir, f"frame-{i:02d}.rgb565"), "wb") as f:
            f.write(rgb)
        count += 1
    # preview collage (4 per row)
    cols = 4
    rows = (count + cols - 1) // cols
    cell = 128
    canvas = Image.new("RGB", (cols * cell, rows * cell), (20, 20, 28))
    for i in range(count):
        fr = Image.open(os.path.join(frames_dir, f"frame-{i:02d}.png")).convert("RGBA")
        bg = Image.new("RGBA", (cell, cell), (0, 0, 0, 255))
        bg.alpha_composite(fr, (0, 0))
        canvas.paste(bg.convert("RGB"), ((i % cols) * cell, (i // cols) * cell))
    canvas.save(os.path.join(out_dir, name, "preview.png"))
    print(f"{name}: {count} frames, {avg_ms}ms/frame -> {frames_dir} + preview.png")
    return count


def main() -> int:
    root = "assets"
    items = [
        ("la_bi_xiao_xin_2324", os.path.join(root, "la_bi_xiao_xin_2324", "source.gif")),
        ("la_bi_xiao_xin_6803", os.path.join(root, "la_bi_xiao_xin_6803", "source.gif")),
        ("la_bi_xiao_xin_8374", os.path.join(root, "la_bi_xiao_xin_8374", "source.gif")),
        ("mao_die_mao_b_33", os.path.join(root, "mao_die_mao_b_33", "source.gif")),
        ("dong_man_dong_t_570", os.path.join(root, "dong_man_dong_t_570", "source.gif")),
    ]
    limits = {"la_bi_xiao_xin_2324": 16, "mao_die_mao_b_33": 10, "la_bi_xiao_xin_8374": 12}
    for name, src in items:
        if not os.path.exists(src):
            print(f"missing {src}")
            continue
        convert(name, src, root, limits.get(name, 0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
