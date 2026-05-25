"""LBC verifier scaffolding."""

from dataclasses import dataclass


@dataclass(slots=True)
class VerifyResult:
    ok: bool
    errors: list[str]


def verify_image(blob: bytes) -> VerifyResult:
    if not blob.startswith(b"LBC0"):
        return VerifyResult(ok=False, errors=["bad magic"])
    return VerifyResult(ok=True, errors=[])
