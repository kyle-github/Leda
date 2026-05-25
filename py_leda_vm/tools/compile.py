"""Compile source to .lbc (stub)."""

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile a Leda source file to .lbc (stub)")
    parser.add_argument("source", nargs="?", help="Path to .led source")
    parser.add_argument("-o", "--output", default="out.lbc", help="Output .lbc path")
    args = parser.parse_args()

    if args.source:
        Path(args.output).write_bytes(b"LBC0\x01\x00")
        print(f"stub-compiled {args.source} -> {args.output}")
    else:
        parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
