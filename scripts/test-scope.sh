#!/usr/bin/env bash
# RTR-OS - PWM capture test with the virtual oscilloscope (inside WSL).
#
# Builds a card image whose manifest runs the pwm program on GPIO 18 (1 kHz,
# 30 % duty), runs the emulator with the rtr-scope probe on that pin, and
# renders the capture to build/scope.html.
#
# Usage: test-scope.sh [seconds of capture] [icount shift]
# With an icount shift (for example 3) the emulator uses its deterministic
# virtual clock: time advances per instruction, so the waveform shows the
# logic of the program, not the scheduling of the host.

set -u

QEMU="$HOME/rtr-tools/qemu-pi4/build/qemu-system-aarch64"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="$ROOT/build/kernel8.img"
SD_IMAGE="$ROOT/build/sd-scope.img"
LOG="$ROOT/build/scope.log"
MANIFEST="$(mktemp)"
SECONDS_TO_RUN="${1:-4}"
ICOUNT=()
if [ -n "${2:-}" ]; then ICOUNT=(-icount "shift=$2,sleep=off"); fi

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

process pwm
  program pwm
  argument 4638
  core 1
  period 1 ms
  stack 4
  device gpio
end
EOF

python3 "$ROOT/scripts/make-sd-image.py" "$SD_IMAGE" 64 \
    "PWM.BIN=$ROOT/build/pwm.bin" "DEMO.BIN=$ROOT/build/demo.bin" "MANIFEST.TXT=$MANIFEST"
rm -f "$MANIFEST" "$LOG"

timeout "$SECONDS_TO_RUN" "$QEMU" -M raspi4b -kernel "$IMAGE" -drive "if=sd,format=raw,file=$SD_IMAGE" \
    -display none -monitor none -serial "file:$ROOT/build/scope-serial.log" -nic user \
    -chardev "file,id=scope,path=$LOG" -device rtr-scope,chardev=scope,pins=0x40000 "${ICOUNT[@]}"

echo "== serial =="
grep -E 'pwm|sealed|: real-time|load' "$ROOT/build/scope-serial.log"
echo "== capture =="
wc -l < "$LOG" | sed 's/^/transitions: /'
python3 "$ROOT/tools/scope/scope.py" "$LOG" "$ROOT/build/scope.html"
