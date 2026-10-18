# Builds the code generator (rexglue) of the SDK fork into out/host. Run from bakugan-nx.
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'run.ps1') 'out\host_build.log' 'cmake --build out/host --target rexglue'
