"""Dispatch loop scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class VMState:
    halted: bool = False
    ip: int = 0


def run(state: VMState) -> VMState:
    """Stub VM runner: immediately halts."""
    state.halted = True
    return state
