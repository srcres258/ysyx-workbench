#!/usr/bin/env python3
from __future__ import annotations

import argparse

from pc_trace import iter_pcs


def main() -> int:
    parser = argparse.ArgumentParser(description="Dump decoded PCs from a NEMU pc trace")
    parser.add_argument("trace")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    for idx, pc in enumerate(iter_pcs(args.trace)):
        if args.limit and idx >= args.limit:
            break
        print(f"{idx}: 0x{pc:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
