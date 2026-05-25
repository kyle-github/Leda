"""LBC reader scaffolding."""

from pathlib import Path


def read_image(path: Path) -> bytes:
    """Stub reader: returns raw bytes."""
    return path.read_bytes()
