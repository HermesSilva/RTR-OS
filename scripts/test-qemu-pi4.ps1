# RTR-OS - runs build\kernel8.img in qemu-pi4, inside WSL, for a few seconds,
# and shows what came out on the serial console.
#
# qemu-pi4 is a QEMU fork that also emulates the Raspberry Pi 4 Ethernet,
# GPIO, SPI and PWM. It is built once by scripts\build-qemu-pi4.sh.
#
#   -Seconds   run time before the emulator is stopped
#   -Distro    WSL distribution where the emulator was built
param(
    [int]$Seconds = 6,
    [string]$Distro = 'Ubuntu'
)

$ErrorActionPreference = 'Stop'

$root = (Split-Path -Parent $PSScriptRoot) -replace '\\', '/'
$wslRoot = (wsl -d $Distro -- wslpath -a $root).Trim()

wsl -d $Distro -- bash "$wslRoot/scripts/test-qemu-pi4.sh" $Seconds
exit $LASTEXITCODE
