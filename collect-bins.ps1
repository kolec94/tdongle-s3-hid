# Collects the 4 firmware binaries that ESP Web Tools needs into .\web\
#
# First, in Arduino IDE: open tdongle-hid-ble.ino, set the board/USB options,
# then Sketch -> "Export Compiled Binary". That writes the .bin files into a
# build\ subfolder next to the sketch. Then run this script.

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$web  = Join-Path $root 'web'

# 1. Find the exported app/bootloader/partitions bins (newest build folder)
$app = Get-ChildItem -Path $root -Recurse -Filter '*.ino.bin' |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $app) { throw "No *.ino.bin found. Run Sketch -> Export Compiled Binary first." }
$buildDir = $app.DirectoryName
Write-Host "Using build folder: $buildDir"

Copy-Item (Join-Path $buildDir '*.ino.bin')            (Join-Path $web 'firmware.bin')   -Force
Copy-Item (Join-Path $buildDir '*.ino.bootloader.bin') (Join-Path $web 'bootloader.bin') -Force
Copy-Item (Join-Path $buildDir '*.ino.partitions.bin') (Join-Path $web 'partitions.bin') -Force

# 2. boot_app0.bin ships with the esp32 core (not the sketch). Locate newest.
$pkg = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\hardware\esp32'
$boot = Get-ChildItem -Path $pkg -Recurse -Filter 'boot_app0.bin' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $boot) { throw "boot_app0.bin not found under $pkg" }
Copy-Item $boot.FullName (Join-Path $web 'boot_app0.bin') -Force

Write-Host "Done. web\ now contains: firmware.bin, bootloader.bin, partitions.bin, boot_app0.bin"
Get-ChildItem $web -Filter *.bin | Select-Object Name, Length
