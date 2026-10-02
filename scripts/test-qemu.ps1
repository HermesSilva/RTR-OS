# RTR-OS - roda build\kernel8.img no QEMU por alguns segundos, sem interação,
# e mostra o que saiu no console serial. Serve para conferir uma compilação.
#
#   -Seconds   tempo de execução antes de encerrar o emulador
#   -Virtual   usa relógio virtual determinístico (tempo conta por instruções),
#              em vez do relógio do Windows, que atrasa o timer em milissegundos
param(
    [int]$Seconds = 6,
    [switch]$Virtual
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$image = Join-Path $root 'build\kernel8.img'
if (-not (Test-Path $image)) { throw "Imagem ausente: rode scripts\build.ps1 primeiro." }

$qemu = Get-Command qemu-system-aarch64 -ErrorAction SilentlyContinue
$qemu = if ($qemu) { $qemu.Source } else { 'C:\Program Files\qemu\qemu-system-aarch64.exe' }

$log = Join-Path $env:TEMP 'rtr-os-serial.log'
Remove-Item $log -ErrorAction SilentlyContinue

$qemuArgs = @('-M', 'raspi4b', '-kernel', $image, '-display', 'none', '-monitor', 'none', '-serial', "file:$log")
if ($Virtual) { $qemuArgs += @('-icount', 'shift=0,sleep=off') }

$process = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -PassThru -WindowStyle Hidden
Start-Sleep -Seconds $Seconds
if ($process.HasExited) { Write-Host "O emulador encerrou sozinho (código $($process.ExitCode))." }
Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

if (Test-Path $log) { Get-Content $log -Encoding utf8 } else { Write-Host 'Nenhuma saída no console serial.' }
