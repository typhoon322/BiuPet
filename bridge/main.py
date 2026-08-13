#!/usr/bin/env python3
"""CodexPet Mac bridge: Codex state -> BLE -> ESP32 pet."""
import argparse
import asyncio
import logging
import signal
import sys

from config import load_config
from codex_monitor import CodexMonitor
from ble_server import BleBridge

log = logging.getLogger("codex-pet-bridge")


async def main():
    parser = argparse.ArgumentParser(description="CodexPet bridge")
    parser.add_argument("--debug", action="store_true", help="verbose logging")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="[%(name)s] %(levelname)s: %(message)s",
    )

    cfg = load_config()
    monitor = CodexMonitor(cfg)
    bridge = BleBridge(cfg)

    async def on_state(state):
        log.info("monitor -> %s", state.get("state"))
        await bridge.send_state(state)

    monitor.set_callback(on_state)

    stop_event = asyncio.Event()
    loop = asyncio.get_event_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stop_event.set)
        except NotImplementedError:
            pass

    log.info("bridge starting")
    await asyncio.gather(
        monitor.run(stop_event),
        bridge.run(stop_event),
        return_exceptions=True,
    )


if __name__ == "__main__":
    asyncio.run(main())
