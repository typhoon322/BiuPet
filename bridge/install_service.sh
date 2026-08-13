#!/bin/bash
# Install CodexPet bridge as a login LaunchAgent (auto-start, keep-alive).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLIST_SRC="$ROOT/bridge/com.typhoon322.codexpet.plist"
PLIST_DST="$HOME/Library/LaunchAgents/com.typhoon322.codexpet.plist"
PYTHON="$ROOT/.venv/bin/python3"
LOG="$HOME/Library/Logs/codexpet-bridge.log"

mkdir -p "$HOME/Library/LaunchAgents"
sed -e "s|__PYTHON__|$PYTHON|g" \
    -e "s|__BRIDGE_MAIN__|$ROOT/bridge/main.py|g" \
    -e "s|__BRIDGE_DIR__|$ROOT/bridge|g" \
    -e "s|__LOG__|$LOG|g" \
    "$PLIST_SRC" > "$PLIST_DST"

# unload old instance if present, then load
launchctl bootout "gui/$(id -u)" "$PLIST_DST" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST_DST"
launchctl enable "gui/$(id -u)/com.typhoon322.codexpet"
echo "[service] installed: $PLIST_DST"
echo "[service] log: $LOG"
