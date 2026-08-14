# DSH 接入 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 DSH（DeepSeek Harness）与 Codex 同时驱动宠物：新增 `dsh_monitor.py` 增量 tail DSH 的 session JSONL（zstd），并在 bridge 加 StateHub 聚合两个 monitor。

**Architecture:** DSH 会话写在 `~/.dsh/sessions/**/session.jsonl.zstd`（zstd 压缩 JSONL），事件类型干净映射到宠物状态；两个 monitor 各上报状态，StateHub 按「最近活动优先」合并，空闲→SLEEP 的定时器上移到 StateHub（基于**两个源的综合活动**），避免 Codex 空闲覆盖 DSH 工作。

**Tech Stack:** Python 3.13、`zstandard`（zstd 解压）、现有 asyncio bridge。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `bridge/dsh_monitor.py` | 新增：tail DSH session.jsonl.zstd + 事件映射 + 任务文本提取 |
| `bridge/main.py` | 加 StateHub，聚合 Codex + DSH，空闲定时器上移 |
| `bridge/codex_monitor.py` | 移除自身 IDLE→SLEEP 定时器（交给 StateHub） |
| `bridge/requirements.txt` | 加 `zstandard` |

---

## Task 1: 安装 zstandard

- [ ] **Step 1:** 在 `bridge/requirements.txt` 追加一行 `zstandard>=0.22.0`。

- [ ] **Step 2:** 安装（走代理）：
  Run: `.venv/bin/pip install -i https://pypi.org/simple zstandard`（若网络被墙，加 `--proxy http://127.0.0.1:7897`）
  Expected: 安装成功，`.venv/bin/python3 -c "import zstandard; print(zstandard.__version__)"` 正常。

- [ ] **Step 3:** 提交 `requirements.txt`。

---

## Task 2: dsh_monitor.py

**Files:** Create `bridge/dsh_monitor.py`

- [ ] **Step 1:** 创建文件，内容如下（EXACTLY）：

```python
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
```

- [ ] **Step 2:** 语法检查：`.venv/bin/python3 -m py_compile bridge/dsh_monitor.py`。

- [ ] **Step 3:** 用真实 DSH 会话文件验证解析：运行一个小脚本，对 `~/.dsh/sessions/**/session.jsonl.zstd` 跑 `DshMonitor._poll()`，确认能打印出 WORKING/COMPLETED 事件（不连接 BLE）。

---

## Task 3: StateHub + codex_monitor 定时器上移

**Files:** Modify `bridge/main.py`, `bridge/codex_monitor.py`

- [ ] **Step 1:** 在 `main.py` 加 StateHub 类并重构 `main()`：

```python
class StateHub:
    def __init__(self, send_fn):
        self._send = send_fn
        self._state = "IDLE"
        self._last_activity = time.monotonic()

    async def report(self, state: dict):
        self._last_activity = time.monotonic()
        s = state.get("state", "IDLE")
        if s != self._state:
            self._state = s
            await self._send(state)

    async def idle_loop(self, stop_event):
        while not stop_event.is_set():
            await asyncio.sleep(1)
            if self._state == "IDLE" and time.monotonic() - self._last_activity >= 300:
                await self.report({"state": "SLEEP", "progress": 0, "task": "", "timestamp": int(time.time())})
```

在 `main()` 里：`hub = StateHub(bridge.send_state)`；`monitor.set_callback(hub.report)`；`dsh.set_callback(hub.report)`；`asyncio.gather(...)` 加入 `hub.idle_loop(stop_event)`。

- [ ] **Step 2:** 在 `main.py` 引入 `DshMonitor`，读 `config.yaml` 的 `dsh.session_dir`（默认 `~/.dsh/sessions`）。

- [ ] **Step 3:** 在 `codex_monitor.py` 删除 `_maybe_transition` 里的 IDLE→SLEEP 分支（保留 COMPLETED→IDLE hold）：

把：
```python
        # IDLE -> SLEEP after long idle (Phase 5 behavior)
        if self._state == "IDLE" and now - self._last_activity >= SLEEP_AFTER_IDLE_MS / 1000:
            await self._apply_state("SLEEP", source="timer")
```
删掉（`SLEEP_AFTER_IDLE_MS` 常量一并删）。

- [ ] **Step 4:** 在 `config.yaml` 加 `dsh:` 段：`dsh:\n  session_dir: ~/.dsh/sessions`。

- [ ] **Step 5:** 语法检查 + 冒烟：`python3 -m py_compile bridge/*.py`。

- [ ] **Step 6:** 提交。

---

## Task 4: 端到端验收

- [ ] **Step 1:** 启动 bridge（`launchctl` 重启），用一个真实 DSH 会话（比如当前正在跑的这个会话）验证：串口/日志应出现 `monitor -> WORKING`、任务文本、`COMPLETED`。

- [ ] **Step 2:** 同时让 Codex 也活动，确认状态不打架（最近活动优先、空闲才 SLEEP）。

- [ ] **Step 3:** 确认中文任务文本推送到设备任务行正常。

- [ ] **Step 4:** 提交微调。

---

## 自检

- **DSH JSONL tail + 事件映射**：Task 2 的 `DshMonitor`。✅
- **zstd 解压**：Task 1 `zstandard` + Task 2 `_decompress`（容忍截断）。✅
- **多源合并**：Task 3 `StateHub`（最近活动优先 + 空闲定时器上移）。✅
- **codex_monitor 定时器上移**：Task 3 Step 3。✅
- **固件不动**：无固件改动任务。✅
