# Translates the game (default.xex) to C++ with the host code generator (classic flavour: no optimisation options).
# It builds nothing: build the generator first (tools\win\host_build.ps1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\codegen.ps1 [-Rexglue <path>]
#
# Input:   assets\game_root\default.xex  (tools\extract_iso.py puts it there), app\bakugan_manifest.toml
# Output:  app\generated\  (C++ of the game; not committed), out\codegen.log, out\default_image.bin (decrypted image
#          dump, the input of the analysis tools)
param([string]$Rexglue = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$app = Join-Path $root 'app'
if (-not $Rexglue) {
    $Rexglue = Get-ChildItem (Join-Path $root 'sdk\out') -Recurse -Include rexglue.exe -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Rexglue -or -not (Test-Path $Rexglue)) { throw 'rexglue.exe not found; build it first (tools\win\host_build.ps1)' }
if (-not (Test-Path (Join-Path $root 'assets\game_root\default.xex'))) { throw 'default.xex not found; run tools\extract_iso.py' }
New-Item -ItemType Directory -Force (Join-Path $root 'out') | Out-Null
$log = Join-Path $root 'out\codegen.log'
Write-Host "== codegen with $Rexglue"
Push-Location $app
try {
    $env:REX_MAX_JUMP_TABLE_ENTRIES = '1024'
    $env:REXGLUE_DUMP_IMAGE = (Join-Path $root 'out\default_image.bin')
    & $env:ComSpec /c "`"$Rexglue`" codegen bakugan_manifest.toml > `"$log`" 2>&1"
    $code = $LASTEXITCODE
} finally { Pop-Location }
Get-Content $log -Tail 12
if ($code -ne 0) { throw "codegen failed (exit $code), see out\codegen.log" }
Write-Host "done: $app\generated"
