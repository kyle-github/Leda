"""Lexer scaffolding.

Ticket target:
- Implement tokenization with source spans.
"""

from dataclasses import dataclass
from typing import Iterable


@dataclass(slots=True)
class Token:
    kind: str
    text: str
    line: int
    col: int


def lex(source: str) -> Iterable[Token]:
    """Stub lexer: returns no tokens yet."""
    _ = source
    return []
