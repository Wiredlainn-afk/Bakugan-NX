# Rebuilds only the code generator after an SDK source change. Run from anywhere.
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'run.ps1') 'out\host_build.log' 'cmake --build out/host --target rexglue'
