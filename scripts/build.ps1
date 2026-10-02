# RTR-OS - configura (na primeira vez) e compila. Gera build\kernel8.img.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

if (-not (Test-Path (Join-Path $build 'build.ninja'))) {
    cmake -S $root -B $build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$root\cmake\toolchain-aarch64.cmake"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

cmake --build $build
exit $LASTEXITCODE
