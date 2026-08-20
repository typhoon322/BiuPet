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


def _trim_utf8(data: bytes) -> bytes:
    """Cut `data` at a UTF-8 character boundary: drop any dangling
    continuation bytes AND the lead byte of a half-copied character."""
    while data and (data[-1] & 0xC0) == 0x80:
        data = data[:-1]
    if data and (data[-1] & 0xC0) == 0xC0:
        data = data[:-1]
    return data


class BleBridge:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self._client: BleakClient | None = None
        self._state_char: str = cfg["device"]["state_char_uuid"]
        self._task_char: str = cfg["device"].get("task_char_uuid", "")
        self._command_char: str = cfg["device"].get("command_char_uuid", "")
        self._last_task = ""
        self._last_usage = -1
        self._last_agents = None
        self._pending: asyncio.Queue[dict] = asyncio.Queue()
        self._last_state = "IDLE"

    async def _write(self, char: str, data: bytes, timeout: float = 5.0) -> bool:
        """Write to a GATT char with a bounded timeout so a half-dead link
        (connected but unresponsive) can never hang its caller forever."""
        if self._client is None or not self._client.is_connected or not char:
            return False
        try:
            await asyncio.wait_for(
                self._client.write_gatt_char(char, data, response=True), timeout)
            return True
        except asyncio.TimeoutError:
            log.warning("BLE write timed out (%s)", char[:8])
        except Exception as e:
            log.warning("BLE write failed: %s", e)
        return False

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
            # fresh link: re-send usage/task/agents on next update
            self._last_usage = -1
            self._last_task = ""
            self._last_agents = None
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

    async def send_task(self, task: str):
        """Send the current task text over the task char (63-byte cap, never
        splitting a UTF-8 char). Dedups via the connection-level _last_task,
        which is reset on every reconnect."""
        if not task or task == self._last_task:
            return
        self._last_task = task
        data = _trim_utf8(task.encode("utf-8")[:63])
        if await self._write(self._task_char, data):
            log.info("task sent: %s", task)

    async def send_usage(self, tokens: int):
        """Send today's token usage over the command char without changing state."""
        if tokens == self._last_usage:
            return
        self._last_usage = tokens
        msg = f"USAGE {tokens}".encode("utf-8")[:63]
        if await self._write(self._command_char, msg):
            log.info("usage sent: %s tokens", tokens)

    async def send_balance(self, value: str):
        """Send the DeepSeek balance + wall-clock refresh time over the command
        char (e.g. "BAL 75.78 14:02:11"). Sent on every fetch (~30s) so the
        pet can show when the balance was last refreshed."""
        ts = time.strftime("%H:%M:%S")
        msg = f"BAL {value} {ts}".encode("utf-8")[:63]
        if await self._write(self._command_char, msg):
            log.info("balance sent: %s @ %s", value, ts)

    async def send_agents(self, text: str):
        """Send the per-agent status list over the command char
        (e.g. "AGENTS codex-3f2:1;dsh-CodexPet-0b47:1" where 1=WORKING).
        Longer than the 63-byte command cap: NimBLE/CoreBluetooth negotiate a
        185-byte ATT MTU, so an 8-agent list (~150 B) fits in one write.
        Dedups per connection (reset on reconnect so a fresh link always gets
        the current list even if the text didn't change)."""
        if text == self._last_agents:
            return
        self._last_agents = text
        msg = _trim_utf8(f"AGENTS {text}".encode("utf-8")[:180])
        if await self._write(self._command_char, msg):
            log.info("agents sent: %s", text)

    async def _send_packet(self, state: dict):
        if self._client is None or not self._client.is_connected:
            return
        ts = int(state.get("timestamp", time.time()))
        progress = state.get("progress", 255)
        if not (isinstance(progress, int) and 0 <= progress <= 100):
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
        if await self._write(self._state_char, packet):
            log.debug("sent state=%s", state.get("state"))
