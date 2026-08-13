"""Track Codex usage (tokens) from local JSONL session logs."""
from __future__ import annotations

import json
import logging
import time
from pathlib import Path

log = logging.getLogger("usage-tracker")


class UsageTracker:
    def __init__(self, session_dir: str):
        self._base = Path(session_dir)
        # path -> (file size at scan, last token count); only rescans on growth
        self._cache: dict[str, tuple[int, int]] = {}

    def today_tokens(self) -> int:
        total = 0
        now = time.localtime()
        day_dir = self._base / f"{now.tm_year:04d}" / f"{now.tm_mon:02d}" / f"{now.tm_mday:02d}"
        if not day_dir.exists():
            return 0
        for path in day_dir.glob("rollout-*.jsonl"):
            total += self._session_tokens(path)
        return total

    def _session_tokens(self, path: Path) -> int:
        try:
            size = path.stat().st_size
        except OSError:
            return 0
        cached = self._cache.get(str(path))
        if cached is not None and cached[0] == size:
            return cached[1]
        last = 0
        try:
            with path.open("r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        rec = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if rec.get("type") != "event_msg":
                        continue
                    payload = rec.get("payload") or {}
                    if payload.get("type") != "token_count":
                        continue
                    info = payload.get("info") or {}
                    usage = info.get("total_token_usage") or {}
                    tok = usage.get("total_tokens")
                    if isinstance(tok, int):
                        last = tok
        except OSError:
            return 0
        self._cache[str(path)] = (size, last)
        return last
