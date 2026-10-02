# RTR-OS - monta o conteúdo do cartão SD em build\sdcard.
#
# Baixa (uma única vez) os arquivos de firmware da Raspberry Pi e junta a eles
# config.txt e kernel8.img. Com -Drive, copia tudo para um cartão já formatado
# em FAT32, por exemplo:  scripts\make-sdcard.ps1 -Drive E:
param(
    [string]$Drive
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$image = Join-Path $root 'build\kernel8.img'
$firmware = Join-Path $root 'sdcard\firmware'
$stage = Join-Path $root 'build\sdcard'
$source = 'https://github.com/raspberrypi/firmware/raw/stable/boot'

if (-not (Test-Path $image)) { throw "Imagem ausente: rode scripts\build.ps1 primeiro." }

New-Item -ItemType Directory -Force $firmware, $stage | Out-Null

foreach ($file in 'start4.elf', 'fixup4.dat', 'bcm2711-rpi-4-b.dtb') {
    $target = Join-Path $firmware $file
    if (-not (Test-Path $target)) {
        Write-Host "Baixando $file"
        Invoke-WebRequest -Uri "$source/$file" -OutFile $target
    }
}

Copy-Item (Join-Path $firmware '*') $stage -Force
Copy-Item (Join-Path $root 'sdcard\config.txt') $stage -Force
Copy-Item $image $stage -Force
Write-Host "Conteúdo do cartão pronto em $stage"

if ($Drive) {
    $letter = $Drive.TrimEnd(':', '\')
    $volume = Get-Volume -DriveLetter $letter
    if ($volume.DriveType -ne 'Removable') { throw "$Drive não é uma unidade removível." }
    if ($volume.FileSystem -ne 'FAT32') { throw "$Drive precisa estar formatado em FAT32 (está em $($volume.FileSystem))." }

    Copy-Item (Join-Path $stage '*') "${letter}:\" -Force
    Write-Host "Copiado para ${letter}:\ - ejete o cartão antes de remover."
}
