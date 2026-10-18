# Builds the Nintendo Switch NRO with LTO + -fipa-pta in its own build folder (out\nx_lto): out\nx_lto\bakugan.nro.
# The link is slow and needs a lot of RAM (GCC LTO of ~55 MB of generated code).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_switch_lto.ps1 [-Jobs 12] [-LtoJobs 6]
param([int]$Jobs = 12, [int]$LtoJobs = 6)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'env.ps1')
$nvk = if ($env:NVK_SDK) { $env:NVK_SDK } else { 'C:/bakugan-tools/mesa-switch/mesa-unified-install/opt/devkitpro/portlibs/switch' }
$devkitpro = 'C:/devkitPro'
$env:DEVKITPRO = $devkitpro
$build = Join-Path $root 'out\nx_lto'
$glslang = (Join-Path $root 'out\glslang\StandAlone\glslangValidator.exe').Replace([char]92, [char]47)
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    & cmake -S (Join-Path $root 'app') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_TOOLCHAIN_FILE=$root\tools\switch\cmake\switch-devkitA64.cmake" `
        "-DREXGLUE_SWITCH_NVK_SDK=$nvk" "-DBAKUGAN_GLSLANG_VALIDATOR=$glslang" "-DDEVKITPRO=$devkitpro" `
        "-DCMAKE_MAKE_PROGRAM=$((Get-Command ninja).Source)" -DBAKUGAN_SWITCH_LTO=ON "-DBAKUGAN_SWITCH_LTO_JOBS=$LtoJobs"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& cmake --build $build -j $Jobs
exit $LASTEXITCODE
