#!/usr/bin/env python3
"""Manually send a pet state to the ESP32 over BLE (Phase 2 test tool)."""
from __future__ import annotations

import argparse
import asyncio
import logging
import sys

from bleak import BleakClient, BleakScanner

from protocol import STATE_CHAR_UUID, TASK_CHAR_UUID, build_state_packet

log = logging.getLogger("send-state")


async def find_device(name: str, timeout: float):
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == name, timeout=timeout
    )
    return device


async def main() -> int:
    parser = argparse.ArgumentParser(description="Send CodexPet state over BLE")
    parser.add_argument("state", choices=[
        "OFFLINE", "IDLE", "WORKING", "WAITING", "COMPLETED", "ERROR", "SLEEP",
    ])
    parser.add_argument("--progress", type=int, default=255, help="0-100 or 255")
    parser.add_argument("--mood", default="NORMAL")
    parser.add_argument("--task", default="", help="optional task text")
    parser.add_argument("--flags", type=int, default=0, help="bit0=heartbeat")
    parser.add_argument("--name", default="CodexPet")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--address", default=None, help="BLE address (skip scan)")
    parser.add_argument("--response", action="store_true", default=True, help="use write with response (default; NimBLE WRITE_NR callbacks are unreliable)")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="[%(name)s] %(message)s",
    )

    if args.address:
        address = args.address
    else:
        log.info("scanning for %s ...", args.name)
        device = await find_device(args.name, args.timeout)
        if device is None:
            log.error("device %s not found", args.name)
            return 1
        address = device.address
        log.info("found %s at %s", args.name, address)

    packet = build_state_packet(args.state, progress=args.progress, mood=args.mood, flags=args.flags)
    log.info("packet: %s", packet.hex(" "))

    async with BleakClient(address) as client:
        log.info("connected to %s", address)
        await client.write_gatt_char(STATE_CHAR_UUID, packet, response=args.response)  # noqa: E501
        log.info("state %s sent", args.state)
        if args.task:
            await client.write_gatt_char(TASK_CHAR_UUID, args.task.encode("utf-8"), response=args.response)
            log.info("task sent: %s", args.task)
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
