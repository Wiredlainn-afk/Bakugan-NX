# Regenerates the game code with the current app\codegen_flavor.toml and builds the Switch NRO, saved as out\nx\variants\<name>.nro (+ELF).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\switch_variant.ps1 <name>
param([Parameter(Mandatory = $true)][string]$Name)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
& powershell -NoProfile -ExecutionPolicy Bypass -File tools\codegen.ps1 | Select-Object -Last 1
& python (Join-Path $root 'tools\post_codegen.py') | Select-Object -Last 1
& powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_switch.ps1 -Jobs 16 | Select-String 'error|Linking CXX executable|FAILED' | Select-Object -Last 3
New-Item -ItemType Directory -Force out\nx\variants | Out-Null
Copy-Item -Force out\nx\bakugan.nro "out\nx\variants\$Name.nro"
Copy-Item -Force out\nx\bakugan "out\nx\variants\$Name.elf"
Get-Item "out\nx\variants\$Name.nro" | ForEach-Object { '{0} {1} bytes' -f $_.Name, $_.Length }
