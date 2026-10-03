# RTR-OS - runs build\kernel8.img in the official QEMU, emulating the Raspberry Pi 4.
# The serial console shows in this terminal. To quit: Ctrl+A then X.
#
#   -Gdb   waits for a debugger on localhost:1234 before starting
param(
    [switch]$Gdb
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$image = Join-Path $root 'build\kernel8.img'
if (-not (Test-Path $image)) { throw "Image missing: run scripts\build.ps1 first." }

$qemu = Get-Command qemu-system-aarch64 -ErrorAction SilentlyContinue
$qemu = if ($qemu) { $qemu.Source } else { 'C:\Program Files\qemu\qemu-system-aarch64.exe' }

$qemuArgs = @('-M', 'raspi4b', '-kernel', $image, '-nographic')
if ($Gdb) { $qemuArgs += @('-s', '-S') }

& $qemu @qemuArgs
