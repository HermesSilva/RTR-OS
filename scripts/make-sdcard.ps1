# RTR-OS - assembles the SD card contents in build\sdcard.
#
# Downloads (once) the Raspberry Pi firmware files and puts config.txt,
# kernel8.img and the program files next to them. With -Drive, copies
# everything to a card already formatted as FAT32, for example:
#   scripts\make-sdcard.ps1 -Drive E:
param(
    [string]$Drive
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$image = Join-Path $root 'build\kernel8.img'
$firmware = Join-Path $root 'sdcard\firmware'
$stage = Join-Path $root 'build\sdcard'
$source = 'https://github.com/raspberrypi/firmware/raw/stable/boot'

if (-not (Test-Path $image)) { throw "Image missing: run scripts\build.ps1 first." }

New-Item -ItemType Directory -Force $firmware, $stage | Out-Null

foreach ($file in 'start4.elf', 'fixup4.dat', 'bcm2711-rpi-4-b.dtb') {
    $target = Join-Path $firmware $file
    if (-not (Test-Path $target)) {
        Write-Host "Downloading $file"
        Invoke-WebRequest -Uri "$source/$file" -OutFile $target
    }
}

Copy-Item (Join-Path $firmware '*') $stage -Force
Copy-Item (Join-Path $root 'sdcard\config.txt') $stage -Force
Copy-Item $image $stage -Force

# The programs the manifest may use, as NAME.BIN. The boot set is inside kernel8.img.
foreach ($program in Get-ChildItem (Join-Path $root 'build\*.bin')) {
    if ($program.BaseName -ne 'system') {
        Copy-Item $program.FullName (Join-Path $stage ($program.BaseName.ToUpper() + '.BIN')) -Force
    }
}
$manifest = Join-Path $root 'sdcard\manifest.txt'
if (Test-Path $manifest) { Copy-Item $manifest (Join-Path $stage 'MANIFEST.TXT') -Force }
Write-Host "Card contents ready in $stage"

if ($Drive) {
    $letter = $Drive.TrimEnd(':', '\')
    $volume = Get-Volume -DriveLetter $letter
    if ($volume.DriveType -ne 'Removable') { throw "$Drive is not a removable drive." }
    if ($volume.FileSystem -ne 'FAT32') { throw "$Drive must be formatted as FAT32 (it is $($volume.FileSystem))." }

    Copy-Item (Join-Path $stage '*') "${letter}:\" -Force
    Write-Host "Copied to ${letter}:\ - eject the card before removing it."
}
