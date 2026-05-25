"""Name binding scaffolding."""

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class BindResult:
    hir: Any
    errors: list[str]


def bind_module(ast: Any) -> BindResult:
    """Stub binder: passthrough AST for now."""
    return BindResult(hir=ast, errors=[])
