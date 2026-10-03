#!/usr/bin/env bash
# RTR-OS - brings the system up in qemu-pi4 (inside WSL) with the web
# interface reachable from the Windows browser at http://localhost:<port>/.
#
# The serial console shows in this terminal. To stop: Ctrl+C.
#
# Usage: run-web.sh [port] [scope] [icount shift]   (default port 8080)
# An icount shift (for example 3) selects the deterministic virtual clock:
# time advances per instruction, so pulse widths show the logic of the
# program instead of the speed of the host. The system runs slower.
# With "scope" as the second argument the rtr-scope probe streams every GPIO
# transition on TCP port 5555, for the live oscilloscope (scripts\scope.ps1).

set -u

QEMU="$HOME/rtr-tools/qemu-pi4/build/qemu-system-aarch64"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="$ROOT/build/kernel8.img"
SD_IMAGE="$ROOT/build/sd.img"
SD_ARGS=()
if [ -f "$SD_IMAGE" ]; then SD_ARGS=(-drive "if=sd,format=raw,file=$SD_IMAGE"); fi
PORT="${1:-8080}"
SCOPE=()
if [ "${2:-}" = "scope" ]; then
    # Bound to every address: WSL only mirrors wildcard listeners to Windows,
    # where the bench (RTR-Bench) and scripts\scope.ps1 connect from.
    SCOPE=(-chardev "socket,id=scope,host=0.0.0.0,port=5555,server=on,wait=off" -device rtr-scope,chardev=scope)
    echo "GPIO probe on: streaming transitions on port 5555 (RTR-Bench or scripts\\scope.ps1 connect to it)"
fi
ICOUNT=()
if [ -n "${3:-}" ]; then
    ICOUNT=(-icount "shift=$3,sleep=off")
    echo "deterministic virtual clock: icount shift $3"
fi

if [ ! -x "$QEMU" ]; then
    echo "Emulator missing: run scripts/build-qemu-pi4.sh first." >&2
    exit 1
fi
if [ ! -f "$IMAGE" ]; then
    echo "Image missing: run scripts\\build.ps1 first." >&2
    exit 1
fi

# A previous instance (left behind by a Ctrl+C on the Windows side) keeps the
# port: stop it before binding again.
if pkill -f "qemu-system-aarch64 -M raspi4b" 2>/dev/null; then
    echo "stopped a previous emulator instance"
    sleep 1
fi

echo "RTR-OS web interface: http://localhost:$PORT/"
echo "Inside the emulated network the board appears as 10.0.2.15; its port 80"
echo "is bound to port $PORT of this machine. Ctrl+C stops."
echo

exec "$QEMU" -M raspi4b -kernel "$IMAGE" "${SD_ARGS[@]}" -display none -monitor none \
    -serial stdio -nic "user,hostfwd=tcp::$PORT-:80" "${SCOPE[@]}" "${ICOUNT[@]}"
