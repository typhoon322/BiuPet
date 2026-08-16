"""Detect DeepSeek Harness (DSH) *session* activity from its session JSONL.

Each DSH session is a directory  sessions/<slug>/session-<uuid>/  containing a
growing zstd JSONL event stream.  The first event ("session") records the
workspace cwd and delegation depth (0 = top-level session, 1 = subagent).

We show one entry per top-level session, named after its workspace folder
(e.g. "CodexPet"), and derive its status from the event stream:

  turn/start ... step/end  -> 工作中 (WORKING)
  approval/asked           -> 需要审批 (WAITING)
  turn/end                 -> 已完成 (COMPLETED, becomes 空闲 after a short hold)
  no recent turn           -> 空闲 (IDLE)
"""
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

COMPLETED_HOLD_S = 10.0        # 已完成 -> 空闲 after this long (stop celebrating)
WORKING_STALE_S = 45.0         # WORKING with no new event this long => finished

StateCallback = Callable[[dict], Coroutine[None, None, None]]


def _today() -> str:
    return time.strftime("%Y-%m-%d")


def _active_on_today(ts: float) -> bool:
    """Only sessions that had activity today (local time) are shown."""
    return ts > 0 and time.strftime("%Y-%m-%d", time.localtime(ts)) == _today()


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
        self._meta: dict[str, dict] = {}                # path -> {label, depth}
        self._agents: dict[str, dict] = {}              # agent_id -> {state, task, ts, label, depth}

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
        agent_id = self._agent_id_for(path)
        if key not in self._tracked:
            # baseline: decompress once, record the session meta, and replay the
            # whole stream so a freshly restarted bridge already knows each
            # session's latest state (17 MB files are only decompressed here).
            text = _decompress(path)
            all_lines = text.splitlines()
            meta = self._scan_meta(all_lines, path)
            self._meta[key] = meta
            for line in all_lines:
                self._process_line(line, agent_id, meta)
            self._tracked[key] = (size, len(all_lines))
            return
        old_size, lines = self._tracked[key]
        if size <= old_size:
            return
        text = _decompress(path)
        all_lines = text.splitlines()
        meta = self._meta.get(key) or self._scan_meta(all_lines, path)
        self._meta[key] = meta
        for line in all_lines[lines:]:
            self._process_line(line, agent_id, meta)
        self._tracked[key] = (size, len(all_lines))

    @staticmethod
    def _scan_meta(lines: list[str], path: Path) -> dict:
        """Workspace name + delegation depth from the first 'session' event."""
        label = path.parent.parent.name.strip("-").split("-")[-1] or "dsh"
        depth = 1   # conservative: hidden unless we see a top-level session event
        for ln in lines[:500]:
            try:
                rec = json.loads(ln)
            except Exception:
                continue
            if rec.get("type") != "session":
                continue
            # cwd / delegationDepth sit at the top level of the session event
            data = rec.get("data") or {}
            cwd = rec.get("cwd") or data.get("cwd") or ""
            if cwd:
                label = Path(cwd).name or label
            d = rec.get("delegationDepth")
            if d is None:
                d = data.get("delegationDepth")
            depth = int(d) if d is not None else 1
            break
        return {"label": label, "depth": depth}

    def _process_line(self, line: str, agent_id: str, meta: dict):
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
        agent = self._agents.setdefault(agent_id, {
            "state": state, "task": "", "ts": 0.0,
            "label": meta.get("label", "dsh"), "depth": meta.get("depth", 1),
        })
        agent["label"] = meta.get("label", agent["label"])
        agent["depth"] = meta.get("depth", agent["depth"])
        t = rec.get("time")
        agent["ts"] = (t / 1000.0) if isinstance(t, (int, float)) and t > 0 else time.time()
        # a session's task = its latest user message (assistant replies are
        # not tasks, they'd overwrite the real request with "好的，我来处理…")
        if event == "user/message":
            task = self._extract_task(rec)
            if task:
                agent["task"] = task
                self._task = task
        # the agent's own state updates on EVERY event; the global _state gate
        # below only controls the pet-animation callback, otherwise a second
        # session whose event equals the global state would never update
        # (e.g. two sessions finishing -> the second stays WORKING forever)
        agent["state"] = state
        log.debug("dsh %s -> %s", event, state)
        if state == "COMPLETED":
            agent["completed_at"] = time.time()
        if state != self._state:
            asyncio.get_event_loop().create_task(
                self._apply(state, agent.get("task", "") or self._task, agent_id))

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

    @staticmethod
    def _agent_id_for(path: Path) -> str:
        """Internal unique id for a session (the pet only ever sees the label)."""
        tail = path.parent.name
        if tail.startswith("session-"):
            tail = tail[len("session-"):]
        return "dsh-" + tail[:8]

    async def _apply(self, state: str, task: str, agent_id: str = "dsh"):
        agent = self._agents.setdefault(agent_id, {"state": state, "task": "", "ts": 0.0})
        agent["state"] = state
        agent["task"] = task or agent.get("task", "")
        # note: agent["ts"] is owned by _process_line (real event time) and must
        # not be overwritten here, otherwise replayed history looks "now".
        if state == self._state:
            return
        self._state = state
        if self._cb:
            await self._cb({
                "state": state,
                "progress": 0,
                "task": self.current_task(),
                "timestamp": int(time.time()),
            })

    def agents_snapshot(self) -> list[tuple[str, str]]:
        """Top-level sessions with activity today as [(label, state)]; busy
        (working / waiting) sessions first, then by most recent activity."""
        items = []
        for aid, a in self._agents.items():
            if a.get("depth", 1) != 0:
                continue            # skip subagent sessions
            if not _active_on_today(a.get("ts", 0)):
                continue            # no activity today: don't show
            busy = 0 if a["state"] in ("WORKING", "WAITING") else 1
            items.append((busy, a["ts"], a.get("label", aid), a["state"]))
        # busy (0) first, then most recent activity first
        items.sort(key=lambda x: (x[0], -x[1]))
        return [(label, st) for _, _, label, st in items]

    def current_task(self) -> str:
        """The task text the pet should show: the newest busy session's latest
        user message, falling back to the most recent session with a task."""
        items = []
        for a in self._agents.values():
            if a.get("depth", 1) != 0:
                continue
            if not _active_on_today(a.get("ts", 0)):
                continue
            items.append((a["ts"], a))
        items.sort(reverse=True)
        for _, a in items:
            if a.get("state") == "WORKING" and a.get("task"):
                return a["task"]
        for _, a in items:
            if a.get("task"):
                return a["task"]
        return ""

    async def _maybe_hold(self):
        """Per-session housekeeping, run every second:
        - COMPLETED -> 空闲 after a short hold, so a finished session reads as
          IDLE instead of staying 已完成 forever;
        - WORKING with no new event for a while means the window was closed /
          the run stalled: mark it finished so it stops showing as busy."""
        now = time.time()
        for a in self._agents.values():
            if a.get("state") == "COMPLETED" and now - a.get("completed_at", 0) >= COMPLETED_HOLD_S:
                a["state"] = "IDLE"
            if a.get("state") == "WORKING" and now - a.get("ts", now) > WORKING_STALE_S:
                a["state"] = "COMPLETED"
                a["completed_at"] = now
