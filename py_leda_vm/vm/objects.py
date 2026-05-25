"""Object and class runtime scaffolding."""

from dataclasses import dataclass, field


@dataclass(slots=True)
class ClassInfo:
    class_id: int
    parent_class_id: int | None
    vtable: list[int] = field(default_factory=list)


@dataclass(slots=True)
class InstanceValue:
    class_id: int
    slots: list[object | None] = field(default_factory=list)
