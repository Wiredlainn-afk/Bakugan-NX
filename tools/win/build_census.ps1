# Builds the Direct3D census executable into out\census_pc (a separate tree; out\pc stays the normal build).
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cpp = (Resolve-Path 'out\census\bakugan_d3d_census.cpp').Path.Replace([char]92, [char]47)
$q = [char]34
& (Join-Path $PSScriptRoot 'run.ps1') 'out\census_configure.log' "cmake --preset win-pc -S app -B out/census_pc ${q}-DBAKUGAN_EXTRA_SOURCES=$cpp${q}"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot 'run.ps1') 'out\census_build.log' 'cmake --build out/census_pc'
