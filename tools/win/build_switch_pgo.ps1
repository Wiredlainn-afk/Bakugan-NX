# Profile-guided Switch builds (LTO + -fipa-pta + PGO) in their own build folder out\nx_pgo (the profile's .gcda names carry that path:
# generate and use must be built there).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_switch_pgo.ps1 -Mode generate   (1: instrumented NRO, play it)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\pgo_merge.ps1 <pgo folder from the SD card>   (2: merge on the PC)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_switch_pgo.ps1 -Mode use        (3: the optimised NRO)
param([Parameter(Mandatory = $true)][ValidateSet('generate', 'use')][string]$Mode, [int]$Jobs = 12, [int]$LtoJobs = 6)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'env.ps1')
$nvk = if ($env:NVK_SDK) { $env:NVK_SDK } else { 'C:/bakugan-tools/mesa-switch/mesa-unified-install/opt/devkitpro/portlibs/switch' }
$devkitpro = 'C:/devkitPro'
$env:DEVKITPRO = $devkitpro
$build = Join-Path $root 'out\nx_pgo'
$glslang = (Join-Path $root 'out\glslang\StandAlone\glslangValidator.exe').Replace([char]92, [char]47)
& cmake -S (Join-Path $root 'app') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_TOOLCHAIN_FILE=$root\tools\switch\cmake\switch-devkitA64.cmake" `
    "-DREXGLUE_SWITCH_NVK_SDK=$nvk" "-DBAKUGAN_GLSLANG_VALIDATOR=$glslang" "-DDEVKITPRO=$devkitpro" `
    "-DCMAKE_MAKE_PROGRAM=$((Get-Command ninja).Source)" -DBAKUGAN_SWITCH_LTO=ON "-DBAKUGAN_SWITCH_LTO_JOBS=$LtoJobs" "-DBAKUGAN_PGO=$Mode"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& cmake --build $build -j $Jobs
exit $LASTEXITCODE
