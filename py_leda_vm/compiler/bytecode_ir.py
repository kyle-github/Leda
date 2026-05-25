"""Bytecode IR scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class InstructionIR:
    opcode: int
    a: int = 0
    b: int = 0
    c: int = 0
    d: int = 0
