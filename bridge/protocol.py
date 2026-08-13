"""CodexPet BLE protocol helpers."""
from __future__ import annotations

import struct
import time

PROTOCOL_VERSION = 0x01

SERVICE_UUID = "CDD4DFFB-2FD0-4F2D-9A87-C7D2535B59E0"
STATE_CHAR_UUID = "DA3CDABA-E192-460B-ACF3-B2C59C6A3EE0"
STATUS_CHAR_UUID = "7497D0A0-FE42-4E2D-B28E-93D083A7CD68"
COMMAND_CHAR_UUID = "7C9DA1DE-8FE4-4B95-A366-51EA94E3010C"
TASK_CHAR_UUID = "9578BE09-6CA5-4221-8F83-E26996838F86"

STATE_ENUM = {
    "OFFLINE": 0,
    "IDLE": 1,
    "WORKING": 2,
    "WAITING": 3,
    "COMPLETED": 4,
    "ERROR": 5,
    "SLEEP": 6,
}

MOOD_ENUM = {
    "NORMAL": 0,
    "FOCUSED": 1,
    "HAPPY": 2,
    "CONFUSED": 3,
    "SAD": 4,
    "SLEEPY": 5,
}


def build_state_packet(
    state: str,
    progress: int = 255,
    mood: str = "NORMAL",
    animation: int = 0,
    flags: int = 0,
    timestamp: int | None = None,
) -> bytes:
    """Build the 10-byte binary state packet used by the firmware."""
    if state not in STATE_ENUM:
        raise ValueError(f"unknown state: {state}")
    if mood not in MOOD_ENUM:
        raise ValueError(f"unknown mood: {mood}")
    if not (0 <= progress <= 100 or progress == 255):
        raise ValueError("progress must be 0-100 or 255 (invalid)")
    ts = timestamp if timestamp is not None else int(time.time())
    return struct.pack(
        "<BBBBBBI",
        PROTOCOL_VERSION,
        STATE_ENUM[state],
        progress,
        MOOD_ENUM[mood],
        animation,
        flags,
        ts,
    )
