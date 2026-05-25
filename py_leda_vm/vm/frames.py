"""Frame model scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class Frame:
    ip: int
    fp: int
    cp: int
    ce: int
    dp: int
    caller_frame_index: int | None
