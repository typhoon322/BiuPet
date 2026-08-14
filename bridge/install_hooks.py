#!/usr/bin/env python3
"""Non-destructively install Codex hooks that forward events to the bridge."""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

HOOK_SCRIPT = str(Path(__file__).resolve().parent / "hook.sh")

EVENTS = [
    "SessionStart",
    "UserPromptSubmit",
    "PreToolUse",
    "PostToolUse",
    "PermissionRequest",
    "Stop",
]


def load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}


def _is_hooks_key(line: str) -> bool:
    """True if this line is a `hooks = ...` key (not a comment)."""
    s = line.lstrip()
    if not s or s.startswith("#"):
        return False
    return s.split("=", 1)[0].strip() == "hooks"


def ensure_features_hooks(text: str) -> str:
    """Insert `hooks = true` inside the existing [features] table (or append
    a new one), without ever emitting a duplicate [features] header."""
    lines = text.splitlines()

    # Locate the exact [features] table header (not [features.*]).
    header = next((i for i, ln in enumerate(lines) if ln.strip() == "[features]"), None)

    if header is None:
        if lines and lines[-1] != "":
            lines.append("")
        lines += ["[features]", "hooks = true", ""]
        return "\n".join(lines) + "\n"

    # End of the section = next top-level table header, or EOF.
    end = len(lines)
    for i in range(header + 1, len(lines)):
        s = lines[i].lstrip()
        if s.startswith("[") and s.endswith("]"):
            end = i
            break

    section = [ln for ln in lines[header:end] if not _is_hooks_key(ln)]
    section.insert(1, "hooks = true")  # right after the [features] header
    return "\n".join(lines[:header] + section + lines[end:]) + "\n"


def main() -> int:
    codex_dir = Path.home() / ".codex"
    hooks_path = codex_dir / "hooks.json"
    config_path = codex_dir / "config.toml"

    cfg = load_json(hooks_path)
    hooks = cfg.setdefault("hooks", {})

    added = 0
    for event in EVENTS:
        entries = hooks.get(event, [])
        # remove existing entries managed by us
        entries = [e for e in entries if HOOK_SCRIPT not in json.dumps(e)]
        entries.append({
            "hooks": [{
                "type": "command",
                "command": f'"{HOOK_SCRIPT}" {event}',
                "timeout": 30,
            }]
        })
        hooks[event] = entries
        added += 1

    hooks_path.write_text(json.dumps(cfg, indent=2) + "\n", encoding="utf-8")
    print(f"[install] wrote {hooks_path}")

    # ensure [features] hooks = true
    text = config_path.read_text(encoding="utf-8") if config_path.exists() else ""
    text = ensure_features_hooks(text)
    config_path.write_text(text, encoding="utf-8")
    print(f"[install] ensured [features] hooks=true in {config_path}")
    print("[install] done. Restart Codex or run /hooks to approve the new commands.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
