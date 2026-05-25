"""Closure analysis scaffolding."""

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class CaptureDescriptor:
    source_scope_depth: int
    source_slot_kind: str
    source_slot_index: int
    capture_cell_index: int


def analyze_captures(hir: Any) -> dict[int, list[CaptureDescriptor]]:
    """Stub: no captures detected yet."""
    _ = hir
    return {}
