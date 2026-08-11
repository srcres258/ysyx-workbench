#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import deque

from pc_trace import iter_pcs


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two decoded NEMU PC traces")
    parser.add_argument("trace_a")
    parser.add_argument("trace_b")
    parser.add_argument("--context", type=int, default=8)
    args = parser.parse_args()

    prev = deque(maxlen=args.context)
    pcs_a = iter_pcs(args.trace_a)
    pcs_b = iter_pcs(args.trace_b)
    idx = 0

    while True:
      try:
        pc_a = next(pcs_a)
      except StopIteration:
        try:
          pc_b = next(pcs_b)
        except StopIteration:
          print(f"match: {idx} decoded PCs")
          return 0
        print(f"length mismatch at index {idx}: A ended, B has 0x{pc_b:08x}")
        return 1

      try:
        pc_b = next(pcs_b)
      except StopIteration:
        print(f"length mismatch at index {idx}: B ended, A has 0x{pc_a:08x}")
        return 1

      if pc_a != pc_b:
        print(f"mismatch at index {idx}: A=0x{pc_a:08x} B=0x{pc_b:08x}")
        if prev:
          print("previous PCs:")
          for hist_idx, hist_pc in prev:
            print(f"  {hist_idx}: 0x{hist_pc:08x}")
        return 1

      prev.append((idx, pc_a))
      idx += 1


if __name__ == "__main__":
    raise SystemExit(main())
