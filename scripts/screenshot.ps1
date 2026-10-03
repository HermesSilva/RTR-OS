# RTR-OS - captures the web interface with a headless Edge, one image per theme.
#
#   -Url    page to capture (default: the configuration tab on localhost:8080)
#   -Out    folder for the images (default: the system temp folder)
param(
    [string]$Url = 'http://localhost:8080/#config',
    [string]$Out = $env:TEMP
)

$ErrorActionPreference = 'Stop'

$edge = @(
    "$env:ProgramFiles (x86)\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge not found.' }

$separator = if ($Url.Contains('?')) { '&' } else { '?' }
$base = $Url -replace '#.*$', ''
$hash = if ($Url.Contains('#')) { $Url.Substring($Url.IndexOf('#')) } else { '' }

foreach ($theme in 'light', 'dark', 'amber') {
    $shot = Join-Path $Out "rtr-os-$theme.png"
    $profile = Join-Path $env:TEMP "rtr-os-edge-$theme"
    $target = "$base$separator" + "theme=$theme$hash"
    $process = Start-Process -FilePath $edge -ArgumentList '--headless=new', '--disable-gpu', '--hide-scrollbars',
        "--user-data-dir=$profile", "--screenshot=$shot", '--window-size=1360,1400', '--virtual-time-budget=6000',
        $target -PassThru -Wait
    Write-Host "$theme -> $shot ($((Get-Item $shot).Length) bytes)"
}
