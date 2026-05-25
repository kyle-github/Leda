"""Frame slot planning scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class FunctionLayout:
    function_id: int
    arity: int
    local_count: int
    temp_count: int
    return_count: int
    return_slot_base: int


def plan_function_slots() -> list[FunctionLayout]:
    """Stub planner: no functions yet."""
    return []
