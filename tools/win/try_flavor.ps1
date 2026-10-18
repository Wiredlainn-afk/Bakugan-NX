# Regenerates the game code with the current app\codegen_flavor.toml, builds the PC exe and runs the scripted battle for N seconds.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\try_flavor.ps1 [-Seconds 215]
param([int]$Seconds = 215)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
& powershell -NoProfile -ExecutionPolicy Bypass -File tools\codegen.ps1 | Select-Object -Last 2
& python tools\post_codegen.py | Select-Object -Last 1
& powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_pc.ps1 | Select-String 'exit=|error' | Select-Object -First 3
$env:REX_BAKUGAN_NATIVE_CONVERSION_FRAG = 'true'
$env:REX_INPUT_SCRIPT = '15000:300:0x0010,23000:300:0x0010,31000:300:0x0010,55000:300:0x0010,65000:300:0x1000,75000:300:0x1000,100000:300:0x0002,105000:300:0x1000,120000:300:0x1000,126000:300:0x0004,128000:300:0x0004,132000:300:0x1000,142000:300:0x1000,152000:300:0x1000,162000:300:0x1000,172000:300:0x1000'
& powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 -Seconds $Seconds -Tail 1 | Select-Object -First 2
Remove-Item Env:REX_BAKUGAN_NATIVE_CONVERSION_FRAG, Env:REX_INPUT_SCRIPT
$log = Get-ChildItem out\pc\logs\*.log | Sort-Object LastWriteTime | Select-Object -Last 1
Select-String -Path $log -Pattern 'access violation|trap hit' | Select-Object -First 3 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(150, $_.Line.Length)) }
