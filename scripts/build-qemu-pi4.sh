#!/usr/bin/env bash
# RTR-OS - builds the qemu-pi4 emulator (a QEMU fork with the Raspberry Pi 4
# Ethernet, GPIO, SPI and PWM) inside WSL, without requiring sudo.
#
# Everything goes in ~/rtr-tools:
#   bin/micromamba   user-space package manager
#   env/             compiler and libraries (conda-forge)
#   qemu-pi4/        emulator source and build
#
# Usage, from Windows:
#   wsl -d Ubuntu -- bash /mnt/d/Tootega/Source/RTR-SO/scripts/build-qemu-pi4.sh

set -euo pipefail

TOOLS="$HOME/rtr-tools"
MAMBA="$TOOLS/bin/micromamba"
ENV="$TOOLS/env"
SRC="$TOOLS/qemu-pi4"
REPO="https://github.com/kmehltretter82/qemu-pi4"

mkdir -p "$TOOLS/bin"

if [ ! -x "$MAMBA" ]; then
    echo "== downloading micromamba =="
    curl -fsSL -o "$MAMBA" \
        https://github.com/mamba-org/micromamba-releases/releases/latest/download/micromamba-linux-64
    chmod +x "$MAMBA"
fi

if [ ! -d "$ENV" ]; then
    echo "== creating the build environment =="
    "$MAMBA" create -y -q -p "$ENV" -c conda-forge \
        c-compiler cxx-compiler pkg-config make ninja meson git \
        python=3.12 glib libslirp pixman dtc zlib flex bison
fi

if [ ! -d "$SRC/.git" ]; then
    echo "== cloning qemu-pi4 =="
    git clone --depth 1 "$REPO" "$SRC"
fi
echo "commit: $(git -C "$SRC" log -1 --format='%h %ad %s' --date=short)"

# RTR-OS additions to the emulator: the GPIO capture device of the virtual oscilloscope.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cp "$ROOT/tools/qemu-pi4/rtr_scope.c" "$SRC/hw/misc/rtr_scope.c"
if ! grep -q rtr_scope "$SRC/hw/misc/meson.build"; then
    echo "system_ss.add(when: 'CONFIG_RASPI4', if_true: files('rtr_scope.c'))" >> "$SRC/hw/misc/meson.build"
fi

mkdir -p "$SRC/build"
cd "$SRC/build"

if [ ! -f build.ninja ]; then
    echo "== configuring =="
    "$MAMBA" run -p "$ENV" ../configure \
        --target-list=aarch64-softmmu \
        --without-default-devices \
        --with-devices-aarch64=pi4 \
        --enable-slirp \
        --disable-werror \
        --disable-docs \
        --disable-tools
fi

echo "== building =="
"$MAMBA" run -p "$ENV" ninja qemu-system-aarch64

echo "== result =="
"$SRC/build/qemu-system-aarch64" --version | head -1
"$SRC/build/qemu-system-aarch64" -M help | grep -i -E 'raspi|pi4|pi400' || true
