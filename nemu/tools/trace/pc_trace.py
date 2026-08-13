#!/usr/bin/env python3
from __future__ import annotations

import bz2
import struct
from pathlib import Path
from typing import BinaryIO, Iterator, cast

from pctr_format import (
    MAGIC,
    PCTR_ENDIANNESS_LITTLE,
    PCTR_V1_ADDRESS_WIDTH,
    PCTR_V1_HEADER_SIZE,
    PCTR_V1_RUN_STRIDE,
    PCTR_V1_TAG_RUN,
    PCTR_V1_TAG_SINGLE_PC,
    PCTR_V1_VERSION,
)


HEADER_STRUCT = struct.Struct("<4sHHBBBBI")
FORMAT_RAW = 1
FORMAT_RUN = 2


class TraceFormatError(RuntimeError):
    pass


def _open_binary(path: str | Path) -> BinaryIO:
    path = Path(path)
    if path.suffix == ".bz2":
        return cast(BinaryIO, bz2.open(path, "rb"))
    return cast(BinaryIO, path.open("rb"))


def _read_header(stream: BinaryIO) -> int:
    data = stream.read(HEADER_STRUCT.size)
    if len(data) != HEADER_STRUCT.size:
        raise TraceFormatError("trace header is truncated")
    magic, version, header_size, addr_width, encoding, endianness, _reserved0, _reserved1 = HEADER_STRUCT.unpack(data)
    if magic != MAGIC:
        raise TraceFormatError(f"bad magic: {magic!r}")
    if version != PCTR_V1_VERSION:
        raise TraceFormatError(f"unsupported version: {version}")
    if header_size != PCTR_V1_HEADER_SIZE:
        raise TraceFormatError(f"unsupported header size: {header_size}")
    if addr_width != PCTR_V1_ADDRESS_WIDTH:
        raise TraceFormatError(f"unsupported address width: {addr_width}")
    if endianness != PCTR_ENDIANNESS_LITTLE:
        raise TraceFormatError(f"unsupported endianness flag: {endianness}")
    if encoding not in (FORMAT_RAW, FORMAT_RUN):
        raise TraceFormatError(f"unsupported encoding: {encoding}")
    return encoding


def iter_pcs(path: str | Path) -> Iterator[int]:
    with _open_binary(path) as stream:
        encoding = _read_header(stream)
        if encoding == FORMAT_RAW:
            while True:
                chunk = stream.read(4)
                if not chunk:
                    return
                if len(chunk) != 4:
                    raise TraceFormatError("raw pc record is truncated")
                yield struct.unpack("<I", chunk)[0]
        else:
            while True:
                tag_raw = stream.read(1)
                if not tag_raw:
                    return
                tag = tag_raw[0]
                if tag == PCTR_V1_TAG_SINGLE_PC:
                    chunk = stream.read(4)
                    if len(chunk) != 4:
                      raise TraceFormatError("single-pc record is truncated")
                    yield struct.unpack("<I", chunk)[0]
                elif tag == PCTR_V1_TAG_RUN:
                    chunk = stream.read(8)
                    if len(chunk) != 8:
                        raise TraceFormatError("seq-run record is truncated")
                    start_pc, count = struct.unpack("<II", chunk)
                    for i in range(count):
                        yield start_pc + i * PCTR_V1_RUN_STRIDE
                else:
                    raise TraceFormatError(f"unknown run-encoding tag: 0x{tag:02x}")


def load_pcs(path: str | Path) -> list[int]:
    return list(iter_pcs(path))


def count_pcs(path: str | Path) -> int:
    return sum(1 for _ in iter_pcs(path))
