#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from collections import deque

from pc_trace import iter_pcs


def npc_pcs(path: str):
    with open(path, "r", encoding="utf-8") as fp:
        for line in fp:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("access_kind") != "ifetch":
                continue
            yield int(record["pc_u64"])


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare NPC itrace JSONL against a decoded NEMU pc trace")
    parser.add_argument("npc_itrace_jsonl")
    parser.add_argument("nemu_trace")
    parser.add_argument("--context", type=int, default=8)
    args = parser.parse_args()

    prev = deque(maxlen=args.context)
    npc_iter = npc_pcs(args.npc_itrace_jsonl)
    nemu_iter = iter_pcs(args.nemu_trace)
    idx = 0

    while True:
        try:
            npc_pc = next(npc_iter)
        except StopIteration:
            try:
                nemu_pc = next(nemu_iter)
            except StopIteration:
                print(f"match: {idx} dynamic PCs")
                return 0
            print(f"length mismatch at index {idx}: NPC ended, NEMU has 0x{nemu_pc:08x}")
            return 1

        try:
            nemu_pc = next(nemu_iter)
        except StopIteration:
            print(f"length mismatch at index {idx}: NEMU ended, NPC has 0x{npc_pc:08x}")
            return 1

        if npc_pc != nemu_pc:
            print(f"mismatch at index {idx}: NPC=0x{npc_pc:08x} NEMU=0x{nemu_pc:08x}")
            if prev:
                print("previous PCs:")
                for hist_idx, hist_pc in prev:
                    print(f"  {hist_idx}: 0x{hist_pc:08x}")
            return 1

        prev.append((idx, npc_pc))
        idx += 1


if __name__ == "__main__":
    raise SystemExit(main())
