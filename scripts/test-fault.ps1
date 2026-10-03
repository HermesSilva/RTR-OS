# RTR-OS - kernel memory protection test.
#
# Builds, in build-fault, a version that writes to its own code right after
# turning on the MMU, and runs it in the official QEMU. The expected result
# is a halt by exception; if the write is accepted, the kernel prints
# "test FAILED".

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
if (($output -match 'PANIC: unexpected exception') -and -not ($output -match 'test FAILED')) {
    Write-Host "`nRESULT: protection confirmed, the write to kernel code was blocked."
    exit 0
}
Write-Host "`nRESULT: the protection did NOT work."
exit 1
