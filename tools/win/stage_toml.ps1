# Stages one test toml as the SD card folder's bakugan.toml (out\nx\sdcard\switch\bakugan-nx\bakugan.toml).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\stage_toml.ps1 W_profile_dynamic
param([Parameter(Mandatory = $true)][string]$Name)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$src = Join-Path $root "app\tests\$($Name -replace '\.toml$','').toml"
if (-not (Test-Path $src)) { throw "no such test toml: $src" }
$dst = Join-Path $root 'out\nx\sdcard\switch\bakugan-nx\bakugan.toml'
Copy-Item -Force $src $dst
"staged $(Split-Path $src -Leaf) -> $dst"
