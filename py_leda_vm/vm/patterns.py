"""Pattern matching scaffolding."""

from typing import Any


def match_kind(value: Any, expected_kind: str) -> bool:
    _ = expected_kind
    return value is not None
