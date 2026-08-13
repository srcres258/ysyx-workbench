#!/usr/bin/env python3
"""Quick end-to-end ysyxSoC + PC trace regression runner for NEMU.

This script validates one complete execution window across three trace modes:
  1. raw, uncompressed
  2. run, uncompressed
  3. run, bzip2-compressed

Run this script *inside* the repository Nix dev shell, for example:

  nix develop --command python3 nemu/scripts/run_ysyxsoc_trace_regression.py

The script intentionally does not spawn ``nix develop`` by itself. It relies on
the dev-shell ``shellHook`` to provide the toolchain and exported environment
variables used by NEMU and AM (`NEMU_HOME`, `AM_HOME`, cross compiler, etc.).

For each run it checks that:
  - NEMU exits successfully;
  - execution reaches "HIT GOOD TRAP";
  - the reported dynamic instruction count is present;
  - the generated PCTR file is structurally valid.

Then it verifies that:
  - raw/run/run+bzip2 decode to the same PC sequence;
  - every decoded PC count matches NEMU's reported guest instruction count;
  - the trace contains both sequential and non-sequential PC transitions,
    showing that it reflects actual control-flow changes instead of a trivial
    constant stream.
"""

from __future__ import annotations

import argparse
import bz2
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import BinaryIO, NoReturn, Sequence


SCRIPT_PATH = Path(__file__).resolve()
NEMU_DIR = SCRIPT_PATH.parents[1]
REPO_ROOT = NEMU_DIR.parent
TRACE_TOOL_DIR = NEMU_DIR / "tools" / "trace"

if str(TRACE_TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TRACE_TOOL_DIR))

from pc_trace import TraceFormatError, load_pcs  # noqa: E402
from pctr_format import (  # noqa: E402
    MAGIC,
    PCTR_ENDIANNESS_LITTLE,
    PCTR_V1_ADDRESS_WIDTH,
    PCTR_V1_HEADER_SIZE,
    PCTR_V1_RUN_STRIDE,
    PCTR_V1_TAG_RUN,
    PCTR_V1_TAG_SINGLE_PC,
    PCTR_V1_VERSION,
)


GOOD_TRAP_MARKER = "HIT GOOD TRAP"
INST_COUNT_RE = re.compile(r"total guest instructions = ([0-9,]+)")
HEADER_STRUCT = struct.Struct("<4sHHBBBBI")
FORMAT_RAW = 1
FORMAT_RUN = 2


@dataclass
class RunResult:
    name: str
    command: list[str]
    trace_path: Path
    compressed: bool
    expected_encoding: int
    log_path: Path
    stdout: str
    instruction_count: int


@dataclass
class TraceInspection:
    path: str
    compressed: bool
    encoding: int
    pc_count: int
    single_records: int
    run_records: int
    raw_records: int
    sequential_transitions: int
    non_sequential_transitions: int
    unique_pcs: int
    max_run_length: int


def fail(message: str) -> NoReturn:
    print(f"[trace-regression] ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def info(message: str) -> None:
    print(f"[trace-regression] {message}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--program",
        type=Path,
        default=REPO_ROOT / "am-kernels" / "kernels" / "hello" / "build" / "hello-riscv32e-ysyxsoc.bin",
        help="ysyxsoc program image to run",
    )
    parser.add_argument(
        "--nemu-bin",
        type=Path,
        default=NEMU_DIR / "build" / "riscv32-nemu-interpreter",
        help="path to the NEMU executable",
    )
    parser.add_argument(
        "--artifact-dir",
        type=Path,
        default=Path("/tmp/opencode/nemu-pc-trace-regression"),
        help="directory for logs, traces, and the JSON summary",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="rebuild NEMU before running the regression",
    )
    parser.add_argument(
        "--keep-artifacts",
        action="store_true",
        help="keep an existing artifact directory instead of cleaning it first",
    )
    return parser.parse_args()


def ensure_dev_shell() -> None:
    """Fail fast when the script is started outside the repo Nix dev shell.

    The project documents `nix develop` as the supported bootstrap path. The
    dev shell exports `NEMU_HOME`, `AM_HOME`, and toolchain paths via shellHook,
    so the regression runner assumes those are already available before it runs.
    """

    if os.environ.get("IN_NIX_SHELL") is None:
        fail(
            "this script must run inside the repository Nix dev shell; "
            "use `nix develop --command python3 nemu/scripts/run_ysyxsoc_trace_regression.py`"
        )

    required_env = ("NEMU_HOME", "AM_HOME")
    missing = [name for name in required_env if not os.environ.get(name)]
    if missing:
        fail(
            "missing dev-shell environment variable(s): "
            f"{', '.join(missing)}. Re-enter the repo with `nix develop`."
        )


def run_command(
    args: argparse.Namespace,
    command: Sequence[str],
    *,
    cwd: Path,
    log_path: Path,
    name: str,
) -> str:
    full_command = list(command)
    info(f"running {name}: {' '.join(full_command)}")
    completed = subprocess.run(
        full_command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    log_path.write_text(completed.stdout, encoding="utf-8")
    if completed.returncode != 0:
        fail(f"{name} failed with exit code {completed.returncode}; see {log_path}")
    return completed.stdout


def ensure_clean_artifact_dir(path: Path, keep_existing: bool) -> None:
    if path.exists() and not keep_existing:
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def maybe_build_nemu(args: argparse.Namespace) -> None:
    if not args.build and args.nemu_bin.exists():
        return
    build_log = args.artifact_dir / "build.log"
    run_command(
        args,
        ["make", "-C", str(NEMU_DIR), "GUEST_ISA=riscv32"],
        cwd=REPO_ROOT,
        log_path=build_log,
        name="nemu-build",
    )
    if not args.nemu_bin.exists():
        fail(f"NEMU binary was not produced at {args.nemu_bin}")


def open_trace(path: Path, compressed: bool) -> BinaryIO:
    if compressed:
        return bz2.open(path, "rb")
    return path.open("rb")


def inspect_trace(path: Path, *, compressed: bool, expected_encoding: int) -> TraceInspection:
    try:
        pcs = load_pcs(path)
    except TraceFormatError as exc:
        fail(f"trace decode failed for {path}: {exc}")

    sequential = 0
    non_sequential = 0
    for prev, current in zip(pcs, pcs[1:]):
        if current - prev == PCTR_V1_RUN_STRIDE:
            sequential += 1
        else:
            non_sequential += 1

    with open_trace(path, compressed) as stream:
        header = stream.read(HEADER_STRUCT.size)
        if len(header) != HEADER_STRUCT.size:
            fail(f"trace header is truncated in {path}")
        magic, version, header_size, addr_width, encoding, endianness, reserved0, reserved1 = HEADER_STRUCT.unpack(header)
        if magic != MAGIC:
            fail(f"bad trace magic in {path}: {magic!r}")
        if version != PCTR_V1_VERSION:
            fail(f"unsupported trace version in {path}: {version}")
        if header_size != PCTR_V1_HEADER_SIZE:
            fail(f"unsupported header size in {path}: {header_size}")
        if addr_width != PCTR_V1_ADDRESS_WIDTH:
            fail(f"unsupported address width in {path}: {addr_width}")
        if endianness != PCTR_ENDIANNESS_LITTLE:
            fail(f"unsupported endianness in {path}: {endianness}")
        if reserved0 != 0 or reserved1 != 0:
            fail(f"reserved trace header fields are not zero in {path}")
        if encoding != expected_encoding:
            fail(f"unexpected trace encoding in {path}: got {encoding}, expected {expected_encoding}")

        raw_records = 0
        single_records = 0
        run_records = 0
        max_run_length = 1 if pcs else 0

        if encoding == FORMAT_RAW:
            while True:
                chunk = stream.read(4)
                if not chunk:
                    break
                if len(chunk) != 4:
                    fail(f"raw trace payload is truncated in {path}")
                raw_records += 1
        elif encoding == FORMAT_RUN:
            while True:
                tag = stream.read(1)
                if not tag:
                    break
                if tag[0] == PCTR_V1_TAG_SINGLE_PC:
                    payload = stream.read(4)
                    if len(payload) != 4:
                        fail(f"single-pc record is truncated in {path}")
                    single_records += 1
                elif tag[0] == PCTR_V1_TAG_RUN:
                    payload = stream.read(8)
                    if len(payload) != 8:
                        fail(f"run record is truncated in {path}")
                    _start_pc, count = struct.unpack("<II", payload)
                    if count == 0:
                        fail(f"run record with zero count found in {path}")
                    run_records += 1
                    if count > max_run_length:
                        max_run_length = count
                else:
                    fail(f"unknown run tag 0x{tag[0]:02x} found in {path}")
        else:
            fail(f"unexpected encoding value in {path}: {encoding}")

    if not pcs:
        fail(f"decoded PC trace is empty: {path}")
    if len(set(pcs)) < 2:
        fail(f"decoded PC trace does not show any PC variation: {path}")
    if sequential == 0:
        fail(f"decoded PC trace has no sequential transitions: {path}")
    if non_sequential == 0:
        fail(f"decoded PC trace has no non-sequential transitions: {path}")

    return TraceInspection(
        path=str(path),
        compressed=compressed,
        encoding=encoding,
        pc_count=len(pcs),
        single_records=single_records,
        run_records=run_records,
        raw_records=raw_records,
        sequential_transitions=sequential,
        non_sequential_transitions=non_sequential,
        unique_pcs=len(set(pcs)),
        max_run_length=max_run_length,
    )


def parse_instruction_count(stdout: str, *, log_path: Path) -> int:
    match = INST_COUNT_RE.search(stdout)
    if match is None:
        fail(f"could not find guest instruction count in {log_path}")
    return int(match.group(1).replace(",", ""))


def ensure_good_trap(stdout: str, *, log_path: Path) -> None:
    if GOOD_TRAP_MARKER not in stdout:
        fail(f"{GOOD_TRAP_MARKER!r} not found in {log_path}")


def run_trace_case(
    args: argparse.Namespace,
    *,
    name: str,
    trace_path: Path,
    trace_format: str,
    trace_compress: str | None,
) -> RunResult:
    log_path = args.artifact_dir / f"{name}.log"
    command = [
        str(args.nemu_bin),
        "-b",
        "--machine=ysyxsoc",
        f"--pc-trace={trace_path}",
        f"--pc-trace-format={trace_format}",
    ]
    if trace_compress is not None:
        command.append(f"--pc-trace-compress={trace_compress}")
    command.append(str(args.program))

    stdout = run_command(args, command, cwd=NEMU_DIR, log_path=log_path, name=name)
    ensure_good_trap(stdout, log_path=log_path)

    return RunResult(
        name=name,
        command=list(command),
        trace_path=trace_path,
        compressed=trace_compress == "bzip2",
        expected_encoding=FORMAT_RAW if trace_format == "raw" else FORMAT_RUN,
        log_path=log_path,
        stdout=stdout,
        instruction_count=parse_instruction_count(stdout, log_path=log_path),
    )


def compare_sequences(results: list[RunResult]) -> dict[str, int]:
    sequences = [load_pcs(result.trace_path) for result in results]
    baseline = sequences[0]
    for result, sequence in zip(results[1:], sequences[1:]):
        if sequence != baseline:
            fail(f"decoded PC sequence mismatch for {result.name} compared to {results[0].name}")
    return {
        "decoded_pc_count": len(baseline),
        "first_pc": baseline[0],
        "last_pc": baseline[-1],
    }


def main() -> int:
    args = parse_args()
    args.program = args.program.resolve()
    args.nemu_bin = args.nemu_bin.resolve()
    args.artifact_dir = args.artifact_dir.resolve()

    # This runner assumes the caller already entered `nix develop`, so all
    # subsequent subprocesses execute directly in the inherited dev-shell env.
    ensure_dev_shell()

    if not args.program.is_file():
        fail(f"program image not found: {args.program}")

    ensure_clean_artifact_dir(args.artifact_dir, args.keep_artifacts)
    maybe_build_nemu(args)

    raw_path = args.artifact_dir / "hello-raw.pctrace"
    run_path = args.artifact_dir / "hello-run.pctrace"
    bz2_path = args.artifact_dir / "hello-run.pctrace.bz2"

    runs = [
        run_trace_case(args, name="raw", trace_path=raw_path, trace_format="raw", trace_compress=None),
        run_trace_case(args, name="run", trace_path=run_path, trace_format="run", trace_compress=None),
        run_trace_case(args, name="run-bzip2", trace_path=bz2_path, trace_format="run", trace_compress="bzip2"),
    ]

    inspections = []
    for result in runs:
        inspection = inspect_trace(
            result.trace_path,
            compressed=result.compressed,
            expected_encoding=result.expected_encoding,
        )
        if inspection.pc_count != result.instruction_count:
            fail(
                f"decoded PC count mismatch for {result.name}: "
                f"trace has {inspection.pc_count}, NEMU reported {result.instruction_count}"
            )
        inspections.append(inspection)

    sequence_summary = compare_sequences(runs)
    summary = {
        "program": str(args.program),
        "nemu_bin": str(args.nemu_bin),
        "artifact_dir": str(args.artifact_dir),
        "runs": [
            {
                "name": result.name,
                "command": result.command,
                "trace_path": str(result.trace_path),
                "log_path": str(result.log_path),
                "instruction_count": result.instruction_count,
            }
            for result in runs
        ],
        "trace_inspections": [asdict(inspection) for inspection in inspections],
        "sequence_summary": sequence_summary,
    }
    summary_path = args.artifact_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    info(f"decoded PC count: {sequence_summary['decoded_pc_count']}")
    info(f"artifacts written to: {args.artifact_dir}")
    info(f"summary written to: {summary_path}")
    info("ysyxsoc trace regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
