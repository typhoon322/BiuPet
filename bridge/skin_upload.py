#!/usr/bin/env python3
"""Upload PNG sprite frames to the ESP32 over WiFi (SoftAP) as RGB565 frames."""
from __future__ import annotations

import argparse
import base64
import glob
import os
import struct
import sys

import requests
from PIL import Image

STATE_IDS = {
    "OFFLINE": 0, "IDLE": 1, "WORKING": 2, "WAITING": 3,
    "COMPLETED": 4, "ERROR": 5, "SLEEP": 6,
}

FRAME_SIZE = 128


def png_to_rgb565(path: str) -> bytes:
    im = Image.open(path).convert("RGBA")
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


def upload_frame(ip: str, state_id: int, index: int, frame: bytes) -> None:
    url = f"http://{ip}/api/frame"
    params = {"state": state_id, "index": index}
    payload = base64.b64encode(frame)
    resp = requests.post(url, params=params, data=payload, timeout=15)
    print(f"[upload] state={state_id} index={index} -> HTTP {resp.status_code} {resp.text.strip()}")
    resp.raise_for_status()


def upload_delay(ip: str, state_id: int, delay_ms: int) -> None:
    url = "http://{}/api/delay".format(ip)
    params = {"state": state_id}
    resp = requests.post(url, params=params, data=str(delay_ms), timeout=10)
    print(f"[upload] state={state_id} delay={delay_ms}ms -> HTTP {resp.status_code} {resp.text.strip()}")
    resp.raise_for_status()


def main() -> int:
    parser = argparse.ArgumentParser(description="Upload PNG frames to CodexPet ESP32")
    parser.add_argument("--dir", required=True, help="directory of frame-*.png files")
    parser.add_argument("--state", required=True, choices=STATE_IDS.keys())
    parser.add_argument("--ip", default="192.168.4.1", help="ESP32 SoftAP IP")
    parser.add_argument("--pattern", default="frame-*.png")
    parser.add_argument("--delay-ms", type=int, default=0,
                        help="override frame delay; defaults to delay_ms.txt in --dir")
    args = parser.parse_args()

    files = sorted(glob.glob(os.path.join(args.dir, args.pattern)))
    if not files:
        print(f"no files match {args.pattern} in {args.dir}")
        return 1
    state_id = STATE_IDS[args.state]
    for i, path in enumerate(files):
        frame = png_to_rgb565(path)
        print(f"[convert] {os.path.basename(path)} -> {len(frame)} bytes")
        upload_frame(args.ip, state_id, i, frame)
    delay_ms = args.delay_ms
    if not delay_ms:
        delay_path = os.path.join(args.dir, "delay_ms.txt")
        if os.path.exists(delay_path):
            with open(delay_path) as f:
                delay_ms = int(f.read().strip())
    if delay_ms:
        upload_delay(args.ip, state_id, delay_ms)
    else:
        print(f"[skip] no delay_ms.txt in {args.dir}; firmware will default to 100ms")
    print(f"done: {len(files)} frames for {args.state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
