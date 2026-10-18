# Builds the Nintendo Switch NRO: out\nx\bakugan.nro (devkitA64 GCC + libnx, static NVK from the Mesa Horizon SDK).
# The generated code must already exist (tools\codegen.ps1 with the PC SDK): a cross build cannot run rexglue.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_switch.ps1 [-Jobs 8] [-Reconfigure]
# NVK_SDK (environment) overrides the Mesa SDK folder (it must contain lib\libvulkan.a).
param([int]$Jobs = 8, [switch]$Reconfigure)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'env.ps1')  # CMake and Ninja on PATH (the compilers come from the toolchain file)
$nvk = if ($env:NVK_SDK) { $env:NVK_SDK } else {
    'C:/bakugan-tools/mesa-switch/mesa-unified-install/opt/devkitpro/portlibs/switch' }
# devkitPro's installer (or Git Bash) exports DEVKITPRO as an MSYS path (/opt/devkitpro) that Windows CMake cannot open.
$devkitpro = 'C:/devkitPro'
$env:DEVKITPRO = $devkitpro
$build = Join-Path $root 'out\nx'
$glslang = (Join-Path $root 'out\glslang\StandAlone\glslangValidator.exe').Replace([char]92, [char]47)  # host tool
if ($Reconfigure -or -not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    & cmake -S (Join-Path $root 'app') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_TOOLCHAIN_FILE=$root\tools\switch\cmake\switch-devkitA64.cmake" `
        "-DREXGLUE_SWITCH_NVK_SDK=$nvk" "-DBAKUGAN_GLSLANG_VALIDATOR=$glslang" "-DDEVKITPRO=$devkitpro" `
        "-DCMAKE_MAKE_PROGRAM=$((Get-Command ninja).Source)"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& cmake --build $build -j $Jobs
exit $LASTEXITCODE
