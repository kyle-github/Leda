"""Generate golden outputs by running the legacy Leda interpreter.

This tool does two jobs:

1. Discover Test/chap*.led cases and write a manifest.
2. Execute legacy `lc` and emit expected stdout/stderr/exit-code artifacts.
"""

import argparse
import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path


SPECIAL_STDIN = {
    "chap8c": "Test/concordanceInput",
    "chap17": "Test/chap17input",
}

SPECIAL_ARGS = {
    "chap20d": ["-m", "500000"],
}


@dataclass(slots=True)
class GoldenCase:
    test_name: str
    source_path: Path
    stdin_path: Path | None
    legacy_args: list[str]
    expected_stdout_path: Path
    expected_stderr_path: Path
    expected_exit_code: int = 0


def _chapter_sort_key(path: Path) -> tuple[int, str]:
    """Sort chap files by numeric chapter then suffix letter."""
    match = re.fullmatch(r"chap(\d+)([a-z]?)", path.stem)
    if match is None:
        return (10**9, path.stem)
    number = int(match.group(1))
    suffix = match.group(2)
    return (number, suffix)


def _normalize_output(text: str) -> str:
    """Normalize line endings and trim trailing whitespace/noise."""
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    return re.sub(r"[ \t\n]+$", "", normalized)


def discover_cases(repo_root: Path, manifest_dir: Path) -> list[GoldenCase]:
    test_dir = repo_root / "Test"
    output_dir = manifest_dir / "outputs"
    output_dir.mkdir(parents=True, exist_ok=True)

    chap_files = sorted(test_dir.glob("chap*.led"), key=_chapter_sort_key)
    cases: list[GoldenCase] = []

    for source_path in chap_files:
        name = source_path.stem
        stdin_rel = SPECIAL_STDIN.get(name)
        stdin_path = (repo_root / stdin_rel) if stdin_rel is not None else None
        legacy_args = list(SPECIAL_ARGS.get(name, []))

        cases.append(
            GoldenCase(
                test_name=name,
                source_path=source_path,
                stdin_path=stdin_path,
                legacy_args=legacy_args,
                expected_stdout_path=output_dir / f"{name}.stdout.txt",
                expected_stderr_path=output_dir / f"{name}.stderr.txt",
            )
        )

    return cases


def case_to_manifest_entry(case: GoldenCase, repo_root: Path, manifest_dir: Path) -> dict[str, object]:
    entry: dict[str, object] = {
        "test_name": case.test_name,
        "source_path": str(case.source_path.relative_to(repo_root)).replace("\\", "/"),
        "stdin_path": None,
        "legacy_args": case.legacy_args,
        "expected_stdout_path": str(case.expected_stdout_path.relative_to(manifest_dir)).replace("\\", "/"),
        "expected_stderr_path": str(case.expected_stderr_path.relative_to(manifest_dir)).replace("\\", "/"),
        "expected_exit_code": case.expected_exit_code,
    }

    if case.stdin_path is not None:
        entry["stdin_path"] = str(case.stdin_path.relative_to(repo_root)).replace("\\", "/")

    return entry


def resolve_legacy_path(repo_root: Path, explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit).expanduser().resolve()
        if not path.exists():
            raise FileNotFoundError(f"legacy interpreter not found: {path}")
        return path

    candidates = [
        repo_root / "build" / "lc",
        repo_root / "lc",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    raise FileNotFoundError(
        "could not locate legacy interpreter; pass --legacy /path/to/lc"
    )


def run_case(case: GoldenCase, legacy_path: Path, timeout_s: int) -> tuple[int, str, str]:
    command = [str(legacy_path)] + case.legacy_args + [case.source_path.name]

    stdin_handle = None
    try:
        if case.stdin_path is not None:
            stdin_handle = case.stdin_path.open("r", encoding="utf-8")

        completed = subprocess.run(
            command,
            cwd=str(case.source_path.parent),
            stdin=stdin_handle,
            capture_output=True,
            text=True,
            timeout=timeout_s,
            check=False,
        )
    finally:
        if stdin_handle is not None:
            stdin_handle.close()

    return (
        int(completed.returncode),
        _normalize_output(completed.stdout),
        _normalize_output(completed.stderr),
    )


def write_manifest(manifest_path: Path, payload: dict[str, object]) -> None:
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate chapter-test golden outputs from legacy lc")
    parser.add_argument("--manifest", default="tests/golden/manifest.json", help="Manifest path")
    parser.add_argument("--repo-root", default=None, help="Repository root containing Test/")
    parser.add_argument("--legacy", default=None, help="Path to legacy interpreter binary (lc)")
    parser.add_argument("--manifest-only", action="store_true", help="Only discover tests and write manifest")
    parser.add_argument("--keep-going", action="store_true", help="Continue generating after individual test failures")
    parser.add_argument("--timeout", type=int, default=30, help="Per-test timeout in seconds")
    args = parser.parse_args()

    manifest_path = Path(args.manifest).resolve()
    manifest_dir = manifest_path.parent
    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[2]

    cases = discover_cases(repo_root, manifest_dir)
    entries = [case_to_manifest_entry(case, repo_root, manifest_dir) for case in cases]

    payload: dict[str, object] = {
        "repo_root": str(repo_root),
        "generated_by": "tools/generate_goldens.py",
        "tests": entries,
    }

    if args.manifest_only:
        write_manifest(manifest_path, payload)
        print(f"Wrote manifest-only: {manifest_path} ({len(cases)} tests)")
        return 0

    try:
        legacy_path = resolve_legacy_path(repo_root, args.legacy)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}")
        return 2

    failures = 0
    for index, case in enumerate(cases, start=1):
        print(f"[{index}/{len(cases)}] {case.test_name}")
        try:
            exit_code, stdout_text, stderr_text = run_case(case, legacy_path, args.timeout)
        except Exception as exc:  # broad by design for harness robustness
            failures += 1
            print(f"  FAILED to run: {exc}")
            if not args.keep_going:
                return 1
            continue

        case.expected_exit_code = exit_code
        case.expected_stdout_path.parent.mkdir(parents=True, exist_ok=True)
        case.expected_stdout_path.write_text(stdout_text + "\n", encoding="utf-8")
        case.expected_stderr_path.write_text(stderr_text + "\n", encoding="utf-8")

        if exit_code != 0:
            failures += 1
            print(f"  non-zero exit: {exit_code}")
            if not args.keep_going:
                break

    payload["tests"] = [case_to_manifest_entry(case, repo_root, manifest_dir) for case in cases]
    write_manifest(manifest_path, payload)

    if failures > 0:
        print(f"Completed with failures: {failures}")
        return 1

    print(f"Generated goldens for {len(cases)} tests: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
