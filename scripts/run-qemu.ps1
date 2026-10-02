# RTR-OS - roda build\kernel8.img no QEMU emulando o Raspberry Pi 4.
# O console serial sai neste terminal. Para sair: Ctrl+A e depois X.
#
#   -Gdb   espera um depurador em localhost:1234 antes de iniciar
param(
    [switch]$Gdb
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$image = Join-Path $root 'build\kernel8.img'
if (-not (Test-Path $image)) { throw "Imagem ausente: rode scripts\build.ps1 primeiro." }

$qemu = Get-Command qemu-system-aarch64 -ErrorAction SilentlyContinue
$qemu = if ($qemu) { $qemu.Source } else { 'C:\Program Files\qemu\qemu-system-aarch64.exe' }

$qemuArgs = @('-M', 'raspi4b', '-kernel', $image, '-nographic')
if ($Gdb) { $qemuArgs += @('-s', '-S') }

& $qemu @qemuArgs
