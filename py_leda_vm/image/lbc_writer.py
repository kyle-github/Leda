"""LBC writer scaffolding."""

from pathlib import Path
from typing import Any


def write_image(path: Path, model: Any) -> None:
    """Stub writer: emits placeholder bytes for now."""
    _ = model
    path.write_bytes(b"LBC0\x01\x00")
