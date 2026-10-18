# Makes the RELEASE SD card folder: out\nx\sdcard_release\switch\bakugan-nx\{bakugan.nro, bakugan_shaders.mesp,
# bakugan_shaders.mesp.idx} and, when it exists, cache\bakugan_native_pipelines.bin (the warmed pipeline cache from
# release\cache\). No toml: the NRO writes the player's default bakugan.toml on its first run and everything else comes from
# its built-in defaults. Test builds keep going to out\nx\sdcard (package_switch.ps1 + stage_toml.ps1), so this folder
# only ever holds what a player gets. The game's own files are never copied.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\package_release.ps1 [-Zip] [-Version 0.1.0]
#
# -Zip also makes out\nx\bakugan-nx-<version>.zip: its switch\ folder is extracted into the SD card's root, and the player
# only has to copy default.xex and media\ of their own disc into switch\bakugan-nx\game_root (an instruction file waits
# there).
param([switch]$Zip, [string]$Version = '0.1.0')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $root 'out\nx\sdcard_release\switch\bakugan-nx'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($f in 'out\nx\bakugan.nro', 'out\shaderlib\bakugan_shaders.mesp', 'out\shaderlib\bakugan_shaders.mesp.idx') {
    $src = Join-Path $root $f
    if (-not (Test-Path $src)) { throw "missing $src (build the NRO / the shader library first)" }
    Copy-Item -Force $src $out
}
$cache = Join-Path $root 'release\cache\bakugan_native_pipelines.bin'
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
