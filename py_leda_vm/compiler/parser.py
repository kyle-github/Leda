"""Parser scaffolding.

Ticket target:
- Implement declaration/statement parser + Pratt expressions.
"""

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class ParseResult:
    ast: Any
    errors: list[str]


def parse_source(source: str) -> ParseResult:
    """Stub parser: returns empty module with no errors."""
    _ = source
    return ParseResult(ast={"kind": "Module", "body": []}, errors=[])
