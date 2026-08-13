#!/usr/bin/env python3
from __future__ import annotations

import bz2
import struct
import tempfile
import unittest
from pathlib import Path

from pc_trace import TraceFormatError, count_pcs, load_pcs
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


HEADER = struct.Struct("<4sHHBBBBI")


def write_header(fp, encoding: int) -> None:
    fp.write(
        HEADER.pack(
            MAGIC,
            PCTR_V1_VERSION,
            PCTR_V1_HEADER_SIZE,
            PCTR_V1_ADDRESS_WIDTH,
            encoding,
            PCTR_ENDIANNESS_LITTLE,
            0,
            0,
        )
    )


class PcTraceDecodeTest(unittest.TestCase):
    def test_raw_decode(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "raw.pctrace"
            with path.open("wb") as fp:
                write_header(fp, 1)
                for pc in (0x1000, 0x1004, 0x2000):
                    fp.write(struct.pack("<I", pc))
            self.assertEqual(load_pcs(path), [0x1000, 0x1004, 0x2000])

    def test_run_decode(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "run.pctrace"
            with path.open("wb") as fp:
                write_header(fp, 2)
                fp.write(bytes([PCTR_V1_TAG_RUN]))
                fp.write(struct.pack("<II", 0x3000, 3))
                fp.write(bytes([PCTR_V1_TAG_SINGLE_PC]))
                fp.write(struct.pack("<I", 0x5000))
            self.assertEqual(
                load_pcs(path),
                [0x3000, 0x3000 + PCTR_V1_RUN_STRIDE, 0x3000 + 2 * PCTR_V1_RUN_STRIDE, 0x5000],
            )

    def test_bzip2_decode(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "run.pctrace.bz2"
            with bz2.open(path, "wb") as fp:
                write_header(fp, 2)
                fp.write(bytes([PCTR_V1_TAG_RUN]))
                fp.write(struct.pack("<II", 0x4000, 4))
            self.assertEqual(
                load_pcs(path),
                [0x4000 + i * PCTR_V1_RUN_STRIDE for i in range(4)],
            )
            self.assertEqual(count_pcs(path), 4)

    def test_equivalent_raw_run_and_bzip2_decode(self) -> None:
        sequence = [0x1000, 0x1004, 0x1008, 0x2000, 0x2004]

        with tempfile.TemporaryDirectory() as td:
            raw_path = Path(td) / "seq.raw.pctrace"
            run_path = Path(td) / "seq.run.pctrace"
            bz2_path = Path(td) / "seq.run.pctrace.bz2"

            with raw_path.open("wb") as fp:
                write_header(fp, 1)
                for pc in sequence:
                    fp.write(struct.pack("<I", pc))

            with run_path.open("wb") as fp:
                write_header(fp, 2)
                fp.write(bytes([PCTR_V1_TAG_RUN]))
                fp.write(struct.pack("<II", 0x1000, 3))
                fp.write(bytes([PCTR_V1_TAG_RUN]))
                fp.write(struct.pack("<II", 0x2000, 2))

            with bz2.open(bz2_path, "wb") as fp:
                write_header(fp, 2)
                fp.write(bytes([PCTR_V1_TAG_RUN]))
                fp.write(struct.pack("<II", 0x1000, 3))
                fp.write(bytes([PCTR_V1_TAG_RUN]))
                fp.write(struct.pack("<II", 0x2000, 2))

            self.assertEqual(load_pcs(raw_path), sequence)
            self.assertEqual(load_pcs(run_path), sequence)
            self.assertEqual(load_pcs(bz2_path), sequence)

    def test_rejects_truncated_run_record(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "truncated-run.pctrace"
            with path.open("wb") as fp:
                write_header(fp, 2)
                fp.write(bytes([PCTR_V1_TAG_RUN]))
                fp.write(struct.pack("<I", 0x1000))

            with self.assertRaisesRegex(TraceFormatError, "seq-run record is truncated"):
                load_pcs(path)

    def test_rejects_unknown_run_tag(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "bad-tag.pctrace"
            with path.open("wb") as fp:
                write_header(fp, 2)
                fp.write(bytes([0x7F]))

            with self.assertRaisesRegex(TraceFormatError, "unknown run-encoding tag"):
                load_pcs(path)


if __name__ == "__main__":
    unittest.main()
