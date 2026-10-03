# RTR-OS - live virtual oscilloscope: connects to the GPIO probe inside the
# emulator and serves the oscilloscope page, which draws the pins in real time.
#
# Run the system with the probe first: scripts\run-web.ps1 -Scope
#
#   -Probe   address of the probe (default 127.0.0.1:5555)
#   -Port    local port of the oscilloscope page (default 8090)
#   -Vcd     also export every transition to a VCD file (PulseView, GTKWave)
#   -Capture render a capture file (build\scope.log) offline instead of going live
param(
    [string]$Probe = '127.0.0.1:5555',
    [int]$Port = 8090,
    [string]$Vcd = '',
    [string]$Capture = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ($Capture) {
    $page = Join-Path $root 'build\scope.html'
    python (Join-Path $root 'tools\scope\scope.py') $Capture $page
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Start-Process $page
    exit 0
}

$arguments = @((Join-Path $root 'tools\scope\live.py'), '--probe', $Probe, '--listen', $Port)
if ($Vcd) { $arguments += @('--vcd', $Vcd) }
Start-Process "http://localhost:$Port/"
python @arguments
