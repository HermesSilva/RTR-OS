#!/usr/bin/env bash
# RTR-OS - runs build/kernel8.img in qemu-pi4 (inside WSL) for a few seconds,
# with no interaction, and shows what came out on the serial console.
#
# Usage: test-qemu-pi4.sh [seconds] [extra QEMU options...]
# The emulator is built by scripts/build-qemu-pi4.sh.

set -u

QEMU="$HOME/rtr-tools/qemu-pi4/build/qemu-system-aarch64"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="$ROOT/build/kernel8.img"
SD_IMAGE="$ROOT/build/sd.img"
SD_ARGS=()
if [ -f "$SD_IMAGE" ]; then SD_ARGS=(-drive "if=sd,format=raw,file=$SD_IMAGE"); fi
SECONDS_TO_RUN="${1:-6}"
shift || true

if [ ! -x "$QEMU" ]; then
    echo "Emulator missing: run scripts/build-qemu-pi4.sh first." >&2
    exit 1
fi
if [ ! -f "$IMAGE" ]; then
    echo "Image missing: run scripts\\build.ps1 first." >&2
    exit 1
fi

LOG="$(mktemp)"
timeout "$SECONDS_TO_RUN" "$QEMU" -M raspi4b -kernel "$IMAGE" "${SD_ARGS[@]}" \
    -display none -monitor none -serial "file:$LOG" -nic user "$@"
STATUS=$?

cat "$LOG"
rm -f "$LOG"

# 124 is the stop by timeout, which is expected.
if [ "$STATUS" -ne 124 ] && [ "$STATUS" -ne 0 ]; then
    echo "The emulator exited with code $STATUS." >&2
    exit "$STATUS"
fi
