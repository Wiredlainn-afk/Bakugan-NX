# Makes the RELEASE files. Two outputs, both without the game's shaders (the web installer, installer/, makes the shader
# package in the player's browser from their own disc):
#
#   out\nx\sdcard_release\switch\bakugan-nx\    the SD card folder: bakugan.nro, cache\bakugan_native_pipelines.bin,
#                                               game_root\PUT_YOUR_GAME_FILES_HERE.txt (the player's bakugan_shaders.mesp
#                                               and .idx come from the installer page, or from -WithShaders below)
#   out\nx\release_assets\                      the files to attach to the GitHub release, which the installer page serves:
#                                               bakugan.nro, bakugan_native_pipelines.bin, SHA256SUMS.txt
#
# The pipeline cache is optional (the game runs without it, the first battles just compile pipelines); it comes from
# release\cache\ and is left out when that folder has none.
#
# -WithShaders also puts bakugan_shaders.mesp and .idx (out\shaderlib) into the SD card folder: the self-contained version
# (the shaders are the game's, translated: see docs/building.md before publishing that one).
# -Zip zips the SD card folder as out\nx\bakugan-nx-<version>.zip.
#
# No bakugan.toml: the NRO writes the player's default one on its first run and everything else comes from its built-in
# defaults (one log file replaced at every start). Test builds go to out\nx\sdcard (package_switch.ps1 + stage_toml.ps1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\package_release.ps1 [-WithShaders] [-Zip] [-Version 0.1.0]
param([switch]$WithShaders, [switch]$Zip, [string]$Version = '0.1.0')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$nro = Join-Path $root 'out\nx\bakugan.nro'
$cache = Join-Path $root 'release\cache\bakugan_native_pipelines.bin'
if (-not (Test-Path $nro)) { throw "missing $nro (build the NRO first)" }

# ---- the SD card folder ----
$out = Join-Path $root 'out\nx\sdcard_release\switch\bakugan-nx'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item -Force $nro $out
if ($WithShaders) {
    foreach ($f in 'out\shaderlib\bakugan_shaders.mesp', 'out\shaderlib\bakugan_shaders.mesp.idx') {
        $src = Join-Path $root $f
        if (-not (Test-Path $src)) { throw "missing $src (build the shader library first)" }
        Copy-Item -Force $src $out
    }
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
    'The game''s files are not included in this download.',
    $(if ($WithShaders) { '' } else { 'The shader package (bakugan_shaders.mesp and .idx) is made from your disc by the installer page.' })
)
$note | Set-Content -Encoding ASCII (Join-Path $gr 'PUT_YOUR_GAME_FILES_HERE.txt')
if ($Zip) {
    $zipPath = Join-Path $root "out\nx\bakugan-nx-$Version.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    # Entry names with forward slashes (Windows PowerShell's Compress-Archive writes backslashes, which other unzip tools
    # keep as part of the file name).
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $base = Join-Path $root 'out\nx\sdcard_release'
    $archive = [IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        Get-ChildItem (Join-Path $base 'switch') -Recurse -File | ForEach-Object {
            $rel = $_.FullName.Substring($base.Length + 1) -replace '\\', '/'
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $rel, 'Optimal')
        }
    } finally { $archive.Dispose() }
    "zip: $zipPath"
}

# ---- the GitHub release assets (what the installer page downloads) ----
$assets = Join-Path $root 'out\nx\release_assets'
if (Test-Path $assets) { Remove-Item -Recurse -Force $assets }
New-Item -ItemType Directory -Force $assets | Out-Null
Copy-Item -Force $nro $assets
if (Test-Path $cache) { Copy-Item -Force $cache $assets }
Get-ChildItem $assets -File | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name } |
    Set-Content -Encoding ASCII (Join-Path $assets 'SHA256SUMS.txt')

"release ${Version}: SD card folder $out"
Get-ChildItem $out -Recurse -File | Select-Object @{n='File';e={$_.FullName.Substring($out.Length + 1)}}, @{n='MB';e={[math]::Round($_.Length/1MB,2)}} | Format-Table -AutoSize
"release assets $assets"
Get-ChildItem $assets -File | Select-Object Name, @{n='MB';e={[math]::Round($_.Length/1MB,2)}} | Format-Table -AutoSize
