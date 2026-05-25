"""Closure runtime scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class ClosureValue:
    template_id: int
    capture_env_id: int
