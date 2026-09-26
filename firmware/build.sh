#!/bin/sh
# Munt386 -- build the CSE firmware image.
#
# The firmware is Z80 assembly.  It is assembled with one of the tools from the
# TI/Doors-CSE ecosystem.  This script does not install anything; it locates an
# assembler already present on the system and reports clearly when none is
# available (the firmware cannot be produced without one).
#
#   sass    (KnightOS)                 sass --include ... src firmware/munt386.asm out.bin
#   brass   (Python, brass.assembler)  brass firmware/munt386.asm out.bin
#   spasm-ng (SPASM-ng)                spasm-ng -O out.bin firmware/munt386.asm
#
# Output: build/firmware/munt386.bin (raw) when an assembler is found.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/firmware/munt386.asm"
OUTDIR="$ROOT/build/firmware"
OUT="$OUTDIR/munt386.bin"

mkdir -p "$OUTDIR"

if command -v sass >/dev/null 2>&1; then
    echo "building with sass..."
    sass --include "$ROOT/firmware/include" "$SRC" "$OUT"
elif command -v brass >/dev/null 2>&1; then
    echo "building with brass..."
    brass "$SRC" "$OUT"
elif command -v spasm-ng >/dev/null 2>&1; then
    echo "building with spasm-ng..."
    spasm-ng -O "$OUT" "$SRC"
elif command -v spasm >/dev/null 2>&1; then
    echo "building with spasm..."
    spasm -O "$OUT" "$SRC"
else
    echo "munt386: no Z80 assembler found." >&2
    echo "  Install one of: sass (KnightOS), brass, or SPASM-ng," >&2
    echo "  then re-run this script.  The host emulator builds and tests" >&2
    echo "  without any Z80 toolchain (see 'make test')." >&2
    exit 2
fi

echo "munt386: wrote $OUT"
