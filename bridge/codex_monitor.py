"""Detect Codex activity via official hooks + JSONL session logs."""
from __future__ import annotations

import asyncio
import json
import logging
import os
import time
from pathlib import Path
from typing import Callable, Coroutine

from hook_server import HookServer

log = logging.getLogger("codex-monitor")

HOOK_EVENT_TO_STATE = {
    "SessionStart": "IDLE",
    "UserPromptSubmit": "WORKING",
    "PreToolUse": "WORKING",
    "PostToolUse": "WORKING",
    "PermissionRequest": "WAITING",
    "Stop": "COMPLETED",
    "PostToolUseFailure": "ERROR",
    "StopFailure": "ERROR",
}

JSONL_EVENT_TO_STATE = {
    "event_msg:task_started": "WORKING",
    "event_msg:user_message": "WORKING",
    "event_msg:agent_message": "WORKING",
    "event_msg:agent_reasoning": "WORKING",
    "event_msg:turn_aborted": "IDLE",
    "event_msg:task_complete": "COMPLETED",
    "event_msg:context_compacted": "IDLE",
    "response_item:reasoning": "WORKING",
    "response_item:message": "WORKING",
    "response_item:function_call": "WORKING",
    "response_item:custom_tool_call": "WORKING",
    "response_item:web_search_call": "WORKING",
    "response_item:function_call_output": "WORKING",
    "response_item:custom_tool_call_output": "WORKING",
}

COMPLETED_HOLD_MS = 3000
SLEEP_AFTER_IDLE_MS = 300000  # 5 min idle -> SLEEP (Phase 5)

StateCallback = Callable[[dict], Coroutine[None, None, None]]


class CodexMonitor:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self._cb: StateCallback | None = None
        self._state = "IDLE"
        self._last_activity = time.monotonic()
        self._hook_seen = False
        self._session_dir = Path(cfg["monitor"]["session_dir"])
        self._tracked: dict[str, int] = {}  # file -> read offset
        self._completed_at = 0.0
        self._current_task = ""

    def set_callback(self, cb: StateCallback):
        self._cb = cb

    async def run(self, stop_event: asyncio.Event):
        hook = HookServer(
            "127.0.0.1",
            int(self.cfg["monitor"].get("hook_port", 17384)),
            self._on_hook,
        )
        tasks = [
            asyncio.create_task(hook.run(stop_event)),
            asyncio.create_task(self._poll_loop(stop_event)),
        ]
        await asyncio.gather(*tasks, return_exceptions=True)

    async def _poll_loop(self, stop_event: asyncio.Event):
        interval = float(self.cfg["monitor"].get("interval", 1))
        while not stop_event.is_set():
            try:
                self._poll_sessions()
                await self._maybe_transition()
            except Exception as e:
                log.warning("poll error: %s", e)
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=interval)
            except asyncio.TimeoutError:
                pass

    async def _on_hook(self, payload: dict):
        event = payload.get("event", "")
        state = HOOK_EVENT_TO_STATE.get(event)
        if state is None:
            return
        self._hook_seen = True
        self._last_activity = time.monotonic()
        # tool events keep the user's task title instead of overwriting it
        if event in ("PreToolUse", "PostToolUse") and self._current_task:
            task = self._current_task
        else:
            task = self._extract_hook_task(payload)
            if task:
                self._current_task = task
        log.info("hook -> %s (event=%s task=%s)", state, event, task)
        await self._apply_state(state, source="hook")

    def _poll_sessions(self):
        if not self._session_dir.exists():
            return
        for day_dir in self._recent_dirs():
            if not day_dir.exists():
                continue
            for f in sorted(day_dir.glob("rollout-*.jsonl")):
                self._poll_file(f)

    def _poll_file(self, path: Path):
        try:
            size = path.stat().st_size
        except OSError:
            return
        offset = self._tracked.get(str(path), size)  # skip history, watch new writes
        if size < offset:
            offset = 0
        if size == offset:
            return
        try:
            with path.open("r", encoding="utf-8") as f:
                f.seek(offset)
                new_lines = f.read()
                self._tracked[str(path)] = f.tell()
        except OSError:
            return

        for line in new_lines.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            key = f"{rec.get('type')}:{rec.get('payload', {}).get('type', '')}"
            state = JSONL_EVENT_TO_STATE.get(key)
            if state:
                self._last_activity = time.monotonic()
                log.debug("jsonl %s -> %s", key, state)
                if state == "COMPLETED":
                    self._completed_at = time.monotonic()
                    self._current_task = ""
                task = self._extract_jsonl_task(rec.get("payload", {}))
                if task:
                    self._current_task = task
                # schedule immediate state application
                asyncio.get_event_loop().create_task(self._apply_state(state, source="jsonl"))

    def _recent_dirs(self) -> list[Path]:
        base = self._session_dir
        out: list[Path] = []
        now = time.localtime()
        for days_ago in range(3):
            t = time.localtime(time.time() - days_ago * 86400)
            out.append(base / f"{t.tm_year:04d}" / f"{t.tm_mon:02d}" / f"{t.tm_mday:02d}")
        # include any dir modified in the last 2 days
        if base.exists():
            for year in base.iterdir():
                if not year.is_dir() or not year.name.isdigit():
                    continue
                for month in year.iterdir():
                    if not month.is_dir():
                        continue
                    for day in month.iterdir():
                        if day.is_dir() and day not in out and day.stat().st_mtime > time.time() - 172800:
                            out.append(day)
        return out

    @staticmethod
    def _first_line(text: str, limit: int = 48) -> str:
        text = (text or "").strip()
        if not text:
            return ""
        first = text.splitlines()[0].strip()
        return first[:limit]

    def _extract_hook_task(self, payload: dict) -> str:
        inner = payload.get("payload") or {}
        if isinstance(inner, str):
            try:
                inner = json.loads(inner)
            except json.JSONDecodeError:
                inner = {}
        if not isinstance(inner, dict):
            inner = {}
        text = inner.get("prompt") or inner.get("text") or payload.get("text") or payload.get("prompt") or ""
        if text:
            return self._first_line(text)
        cwd = inner.get("cwd") or payload.get("cwd") or ""
        if cwd:
            return Path(cwd).name
        return ""

    def _extract_jsonl_task(self, payload: dict) -> str:
        if not isinstance(payload, dict):
            return ""
        ptype = payload.get("type", "")
        if ptype not in ("user_message", "agent_message"):
            return ""
        text = ""
        if isinstance(payload.get("text"), str):
            text = payload["text"]
        elif isinstance(payload.get("message"), str):
            text = payload["message"]
        elif isinstance(payload.get("content"), list):
            parts = []
            for item in payload["content"]:
                if isinstance(item, dict):
                    if isinstance(item.get("text"), str):
                        parts.append(item["text"])
                    elif isinstance(item.get("output_text"), str):
                        parts.append(item["output_text"])
            text = " ".join(parts)
        return self._first_line(text)

    async def _maybe_transition(self):
        now = time.monotonic()
        # COMPLETED -> IDLE after hold
        if self._state == "COMPLETED" and self._completed_at and now - self._completed_at >= COMPLETED_HOLD_MS / 1000:
            await self._apply_state("IDLE", source="timer")
        # IDLE -> SLEEP after long idle (Phase 5 behavior)
        if self._state == "IDLE" and now - self._last_activity >= SLEEP_AFTER_IDLE_MS / 1000:
            await self._apply_state("SLEEP", source="timer")

    async def _apply_state(self, state: str, source: str):
        if state == self._state:
            return
        self._state = state
        if self._cb:
            await self._cb({
                "state": state,
                "progress": 0,
                "task": self._current_task,
                "timestamp": int(time.time()),
            })
        log.info("state -> %s (%s)", state, source)
