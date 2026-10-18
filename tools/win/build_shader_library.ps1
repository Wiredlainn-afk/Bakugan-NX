# Builds the shader package out\shaderlib\bakugan_shaders.mesp(.idx) from dumped containers (vs_*/ps_*.bin folders).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_shader_library.ps1 <folder> [<folder> ...]
. (Join-Path $PSScriptRoot 'env.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$bash = 'C:\devkitPro\msys2\usr\bin\bash.exe'
$env:PATH = 'C:\devkitPro\msys2\usr\bin;' + $env:PATH  # dirname, rm, timeout... for the script (env.ps1 takes this folder out; first, so the msys timeout wins over Windows')
$args2 = @($args | ForEach-Object { (Resolve-Path $_).Path.Replace([char]92, [char]47) })
& $bash (Join-Path $root 'tools/build_shader_library.sh').Replace([char]92, [char]47) @args2
exit $LASTEXITCODE
