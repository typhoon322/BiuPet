"""Load bridge configuration."""
import os
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None

DEFAULT_CFG = Path(__file__).with_name("config.yaml")


def _expand(path: str) -> Path:
    return Path(os.path.expandvars(os.path.expanduser(path)))


def load_config(path: Path | None = None) -> dict[str, Any]:
    cfg_path = path or DEFAULT_CFG
    if yaml is None:
        raise RuntimeError("PyYAML is required: pip install pyyaml")
    with cfg_path.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}
    cfg["monitor"]["session_dir"] = _expand(cfg["monitor"]["session_dir"])
    return cfg
