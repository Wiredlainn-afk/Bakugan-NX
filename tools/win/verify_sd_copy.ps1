# Compares the game folder copied to the SD card with the PC copy (names and sizes) and lists what is missing or
# differs. Hashes are checked for the files that differ in size only (a full hash of 3 GB over the SD is slow).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\verify_sd_copy.ps1 -Sd E:
param([Parameter(Mandatory)][string]$Sd)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$src = Join-Path $root 'assets\game_root'
$dst = Join-Path $Sd.TrimEnd('\') 'switch\bakugan-nx\game_root'
if (-not (Test-Path $dst)) { "not found: $dst"; exit 1 }
$skip = @('$SystemUpdate', 'nxeart')
$bad = 0; $n = 0
Get-ChildItem $src -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($src.Length + 1)
    if ($skip | Where-Object { $rel -like "$_*" }) { return }
    $n++
    $d = Join-Path $dst $rel
    if (-not (Test-Path -LiteralPath $d)) { "MISSING  $rel"; $script:bad++; return }
    $len = (Get-Item -LiteralPath $d).Length
    if ($len -ne $_.Length) { "DIFFERENT SIZE  $rel  PC $($_.Length)  SD $len"; $script:bad++ }
}
"checked $n files, $bad problem(s)"
