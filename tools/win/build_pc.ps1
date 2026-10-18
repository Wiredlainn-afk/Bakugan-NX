# Builds the PC executable (out\pc\bakugan.exe). Run from anywhere; log in out\pc_build.log.
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'run.ps1') 'out\pc_build.log' 'cmake --build out/pc'
