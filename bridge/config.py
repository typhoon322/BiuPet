"""Load bridge configuration."""
import os
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None

DEFAULT_CFG = Path(__file__).with_name("config.yaml")
LOCAL_CFG = Path(__file__).with_name("config.local.yaml")


def _expand(path: str) -> Path:
    return Path(os.path.expandvars(os.path.expanduser(path)))


def _deep_merge(base: dict, override: dict) -> dict:
    """Recursively merge override into base (override wins at every level)."""
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(base.get(k), dict):
            _deep_merge(base[k], v)
        else:
            base[k] = v
    return base


def load_config(path: Path | None = None) -> dict[str, Any]:
    cfg_path = path or DEFAULT_CFG
    if yaml is None:
        raise RuntimeError("PyYAML is required: pip install pyyaml")
    with cfg_path.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}
    local = path.parent / "config.local.yaml" if path else LOCAL_CFG
    if local.exists():
        with local.open("r", encoding="utf-8") as f:
            local_cfg = yaml.safe_load(f) or {}
        cfg = _deep_merge(cfg, local_cfg)
    cfg["monitor"]["session_dir"] = _expand(cfg["monitor"]["session_dir"])
    return cfg
