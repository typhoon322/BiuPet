"""Detect DeepSeek Harness (DSH) activity from its session JSONL (zstd)."""
from __future__ import annotations

import asyncio
import json
import logging
import time
from pathlib import Path
from typing import Callable, Coroutine

import zstandard

log = logging.getLogger("dsh-monitor")

EVENT_TO_STATE = {
    "turn/start": "WORKING",
    "user/message": "WORKING",
    "assistant/message": "WORKING",
    "assistant/chunk": "WORKING",
    "tool/call": "WORKING",
    "tool/result": "WORKING",
    "step/start": "WORKING",
    "step/end": "WORKING",
    "reasoning-chunks": "WORKING",
    "approval/asked": "WAITING",
    "turn/end": "COMPLETED",
}

COMPLETED_HOLD_S = 15.0

StateCallback = Callable[[dict], Coroutine[None, None, None]]


def _decompress(path: Path) -> str:
    """Decompress a (possibly still-growing) zstd JSONL file, tolerating truncation."""
    with path.open("rb") as f:
        reader = zstandard.ZstdDecompressor().stream_reader(f)
        data = bytearray()
        try:
            while True:
                chunk = reader.read(65536)
                if not chunk:
                    break
                data += chunk
        except zstandard.ZstdError:
            pass  # in-progress/truncated stream: use what we decoded
    return data.decode("utf-8", "replace")


class DshMonitor:
    def __init__(self, session_dir: str):
        self._session_dir = Path(session_dir)
        self._cb: StateCallback | None = None
        self._state = "IDLE"
        self._task = ""
        self._completed_at = 0.0
        self._last_activity = time.monotonic()
        self._tracked: dict[str, tuple[int, int]] = {}  # path -> (size, lines_processed)

    def set_callback(self, cb: StateCallback):
        self._cb = cb

    async def run(self, stop_event: asyncio.Event):
        while not stop_event.is_set():
            try:
                self._poll()
                await self._maybe_hold()
            except Exception as e:  # noqa: BLE001
                log.warning("dsh poll error: %s", e)
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=1.0)
            except asyncio.TimeoutError:
                pass

    def _poll(self):
        if not self._session_dir.exists():
            return
        for f in self._session_dir.rglob("session.jsonl.zstd"):
            self._poll_file(f)

    def _poll_file(self, path: Path):
        try:
            size = path.stat().st_size
        except OSError:
            return
        key = str(path)
        if key not in self._tracked:
            self._tracked[key] = (size, 0)  # baseline: only watch new writes
            return
        old_size, lines = self._tracked[key]
        if size <= old_size:
            return
        text = _decompress(path)
        all_lines = text.splitlines()
        for line in all_lines[lines:]:
            self._process_line(line)
        self._tracked[key] = (size, len(all_lines))

    def _process_line(self, line: str):
        line = line.strip()
        if not line:
            return
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            return
        event = rec.get("type", "")
        state = EVENT_TO_STATE.get(event)
        if state is None:
            return
        self._last_activity = time.monotonic()
        task = self._extract_task(rec) or self._task
        if task:
            self._task = task
        log.debug("dsh %s -> %s", event, state)
        if state == "COMPLETED":
            self._completed_at = time.monotonic()
        elif state != self._state:
            asyncio.get_event_loop().create_task(self._apply(state, task))

    @staticmethod
    def _extract_task(rec: dict) -> str:
        data = rec.get("data") or {}
        if not isinstance(data, dict):
            return ""
        content = data.get("content")
        if isinstance(content, list):
            parts = []
            for item in content:
                if isinstance(item, dict) and isinstance(item.get("text"), str):
                    parts.append(item["text"])
            text = " ".join(parts)
        elif isinstance(data.get("text"), str):
            text = data["text"]
        else:
            return ""
        first = (text or "").strip().splitlines()
        return first[0][:48] if first else ""

    async def _apply(self, state: str, task: str):
        if state == self._state:
            return
        self._state = state
        if self._cb:
            await self._cb({
                "state": state,
                "progress": 0,
                "task": self._task,
                "timestamp": int(time.time()),
            })

    async def _maybe_hold(self):
        if self._state == "COMPLETED" and time.monotonic() - self._completed_at >= COMPLETED_HOLD_S:
            await self._apply("IDLE", self._task)
