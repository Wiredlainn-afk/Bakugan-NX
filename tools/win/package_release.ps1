# Makes the RELEASE files.
#
# Default (the web installer, installer/): out\nx\release_assets\{bakugan.nro, bakugan_native_pipelines.bin} - the two files
# to attach to the GitHub release. The installer page downloads them from the site that the workflow builds from that
# release, and makes the shader package in the player's browser from their own disc, so no shaders are published.
# The pipeline cache is optional (the game runs without it, the first battles just compile pipelines); it comes from
# release\cache\ and is left out when that folder has none. A SHA256SUMS.txt is written next to them.
#
# -Full makes the old self-contained SD card folder instead: out\nx\sdcard_release\switch\bakugan-nx\{bakugan.nro,
# bakugan_shaders.mesp, bakugan_shaders.mesp.idx, cache\..., game_root\} (the shaders are the game's, translated: see
# docs/building.md before publishing that one); -Zip also zips it as out\nx\bakugan-nx-<version>.zip.
#
# No bakugan.toml in either: the NRO writes the player's default one on its first run and everything else comes from its
# built-in defaults (one log file replaced at every start). Test builds go to out\nx\sdcard (package_switch.ps1 +
# stage_toml.ps1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\package_release.ps1 [-Full [-Zip]] [-Version 0.1.0]
param([switch]$Full, [switch]$Zip, [string]$Version = '0.1.0')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$nro = Join-Path $root 'out\nx\bakugan.nro'
$cache = Join-Path $root 'release\cache\bakugan_native_pipelines.bin'
if (-not (Test-Path $nro)) { throw "missing $nro (build the NRO first)" }

if (-not $Full) {
    $out = Join-Path $root 'out\nx\release_assets'
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    New-Item -ItemType Directory -Force $out | Out-Null
    Copy-Item -Force $nro $out
    if (Test-Path $cache) { Copy-Item -Force $cache $out; 'pipeline cache included' }
    else { 'no warmed pipeline cache (release\cache\bakugan_native_pipelines.bin): the release goes without it' }
    $sums = Get-ChildItem $out -File | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name }
    $sums | Set-Content -Encoding ASCII (Join-Path $out 'SHA256SUMS.txt')
    "release $Version assets in $out"
    Get-ChildItem $out -File | Select-Object Name, @{n='MB';e={[math]::Round($_.Length/1MB,2)}} | Format-Table -AutoSize
    return
}

$out = Join-Path $root 'out\nx\sdcard_release\switch\bakugan-nx'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($f in 'out\nx\bakugan.nro', 'out\shaderlib\bakugan_shaders.mesp', 'out\shaderlib\bakugan_shaders.mesp.idx') {
    $src = Join-Path $root $f
    if (-not (Test-Path $src)) { throw "missing $src (build the NRO / the shader library first)" }
    Copy-Item -Force $src $out
}
if (Test-Path $cache) {
    New-Item -ItemType Directory -Force (Join-Path $out 'cache') | Out-Null
    Copy-Item -Force $cache (Join-Path $out 'cache')
    'pipeline cache included'
} else {
    'no warmed pipeline cache yet (release\cache\bakugan_native_pipelines.bin): the first battles compile pipelines'
}
$gr = Join-Path $out 'game_root'
New-Item -ItemType Directory -Force $gr | Out-Null
$note = @(
    'Put the files of YOUR OWN copy of Bakugan: Battle Brawlers (Xbox 360) here:',
    '',
    '    game_root\default.xex',
    '    game_root\media\...        (the whole media folder of the disc)',
    '',
    'Delete this file if you like. The game''s files are not included in this download.'
)
$note | Set-Content -Encoding ASCII (Join-Path $gr 'PUT_YOUR_GAME_FILES_HERE.txt')
if ($Zip) {
    $zipPath = Join-Path $root "out\nx\bakugan-nx-$Version.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path (Join-Path $root 'out\nx\sdcard_release\switch') -DestinationPath $zipPath
    "zip: $zipPath"
}
Get-ChildItem $out -Recurse -File | Select-Object @{n='File';e={$_.FullName.Substring($out.Length + 1)}}, @{n='MB';e={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize
