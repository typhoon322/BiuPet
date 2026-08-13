"""Detect Codex activity via official hooks and JSONL session logs."""
from __future__ import annotations

import asyncio
import json
import logging
import os
import time
from pathlib import Path
from typing import Callable, Coroutine

log = logging.getLogger("codex-monitor")

HOOK_EVENT_TO_STATE = {
    "SessionStart": "IDLE",
    "UserPromptSubmit": "WORKING",
    "PreToolUse": "WORKING",
    "PermissionRequest": "WAITING",
    "PostToolUse": "WORKING",
    "Stop": "COMPLETED",
}

JSONL_EVENT_TO_STATE = {
    "event_msg:task_started": "WORKING",
    "event_msg:user_message": "WORKING",
    "response_item:reasoning": "WORKING",
    "response_item:function_call": "WORKING",
    "response_item:custom_tool_call": "WORKING",
    "response_item:web_search_call": "WORKING",
    "event_msg:task_complete": "COMPLETED",
    "event_msg:turn_aborted": "IDLE",
}

StateCallback = Callable[[dict], Coroutine[None, None, None]]


class CodexMonitor:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self._cb: StateCallback | None = None
        self._last_state = "IDLE"
        self._last_activity = 0.0

    def set_callback(self, cb: StateCallback):
        self._cb = cb

    async def run(self, stop_event: asyncio.Event):
        log.info("Codex monitor started")
        while not stop_event.is_set():
            # TODO: implement hook server + JSONL tail
            state = self._derive_state()
            if state != self._last_state:
                self._last_state = state
                await self._emit(state)
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=self.cfg["monitor"]["interval"])
            except asyncio.TimeoutError:
                pass

    def _derive_state(self) -> str:
        now = time.time()
        idle_to_sleep = self.cfg["monitor"]["idle_to_sleep_sec"]
        if now - self._last_activity > idle_to_sleep and self._last_state == "IDLE":
            return "SLEEP"
        return self._last_state

    async def _emit(self, state: str):
        if self._cb:
            await self._cb({"state": state, "progress": 0, "task": "", "timestamp": int(time.time())})

    def on_hook_event(self, event: str, payload: dict):
        state = HOOK_EVENT_TO_STATE.get(event, "IDLE")
        log.info("hook event=%s -> %s", event, state)
        self._last_activity = time.time()
        self._last_state = state
        # Completed returns to IDLE after a short celebration.
        if state == "COMPLETED":
            asyncio.get_event_loop().call_later(3.0, self._force_idle)

    def _force_idle(self):
        if self._last_state == "COMPLETED":
            self._last_state = "IDLE"

    def on_jsonl_event(self, key: str, payload: dict):
        state = JSONL_EVENT_TO_STATE.get(key)
        if state:
            log.debug("jsonl %s -> %s", key, state)
            self._last_activity = time.time()
            self._last_state = state
            if state == "COMPLETED":
                asyncio.get_event_loop().call_later(3.0, self._force_idle)
