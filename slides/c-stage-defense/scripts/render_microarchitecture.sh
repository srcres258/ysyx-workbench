#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SLIDES_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEX_FILE="$SLIDES_DIR/microarchitecture.tex"
OUT_FILE="$SLIDES_DIR/assets/microarchitecture.svg"
BUILD_DIR="$(mktemp -d /tmp/opencode/microarchitecture.XXXXXX)"

cleanup() {
  rm -rf "$BUILD_DIR"
}
trap cleanup EXIT

xelatex \
  -interaction=nonstopmode \
  -halt-on-error \
  -output-directory="$BUILD_DIR" \
  "$TEX_FILE"

pdftocairo \
  -svg \
  "$BUILD_DIR/microarchitecture.pdf" \
  "$OUT_FILE"

printf 'wrote %s\n' "$OUT_FILE"
