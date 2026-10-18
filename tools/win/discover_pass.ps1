# One shader-discovery pass with the native renderer (muted, minimized): the renderer writes what the library does not
# know to out\disc\<name>\{discovery,variants,missing}. Input comes from the scripted pad (REX_INPUT_SCRIPT).
#   powershell -NoProfile -File tools\win\discover_pass.ps1 -Name p1 [-Seconds 200] [-Script '115000:250:0x1000,...']
param([Parameter(Mandatory=$true)][string]$Name, [int]$Seconds = 200, [string]$Script = '', [int]$ShotS = 0)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$d = Join-Path $root "out\disc\$Name"
New-Item -ItemType Directory -Force $d | Out-Null
$env:REX_BAKUGAN_RENDERER_NATIVE = 'true'
$env:BAKUGAN_SHADER_DISCOVERY = Join-Path $d 'discovery'
$env:BAKUGAN_VERTEX_VARIANTS = Join-Path $d 'variants'
$env:BAKUGAN_SHADER_MISSING = Join-Path $d 'missing'
if ($Script) { $env:REX_INPUT_SCRIPT = $Script }
if ($ShotS -gt 0) { $env:REX_BAKUGAN_SHOT_S = "$ShotS"; $env:REX_BAKUGAN_SHOT_COUNT = '6'; $env:REX_BAKUGAN_SHOT_EVERY_S = '20' }
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'boot_test.ps1') -Seconds $Seconds -Tail 0 2>&1 | Select-Object -First 1
Remove-Item env:REX_BAKUGAN_RENDERER_NATIVE, env:BAKUGAN_SHADER_DISCOVERY, env:BAKUGAN_VERTEX_VARIANTS, env:BAKUGAN_SHADER_MISSING -ErrorAction SilentlyContinue
Remove-Item env:REX_INPUT_SCRIPT, env:REX_BAKUGAN_SHOT_S, env:REX_BAKUGAN_SHOT_COUNT, env:REX_BAKUGAN_SHOT_EVERY_S -ErrorAction SilentlyContinue
foreach ($s in 'discovery','variants','missing') {
    $p = Join-Path $d $s
    "{0}: {1} files" -f $s, $(if (Test-Path $p) { (Get-ChildItem $p | Measure-Object).Count } else { 0 })
}
