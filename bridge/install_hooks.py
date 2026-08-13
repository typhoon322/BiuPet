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
    if "[features]" not in text:
        text += "\n[features]\nhooks = true\n"
    else:
        # replace any hooks=false / hooks = false line inside [features]
        lines = text.splitlines()
        out = []
        in_features = False
        for line in lines:
            if line.strip() == "[features]":
                in_features = True
            elif line.strip().startswith("["):
                in_features = False
            if in_features and "hooks" in line and "=" in line:
                continue
            out.append(line)
        text = "\n".join(out) + "\n[features]\nhooks = true\n"
    config_path.write_text(text, encoding="utf-8")
    print(f"[install] ensured [features] hooks=true in {config_path}")
    print("[install] done. Restart Codex or run /hooks to approve the new commands.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
