"""Debug stack trace scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class DebugFrame:
    function_name: str
    file_name: str
    line: int


def format_trace(frames: list[DebugFrame]) -> str:
    lines = [f"{f.function_name} at {f.file_name}:{f.line}" for f in frames]
    return "\n".join(lines)
