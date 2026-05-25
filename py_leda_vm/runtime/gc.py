"""GC scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class GCStats:
    collections: int = 0


def collect() -> GCStats:
    return GCStats(collections=1)
