# RTR-OS - ensaio de proteção de memória do kernel.
#
# Compila, em build-fault, uma versão que grava no próprio código logo após
# ligar a MMU, e roda no QEMU. O resultado esperado é a parada por exceção;
# se a gravação for aceita, o kernel imprime "ensaio FALHOU".

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-fault'

cmake -S $root -B $build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$root\cmake\toolchain-aarch64.cmake" -DRTR_FAULT_TEST=ON | Out-Null
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build | Out-Null
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$qemu = Get-Command qemu-system-aarch64 -ErrorAction SilentlyContinue
$qemu = if ($qemu) { $qemu.Source } else { 'C:\Program Files\qemu\qemu-system-aarch64.exe' }

$log = Join-Path $env:TEMP 'rtr-os-fault.log'
if (Test-Path $log) { Clear-Content $log }

$qemuArgs = @('-M', 'raspi4b', '-kernel', (Join-Path $build 'kernel8.img'), '-display', 'none', '-monitor', 'none', '-serial', "file:$log")
$process = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 3
Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$output = Get-Content $log -Encoding utf8
$output
if (($output -match 'PANIC: excecao inesperada') -and -not ($output -match 'ensaio FALHOU')) {
    Write-Host "`nRESULTADO: proteção confirmada, a gravação no código foi barrada."
    exit 0
}
Write-Host "`nRESULTADO: a proteção NÃO funcionou."
exit 1
