# Makes the SD card folder of the Switch build: out\nx\sdcard\switch\bakugan-nx\{bakugan.nro, bakugan_shaders.mesp,
# bakugan_shaders.mesp.idx}. The game files are NOT copied unless -IncludeGame is given (3.2 GB): copy your own
# extracted disc folder (assets\game_root) to sdmc:/switch/bakugan-nx/game_root yourself.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\package_switch.ps1 [-IncludeGame]
param([switch]$IncludeGame, [switch]$WithCache)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $root 'out\nx\sdcard\switch\bakugan-nx'
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($f in 'out\nx\bakugan.nro', 'out\shaderlib\bakugan_shaders.mesp', 'out\shaderlib\bakugan_shaders.mesp.idx') {
    $src = Join-Path $root $f
    if (-not (Test-Path $src)) { throw "missing $src (build the NRO / the shader library first)" }
    Copy-Item -Force $src $out
}
# A warmed pipeline cache (compiled by a real console after a few battles; NVK blobs are the same on every Switch with this NRO
# build and the shader package): copy <SD>\switch\bakugan-nx\cache\bakugan_native_pipelines.bin to
# release\cache\bakugan_native_pipelines.bin and it is packaged, so the first battle does not compile pipelines.
$cache = Join-Path $root 'release\cache\bakugan_native_pipelines.bin'
# The TEST folder does not get it unless -WithCache: the console keeps growing its own file while testing, and copying the
# packaged one over it would throw that away.
if ($WithCache -and (Test-Path $cache)) {
    New-Item -ItemType Directory -Force (Join-Path $out 'cache') | Out-Null
    Copy-Item -Force $cache (Join-Path $out 'cache')
}
if ($IncludeGame) {
    # Only what the game reads: default.xex and media\ ($SystemUpdate and nxeart are dashboard files).
    $game = Join-Path $root 'assets\game_root'
    $dest = Join-Path $out 'game_root'
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item -Force (Join-Path $game 'default.xex') $dest
    & robocopy (Join-Path $game 'media') (Join-Path $dest 'media') /E /NFL /NDL /NJH /NJS | Out-Null
}
Get-ChildItem $out | Select-Object Name, @{n='MB';e={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize
