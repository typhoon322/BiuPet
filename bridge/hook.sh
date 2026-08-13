#!/bin/bash
# Codex hook forwarder. Called by ~/.codex/hooks.json for each event.
# Usage: hook.sh <EventName>
# Stdin: JSON payload from Codex.

set -e
EVENT="$1"
BODY=$(cat)
BRIDGE_URL="${CODEX_PET_BRIDGE:-http://127.0.0.1:17384/api/hook}"

curl -s -X POST "$BRIDGE_URL" \
  -H 'Content-Type: application/json' \
  -d "{\"agentId\":\"codex\",\"agentName\":\"Codex\",\"event\":\"$EVENT\",\"payload\":$BODY}" \
  --max-time 2 || true
