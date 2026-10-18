# Configures the PC build (out\pc). Run from anywhere; log in out\pc_configure.log.
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'run.ps1') 'out\pc_configure.log' 'cmake --preset win-pc -S app'
