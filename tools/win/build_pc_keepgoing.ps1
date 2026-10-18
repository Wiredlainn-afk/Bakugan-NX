# Builds the PC executable continuing past errors (ninja -k 0), to list every compile error at once.
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'run.ps1') 'out\pc_build.log' 'cmake --build out/pc -- -k 0'
