# Runs one command line in the Windows build environment (tools\win\env.ps1) from a working directory and writes its
# whole output to a log file (the redirect is done by cmd.exe so a stderr line does not stop PowerShell 5.1).
#   pwsh -NoProfile -File run.ps1 <log> "<command>" [<workdir>]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Log,
    [Parameter(Mandatory = $true, Position = 1)][string]$Cmd,
    [Parameter(Position = 2)][string]$WorkDir = ''
)
. (Join-Path $PSScriptRoot 'env.ps1')
if ($WorkDir) { Set-Location $WorkDir }
New-Item -ItemType Directory -Force (Split-Path -Parent (Join-Path (Get-Location) $Log)) | Out-Null
& $env:ComSpec /c "$Cmd > `"$Log`" 2>&1"
$code = $LASTEXITCODE
Get-Content $Log -Tail 40
"exit=$code"
exit $code
