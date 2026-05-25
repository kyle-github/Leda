"""Run .lbc image (stub)."""

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Run an .lbc image (stub)")
    parser.add_argument("image", nargs="?", help="Path to .lbc image")
    args = parser.parse_args()

    if args.image:
        _ = Path(args.image).read_bytes()
        print(f"stub-ran {args.image}")
    else:
        parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
