"""Parse AST node placeholders."""

from dataclasses import dataclass, field
from typing import Any


@dataclass(slots=True)
class AstNode:
    kind: str
    span: tuple[int, int, int, int] = (0, 0, 0, 0)
    fields: dict[str, Any] = field(default_factory=dict)
