from __future__ import annotations

import os
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]


def run(cmd, cwd, env=None):
    p = subprocess.run(
        cmd,
        cwd=cwd,
        input="q\n",
        text=True,
        capture_output=True,
        env={**os.environ, **(env or {})},
        timeout=30,
    )
    return p


def main():
    nemu = run(["./build/riscv32-nemu-interpreter"], REPO / "nemu")
    assert nemu.returncode == 0, nemu.stderr
    assert "Welcome to" in nemu.stdout

    img = REPO / "series" / "char-test" / "char-test.bin"
    npc = run(
        ["./build/ysyx_25070190"],
        REPO / "npc",
        env={
            "NPC_SDB_ENABLED": "true",
            "IMG": str(img),
            "ASAN_OPTIONS": "detect_leaks=0:exitcode=0",
        },
    )
    assert npc.returncode == 0, npc.stderr
    assert "(npc)" in npc.stdout
    print("sdb smoke passed")


if __name__ == "__main__":
    main()
