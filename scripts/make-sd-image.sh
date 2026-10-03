#!/usr/bin/env bash
# RTR-OS - creates build/sd.img, a FAT32 card image for the emulator (inside WSL).
#
# The image gets every program built (build/*.bin except the boot set) as
# NAME.BIN, and sdcard/manifest.txt as MANIFEST.TXT if it exists; otherwise
# the system writes the default manifest on first boot.
#
# Usage: make-sd-image.sh [size in MiB]   (default 64)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="$ROOT/build/sd.img"
SIZE_MIB="${1:-64}"
FILES=()

for bin in "$ROOT"/build/*.bin; do
    name="$(basename "$bin" .bin)"
    if [ "$name" != "system" ]; then
        FILES+=("$(echo "$name" | tr '[:lower:]' '[:upper:]').BIN=$bin")
    fi
done
if [ -f "$ROOT/sdcard/manifest.txt" ]; then
    FILES+=("MANIFEST.TXT=$ROOT/sdcard/manifest.txt")
fi

mkdir -p "$ROOT/build"
python3 "$ROOT/scripts/make-sd-image.py" "$IMAGE" "$SIZE_MIB" "${FILES[@]}"
