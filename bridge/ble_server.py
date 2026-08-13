"""BLE central: connects to CodexPet peripheral and writes state packets."""
from __future__ import annotations

import asyncio
import logging
import struct
import time
from typing import Any

from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

log = logging.getLogger("ble-server")

STATE_ENUM = {
    "OFFLINE": 0,
    "IDLE": 1,
    "WORKING": 2,
    "WAITING": 3,
    "COMPLETED": 4,
    "ERROR": 5,
    "SLEEP": 6,
}


class BleBridge:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self._client: BleakClient | None = None
        self._state_char: str = cfg["device"]["state_char_uuid"]
        self._pending: asyncio.Queue[dict] = asyncio.Queue()
        self._last_state = "IDLE"

    async def run(self, stop_event: asyncio.Event):
        log.info("BLE bridge started; looking for %s", self.cfg["device"]["name"])
        while not stop_event.is_set():
            try:
                await self._connect_and_run(stop_event)
            except Exception as e:
                log.warning("BLE error: %s", e)
            if not stop_event.is_set():
                await asyncio.sleep(self.cfg["connection"]["reconnect_interval"])

    async def _connect_and_run(self, stop_event: asyncio.Event):
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: d.name == self.cfg["device"]["name"],
            timeout=self.cfg["connection"]["scan_timeout"],
        )
        if device is None:
            raise RuntimeError("device not found")
        log.info("Connecting to %s", device.address)
        async with BleakClient(device) as client:
            self._client = client
            log.info("Connected")
            while client.is_connected and not stop_event.is_set():
                try:
                    msg = await asyncio.wait_for(self._pending.get(), timeout=1.0)
                    await self._send_packet(msg)
                except asyncio.TimeoutError:
                    # send heartbeat
                    await self._send_packet({"state": self._last_state or "IDLE", "flags": 0x01})

    async def send_state(self, state: dict):
        self._last_state = state.get("state", "IDLE")
        await self._pending.put(state)

    async def _send_packet(self, state: dict):
        if self._client is None or not self._client.is_connected:
            return
        ts = int(state.get("timestamp", time.time()))
        progress = state.get("progress", 255)
        if isinstance(progress, int) and 0 <= progress <= 100:
            pass
        else:
            progress = 255
        packet = struct.pack(
            "<BBBBBBI",
            0x01,
            STATE_ENUM.get(state.get("state", "IDLE"), 1),
            progress,
            0,  # mood
            0,  # animation
            state.get("flags", 0),
            ts,
        )
        await self._client.write_gatt_char(self._state_char, packet, response=True)
        log.debug("sent state=%s", state.get("state"))
