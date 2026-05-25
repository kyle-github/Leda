"""Differential runner against legacy-generated golden artifacts.

This tool reads tests/golden/manifest.json, compiles each source, runs the VM,
and compares (exit_code, stdout, stderr) with expected artifacts.
"""

import argparse
import json
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


def _normalize_output(text: str) -> str:
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    return re.sub(r"[ \t\n]+$", "", normalized)


def _safe_read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8")


def _write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


@dataclass(slots=True)
class TestCase:
    test_name: str
    source_path: Path
    stdin_path: Path | None
    expected_stdout_path: Path
    expected_stderr_path: Path
    expected_exit_code: int


def _load_manifest(manifest_path: Path, repo_root_override: Path | None = None) -> tuple[Path, list[TestCase]]:
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    repo_root = repo_root_override
    if repo_root is None:
        repo_root = Path(payload.get("repo_root", manifest_path.resolve().parents[2])).resolve()

    tests = payload.get("tests", [])
    cases: list[TestCase] = []
    for entry in tests:
        source_rel = str(entry["source_path"])
        stdin_rel = entry.get("stdin_path")
        expected_stdout_rel = str(entry["expected_stdout_path"])
        expected_stderr_rel = str(entry["expected_stderr_path"])
        expected_exit_code = int(entry["expected_exit_code"])

        cases.append(
            TestCase(
                test_name=str(entry["test_name"]),
                source_path=(repo_root / source_rel).resolve(),
                stdin_path=((repo_root / stdin_rel).resolve() if stdin_rel else None),
                expected_stdout_path=(manifest_path.parent / expected_stdout_rel).resolve(),
                expected_stderr_path=(manifest_path.parent / expected_stderr_rel).resolve(),
                expected_exit_code=expected_exit_code,
            )
        )

    return repo_root, cases


def _run_command(command: list[str], cwd: Path, timeout_s: int, stdin_path: Path | None = None) -> tuple[int, str, str]:
    stdin_handle = None
    try:
        if stdin_path is not None:
            stdin_handle = stdin_path.open("r", encoding="utf-8")

        completed = subprocess.run(
            command,
            cwd=str(cwd),
            stdin=stdin_handle,
            capture_output=True,
            text=True,
            timeout=timeout_s,
            check=False,
        )
    finally:
        if stdin_handle is not None:
            stdin_handle.close()

    return int(completed.returncode), _normalize_output(completed.stdout), _normalize_output(completed.stderr)


def _build_artifact_bundle(
    artifact_dir: Path,
    test: TestCase,
    compile_cmd: list[str],
    vm_cmd: list[str],
    compile_exit: int,
    compile_stdout: str,
    compile_stderr: str,
    vm_exit: int,
    vm_stdout: str,
    vm_stderr: str,
    expected_exit: int,
    expected_stdout: str,
    expected_stderr: str,
) -> None:
    _write_text(artifact_dir / "expected.stdout.txt", expected_stdout + "\n")
    _write_text(artifact_dir / "expected.stderr.txt", expected_stderr + "\n")
    _write_text(artifact_dir / "actual.stdout.txt", vm_stdout + "\n")
    _write_text(artifact_dir / "actual.stderr.txt", vm_stderr + "\n")
    _write_text(artifact_dir / "compile.stdout.txt", compile_stdout + "\n")
    _write_text(artifact_dir / "compile.stderr.txt", compile_stderr + "\n")

    metadata = {
        "test_name": test.test_name,
        "source_path": str(test.source_path),
        "stdin_path": str(test.stdin_path) if test.stdin_path else None,
        "compile_command": compile_cmd,
        "vm_command": vm_cmd,
        "expected_exit_code": expected_exit,
        "actual_compile_exit_code": compile_exit,
        "actual_vm_exit_code": vm_exit,
    }
    _write_text(artifact_dir / "metadata.json", json.dumps(metadata, indent=2) + "\n")


def main() -> int:
    tools_dir = Path(__file__).resolve().parent
    default_compile_cmd = f"{sys.executable} {tools_dir / 'compile.py'}"
    default_vm_cmd = f"{sys.executable} {tools_dir / 'run_vm.py'}"

    parser = argparse.ArgumentParser(description="Run differential comparison against golden manifest")
    parser.add_argument("--manifest", default="tests/golden/manifest.json", help="Golden manifest path")
    parser.add_argument("--repo-root", default=None, help="Override repo root")
    parser.add_argument(
        "--compile-cmd",
        default=default_compile_cmd,
        help="Compiler command prefix (source and -o output are appended)",
    )
    parser.add_argument(
        "--vm-cmd",
        default=default_vm_cmd,
        help="VM command prefix (image path is appended)",
    )
    parser.add_argument("--artifact-dir", default="tests/diff_artifacts", help="Where to write mismatch artifacts")
    parser.add_argument("--keep-going", action="store_true", help="Continue after mismatches")
    parser.add_argument("--timeout", type=int, default=30, help="Per-command timeout in seconds")
    parser.add_argument("--test", action="append", default=[], help="Run only named test (repeatable)")
    parser.add_argument("--test-regex", default=None, help="Run tests matching regex")
    args = parser.parse_args()

    manifest_path = Path(args.manifest).resolve()
    repo_root_override = Path(args.repo_root).resolve() if args.repo_root else None
    repo_root, cases = _load_manifest(manifest_path, repo_root_override)

    if args.test:
        wanted = set(args.test)
        cases = [case for case in cases if case.test_name in wanted]

    if args.test_regex:
        regex = re.compile(args.test_regex)
        cases = [case for case in cases if regex.search(case.test_name)]

    if not cases:
        print("No tests selected")
        return 2

    compile_cmd_base = shlex.split(args.compile_cmd)
    vm_cmd_base = shlex.split(args.vm_cmd)

    artifact_root = (Path(args.artifact_dir).resolve() if Path(args.artifact_dir).is_absolute() else (Path.cwd() / args.artifact_dir).resolve())
    image_dir = artifact_root / "images"
    image_dir.mkdir(parents=True, exist_ok=True)

    failed = 0
    for index, case in enumerate(cases, start=1):
        print(f"[{index}/{len(cases)}] {case.test_name}")

        expected_stdout = _normalize_output(_safe_read_text(case.expected_stdout_path))
        expected_stderr = _normalize_output(_safe_read_text(case.expected_stderr_path))

        image_path = image_dir / f"{case.test_name}.lbc"

        compile_cmd = list(compile_cmd_base) + [str(case.source_path), "-o", str(image_path)]
        compile_exit, compile_stdout, compile_stderr = _run_command(
            compile_cmd,
            cwd=repo_root,
            timeout_s=args.timeout,
        )

        vm_cmd = list(vm_cmd_base) + [str(image_path)]
        if compile_exit == 0:
            vm_exit, vm_stdout, vm_stderr = _run_command(
                vm_cmd,
                cwd=repo_root,
                timeout_s=args.timeout,
                stdin_path=case.stdin_path,
            )
        else:
            vm_exit, vm_stdout, vm_stderr = compile_exit, "", compile_stderr

        same_exit = (vm_exit == case.expected_exit_code)
        same_stdout = (vm_stdout == expected_stdout)
        same_stderr = (vm_stderr == expected_stderr)
        ok = same_exit and same_stdout and same_stderr

        if ok:
            print("  OK")
            continue

        failed += 1
        print(
            "  MISMATCH"
            f" (exit expected={case.expected_exit_code} actual={vm_exit},"
            f" stdout={'ok' if same_stdout else 'diff'}, stderr={'ok' if same_stderr else 'diff'})"
        )

        case_artifact_dir = artifact_root / case.test_name
        _build_artifact_bundle(
            artifact_dir=case_artifact_dir,
            test=case,
            compile_cmd=compile_cmd,
            vm_cmd=vm_cmd,
            compile_exit=compile_exit,
            compile_stdout=compile_stdout,
            compile_stderr=compile_stderr,
            vm_exit=vm_exit,
            vm_stdout=vm_stdout,
            vm_stderr=vm_stderr,
            expected_exit=case.expected_exit_code,
            expected_stdout=expected_stdout,
            expected_stderr=expected_stderr,
        )

        if not args.keep_going:
            print(f"Stopped on first mismatch. Artifacts: {case_artifact_dir}")
            return 1

    if failed > 0:
        print(f"FAILED: {failed}/{len(cases)} tests mismatched. Artifacts root: {artifact_root}")
        return 1

    print(f"PASS: {len(cases)} tests matched")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
