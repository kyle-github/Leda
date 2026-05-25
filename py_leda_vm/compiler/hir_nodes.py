"""HIR node placeholders."""

from dataclasses import dataclass, field
from typing import Any


@dataclass(slots=True)
class HirNode:
    kind: str
    span: tuple[int, int, int, int] = (0, 0, 0, 0)
    result_type_id: int | None = None
    fields: dict[str, Any] = field(default_factory=dict)
