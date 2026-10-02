# RTR-OS - roda build\kernel8.img no QEMU emulando o Raspberry Pi 4.
# O console serial sai neste terminal. Para sair: Ctrl+A e depois X.
#
#   -Gdb       espera um depurador em localhost:1234 antes de iniciar
#   -RealTime  usa o relógio do Windows em vez do relógio virtual determinístico
param(
    [switch]$Gdb,
    [switch]$RealTime
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$image = Join-Path $root 'build\kernel8.img'
if (-not (Test-Path $image)) { throw "Imagem ausente: rode scripts\build.ps1 primeiro." }

$qemu = Get-Command qemu-system-aarch64 -ErrorAction SilentlyContinue
$qemu = if ($qemu) { $qemu.Source } else { 'C:\Program Files\qemu\qemu-system-aarch64.exe' }

$qemuArgs = @('-M', 'raspi4b', '-kernel', $image, '-nographic')

# Com -icount o tempo do emulador avança só pela contagem de instruções (1 ns
# cada), e não pelo agendador do Windows, que atrasaria os disparos do timer
# em milissegundos. O tempo virtual não acompanha o relógio de parede.
if (-not $RealTime) { $qemuArgs += @('-icount', 'shift=0,sleep=off') }
if ($Gdb) { $qemuArgs += @('-s', '-S') }

& $qemu @qemuArgs
