"""Runtime memory scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class HeapStats:
    allocated_objects: int = 0
