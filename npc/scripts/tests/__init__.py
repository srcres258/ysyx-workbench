#!/usr/bin/env python3
"""Shared test fixtures and helpers for npc/scripts tests."""
import sys
from pathlib import Path

# Ensure npc/scripts is importable
_SCRIPT_DIR = str(Path(__file__).resolve().parent.parent)
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def fixture(name: str) -> Path:
    return FIXTURES / name
