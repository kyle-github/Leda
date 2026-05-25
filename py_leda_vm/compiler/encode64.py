"""64-bit instruction encoding helpers.

Layout:
- opcode: bits 63..56
- a: bits 55..44
- b: bits 43..32
- c: bits 31..20
- d: bits 19..8
- reserved: bits 7..0
"""

from dataclasses import dataclass


@dataclass(slots=True)
class DecodedInstruction:
    opcode: int
    a: int
    b: int
    c: int
    d: int
    reserved: int


_MASK_12 = 0xFFF
_MASK_8 = 0xFF


def encode_word(opcode: int, a: int = 0, b: int = 0, c: int = 0, d: int = 0, reserved: int = 0) -> int:
    if not (0 <= opcode <= _MASK_8):
        raise ValueError("opcode out of range")
    for name, value in (("a", a), ("b", b), ("c", c), ("d", d)):
        if not (0 <= value <= _MASK_12):
            raise ValueError(f"{name} out of range")
    if not (0 <= reserved <= _MASK_8):
        raise ValueError("reserved out of range")

    return (
        (opcode << 56)
        | (a << 44)
        | (b << 32)
        | (c << 20)
        | (d << 8)
        | reserved
    )


def decode_word(word: int) -> DecodedInstruction:
    return DecodedInstruction(
        opcode=(word >> 56) & _MASK_8,
        a=(word >> 44) & _MASK_12,
        b=(word >> 32) & _MASK_12,
        c=(word >> 20) & _MASK_12,
        d=(word >> 8) & _MASK_12,
        reserved=word & _MASK_8,
    )


def compose_imm36(b: int, c: int, d: int) -> int:
    for name, value in (("b", b), ("c", c), ("d", d)):
        if not (0 <= value <= _MASK_12):
            raise ValueError(f"{name} out of range")
    return (b << 24) | (c << 12) | d
