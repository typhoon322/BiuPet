"""Track Codex + DSH usage (tokens) from local session logs."""
from __future__ import annotations

import json
import logging
import time
from pathlib import Path

log = logging.getLogger("usage-tracker")


class UsageTracker:
    def __init__(self, session_dir: str, dsh_storage: str | None = None):
        self._base = Path(session_dir)
        self._dsh_storage = Path(dsh_storage) if dsh_storage else None
        # path -> (file size at scan, last token count); only rescans on growth
        self._cache: dict[str, tuple[int, int]] = {}
        self._dsh_cache: tuple[float, int] = (0.0, 0)  # (mtime, tokens)

    def today_tokens(self) -> int:
        return self._codex_today_tokens() + self._dsh_today_tokens()

    def _codex_today_tokens(self) -> int:
        total = 0
        now = time.localtime()
        day_dir = self._base / f"{now.tm_year:04d}" / f"{now.tm_mon:02d}" / f"{now.tm_mday:02d}"
        if not day_dir.exists():
            return 0
        for path in day_dir.glob("rollout-*.jsonl"):
            total += self._session_tokens(path)
        return total

    def _dsh_today_tokens(self) -> int:
        if self._dsh_storage is None or not self._dsh_storage.exists():
            return 0
        try:
            mtime = self._dsh_storage.stat().st_mtime
        except OSError:
            return 0
        if self._dsh_cache[0] == mtime:
            return self._dsh_cache[1]
        total = 0
        try:
            data = json.loads(self._dsh_storage.read_text(encoding="utf-8"))
            sessions = (data.get("tables") or {}).get("sessions") or {}
            now = time.localtime()
            midnight = time.mktime((now.tm_year, now.tm_mon, now.tm_mday, 0, 0, 0, 0, 0, -1))
            midnight_ms = int(midnight * 1000)
            for s in sessions.values():
                rows = s.get("rows") or {}
                meta = rows.get("sessionListMetadata") or {}
                last = (meta.get("val") or {}).get("lastPromptAt")
                if not isinstance(last, (int, float)) or last < midnight_ms:
                    continue
                tu = rows.get("tokenUsage") or {}
                totals = (tu.get("val") or {}).get("totals") or {}
                total += int(totals.get("uncachedInputTokens", 0) or 0)
                total += int(totals.get("outputTokens", 0) or 0)
        except (OSError, json.JSONDecodeError, ValueError):
            pass
        self._dsh_cache = (mtime, total)
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
