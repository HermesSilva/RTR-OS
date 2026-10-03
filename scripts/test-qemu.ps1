# RTR-OS - runs build\kernel8.img in the official QEMU for a few seconds, with
# no interaction, and shows what came out on the serial console. Useful to
# check a build.
#
#   -Seconds   run time before the emulator is stopped
#   -Virtual   uses the deterministic virtual clock (time advances by
#              instruction count) instead of the Windows clock, which delays
#              the timer by milliseconds
param(
    [int]$Seconds = 6,
    [switch]$Virtual
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$image = Join-Path $root 'build\kernel8.img'
if (-not (Test-Path $image)) { throw "Image missing: run scripts\build.ps1 first." }

$qemu = Get-Command qemu-system-aarch64 -ErrorAction SilentlyContinue
$qemu = if ($qemu) { $qemu.Source } else { 'C:\Program Files\qemu\qemu-system-aarch64.exe' }

$log = Join-Path $env:TEMP 'rtr-os-serial.log'
Remove-Item $log -ErrorAction SilentlyContinue

$qemuArgs = @('-M', 'raspi4b', '-kernel', $image, '-display', 'none', '-monitor', 'none', '-serial', "file:$log")
if ($Virtual) { $qemuArgs += @('-icount', 'shift=0,sleep=off') }

$process = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -PassThru -WindowStyle Hidden
Start-Sleep -Seconds $Seconds
if ($process.HasExited) { Write-Host "The emulator exited on its own (code $($process.ExitCode))." }
Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

if (Test-Path $log) { Get-Content $log -Encoding utf8 } else { Write-Host 'No serial console output.' }
