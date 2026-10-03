# RTR-OS - brings the system up in the qemu-pi4 emulator (inside WSL) with the
# web interface reachable from the browser at http://localhost:<port>/.
#
# The serial console shows in this terminal. To stop: Ctrl+C.
#
#   -Port     local port bound to port 80 of the emulated board
#   -Scope          streams every GPIO transition to the live oscilloscope (scripts\scope.ps1)
#   -Deterministic  virtual clock advancing per instruction (icount): exact pulse widths, slower system
#   -Distro   WSL distribution where the emulator was built
param(
    [int]$Port = 8080,
    [switch]$Scope,
    [switch]$Deterministic,
    [string]$Distro = 'Ubuntu'
)

$ErrorActionPreference = 'Stop'

$root = (Split-Path -Parent $PSScriptRoot) -replace '\\', '/'
$wslRoot = (wsl -d $Distro -- wslpath -a $root).Trim()

# Variable names are case-insensitive: keep the locals distinct from the parameters.
$scopeArg = if ($Scope) { 'scope' } else { '-' }
$shiftArg = if ($Deterministic) { '3' } else { '' }
wsl -d $Distro -- bash "$wslRoot/scripts/run-web.sh" $Port $scopeArg $shiftArg
exit $LASTEXITCODE
