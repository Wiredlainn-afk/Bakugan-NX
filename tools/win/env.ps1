# Loads the Windows build environment of the Bakugan port into the current PowerShell session, for the PORTABLE layout
# (no Visual Studio install, no admin): LLVM clang 20, the MSVC CRT + Windows SDK splatted by xwin, CMake + Ninja.
#
#   . tools\win\env.ps1
#
# Layout (override the root with $env:BAKUGAN_TOOLS): <tools>\llvm, <tools>\msvc\{crt,sdk}, <tools>\venv (cmake, ninja).
$ErrorActionPreference = 'Stop'
$tools = if ($env:BAKUGAN_TOOLS) { $env:BAKUGAN_TOOLS } else { 'C:akugan-tools' }
$llvm = Join-Path $tools 'llvm'
$msvc = Join-Path $tools 'msvc'
$venv = Join-Path $tools 'venv\Scripts'
foreach ($p in "$llvm\bin\clang++.exe", "$msvc\crt\include", "$msvc\sdk\include\ucrt", "$venv\cmake.exe", "$venv\ninja.exe") {
    if (-not (Test-Path $p)) { throw "missing $p (see docs/toolchain-windows.md)" }
}
if (-not $env:BAKUGAN_ENV_LOADED) {
    $env:INCLUDE = @("$msvc\crt\include", "$msvc\sdk\include\ucrt", "$msvc\sdk\include\um", "$msvc\sdk\include\shared",
                     "$msvc\sdk\include\winrt", "$msvc\sdk\include\cppwinrt") -join ';'
    $env:LIB = @("$msvc\crt\lib\x86_64", "$msvc\sdk\lib\ucrt\x86_64", "$msvc\sdk\lib\um\x86_64") -join ';'
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch 'msys2[\\/]usr[\\/]bin' }) -join ';'
    $env:PATH = "$llvm\bin;$venv;$env:PATH"
    $env:CC = 'clang'
    $env:CXX = 'clang++'
    $env:BAKUGAN_ENV_LOADED = '1'
}
$ErrorActionPreference = 'Continue'
