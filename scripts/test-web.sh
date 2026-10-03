#!/usr/bin/env bash
# RTR-OS - web interface test in qemu-pi4 (inside WSL).
#
# Brings the system up with port 80 of the emulated board exposed on
# localhost:8080, requests the page and the statistics, and shows the serial
# console.
#
# Usage: test-web.sh [seconds to wait before the requests]

set -u

QEMU="$HOME/rtr-tools/qemu-pi4/build/qemu-system-aarch64"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="$ROOT/build/kernel8.img"
SD_IMAGE="$ROOT/build/sd.img"
SD_ARGS=()
if [ -f "$SD_IMAGE" ]; then SD_ARGS=(-drive "if=sd,format=raw,file=$SD_IMAGE"); fi
PORT=8080
WAIT="${1:-5}"
LOG="$(mktemp)"

"$QEMU" -M raspi4b -kernel "$IMAGE" "${SD_ARGS[@]}" -display none -monitor none \
    -serial "file:$LOG" -nic "user,hostfwd=tcp::$PORT-:80" &
PID=$!
trap 'kill $PID 2>/dev/null; rm -f "$LOG"' EXIT

sleep "$WAIT"

echo "== GET / =="
curl -s -m 5 -o /tmp/rtr-index.html -w 'status %{http_code}, %{size_download} bytes, type %{content_type}\n' \
    "http://localhost:$PORT/" || echo "no response"
head -c 120 /tmp/rtr-index.html 2>/dev/null | head -3
echo
echo "== GET /api/stats =="
curl -s -m 5 -w '\nstatus %{http_code}, %{size_download} bytes\n' "http://localhost:$PORT/api/stats" || echo "no response"
echo "== GET /missing =="
curl -s -m 5 -w 'status %{http_code}\n' "http://localhost:$PORT/missing" || echo "no response"
echo "== serial console =="
cat "$LOG"
