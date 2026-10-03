#!/usr/bin/env bash
# RTR-OS - configuration round trip in qemu-pi4 (inside WSL).
#
# Brings the system up, reads the configuration, posts a modified manifest
# (the report period changes to 2 s), and shows the console across the
# reboot the save triggers. Needs build/sd.img (scripts/make-sd-image.sh).

set -u

QEMU="$HOME/rtr-tools/qemu-pi4/build/qemu-system-aarch64"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="$ROOT/build/kernel8.img"
SD_IMAGE="$ROOT/build/sd.img"
PORT=8080
LOG="$(mktemp)"
MANIFEST="$(mktemp)"

"$QEMU" -M raspi4b -kernel "$IMAGE" -drive "if=sd,format=raw,file=$SD_IMAGE" -display none \
    -monitor none -serial "file:$LOG" -nic "user,hostfwd=tcp::$PORT-:80" &
PID=$!
trap 'kill $PID 2>/dev/null; rm -f "$LOG" "$MANIFEST"' EXIT

sleep 6
echo "== GET /api/config =="
curl -s -m 5 "http://localhost:$PORT/api/config" | head -c 600
echo
echo

cat > "$MANIFEST" <<'EOF'
# RTR-OS manifest
version 1

process system
  priority 4
  period 1 ms
  limit 800 us
  system yes
  floor 400 us
end

process report
  program demo
  argument 0
  priority 1
  period 2 s
  limit 200 ms
  system no
  stack 4
end
EOF

echo "== POST /api/config (bad manifest: limit above period) =="
printf 'version 1\nprocess x\n program demo\n period 1 ms\n limit 2 ms\nend\n' | \
    curl -s -m 5 -w '\nstatus %{http_code}\n' -X POST --data-binary @- "http://localhost:$PORT/api/config"
echo "== POST /api/config (report period 2 s) =="
curl -s -m 5 -w '\nstatus %{http_code}\n' -X POST --data-binary "@$MANIFEST" "http://localhost:$PORT/api/config"

sleep 8
echo "== GET /api/config after the reboot =="
curl -s -m 5 "http://localhost:$PORT/api/config" | head -c 300
echo
echo "== serial console =="
cat "$LOG"
