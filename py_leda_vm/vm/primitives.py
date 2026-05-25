"""Primitive dispatch scaffolding."""

from typing import Callable


PrimitiveFn = Callable[[list[object]], object]


class PrimitiveRegistry:
    def __init__(self) -> None:
        self._impl: dict[int, PrimitiveFn] = {}

    def register(self, primitive_id: int, fn: PrimitiveFn) -> None:
        self._impl[primitive_id] = fn

    def call(self, primitive_id: int, args: list[object]) -> object:
        if primitive_id not in self._impl:
            raise KeyError(f"unknown primitive {primitive_id}")
        return self._impl[primitive_id](args)
