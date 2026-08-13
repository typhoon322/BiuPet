#!/usr/bin/env python3
"""CodexPet Mac bridge: Codex state -> BLE -> ESP32 pet."""
import argparse
import asyncio
import logging
import signal
import sys
import time
import tomllib
from pathlib import Path

from config import load_config
from codex_monitor import CodexMonitor
from ble_server import BleBridge
from usage_tracker import UsageTracker

log = logging.getLogger("codex-pet-bridge")

DS_BALANCE_URL = "https://api.deepseek.com/user/balance"


def load_deepseek_key() -> str:
    """Read the DeepSeek key from the Codex config (same source Codex uses)."""
    cfg = Path.home() / ".codex" / "config.toml"
    if not cfg.exists():
        return ""
    try:
        with cfg.open("rb") as f:
            data = tomllib.load(f)
        prov = data.get("model_providers", {}).get("deepseek", {})
        return prov.get("experimental_bearer_token", "") or prov.get("api_key", "")
    except Exception as e:  # noqa: BLE001
        log.warning("cannot read deepseek key from codex config: %s", e)
        return ""


def fetch_deepseek_balance() -> str:
    """Return the total balance string, or "--" on any failure."""
    try:
        import requests

        key = load_deepseek_key()
        if not key:
            return "--"
        resp = requests.get(DS_BALANCE_URL, headers={"Authorization": f"Bearer {key}"}, timeout=10)
        resp.raise_for_status()
        data = resp.json()
        if not data.get("is_available", False):
            return "--"
        infos = data.get("balance_infos") or []
        if not infos:
            return "--"
        return str(infos[0].get("total_balance", "--"))
    except Exception as e:  # noqa: BLE001
        log.warning("deepseek balance fetch failed: %s", e)
        return "--"


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
    usage = UsageTracker(cfg["monitor"]["session_dir"])
    last_balance_at = 0.0

    async def on_state(state):
        log.info("monitor -> %s", state.get("state"))
        await bridge.send_state(state)

    async def usage_loop():
        nonlocal last_balance_at
        while not stop_event.is_set():
            try:
                tokens = usage.today_tokens()
                if tokens > 0:
                    # usage must not touch the current pet state
                    await bridge.send_usage(tokens)
                # DeepSeek balance, refreshed every 30s
                if time.monotonic() - last_balance_at >= 30:
                    last_balance_at = time.monotonic()
                    await bridge.send_balance(fetch_deepseek_balance())
            except Exception as e:
                log.warning("usage loop error: %s", e)
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=30)
            except asyncio.TimeoutError:
                pass

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
        usage_loop(),
        return_exceptions=True,
    )


if __name__ == "__main__":
    asyncio.run(main())
