"""Symbol table scaffolding."""

from dataclasses import dataclass, field


@dataclass(slots=True)
class Symbol:
    symbol_id: int
    name: str
    kind: str


@dataclass(slots=True)
class Scope:
    scope_id: int
    parent_scope_id: int | None
    symbols: dict[str, Symbol] = field(default_factory=dict)
