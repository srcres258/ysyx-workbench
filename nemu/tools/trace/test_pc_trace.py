#!/usr/bin/env python3
from __future__ import annotations

import bz2
import struct
import tempfile
import unittest
from pathlib import Path

from pc_trace import count_pcs, load_pcs


HEADER = struct.Struct("<4sHHBBBBI")


def write_header(fp, encoding: int) -> None:
    fp.write(HEADER.pack(b"PCTR", 1, HEADER.size, 4, encoding, 1, 0, 0))


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
                fp.write(bytes([0x02]))
                fp.write(struct.pack("<II", 0x3000, 3))
                fp.write(bytes([0x01]))
                fp.write(struct.pack("<I", 0x5000))
            self.assertEqual(load_pcs(path), [0x3000, 0x3004, 0x3008, 0x5000])

    def test_bzip2_decode(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "run.pctrace.bz2"
            with bz2.open(path, "wb") as fp:
                write_header(fp, 2)
                fp.write(bytes([0x02]))
                fp.write(struct.pack("<II", 0x4000, 4))
            self.assertEqual(load_pcs(path), [0x4000, 0x4004, 0x4008, 0x400c])
            self.assertEqual(count_pcs(path), 4)


if __name__ == "__main__":
    unittest.main()
