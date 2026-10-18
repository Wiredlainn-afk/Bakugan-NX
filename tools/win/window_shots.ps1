# VISIBLE run (needs the user's OK): starts the PC build with a normal window (muted), then saves PNGs of the window at
# the given seconds using PrintWindow. For comparing the emulated Xenos GPU with the native renderer on the same scene.
#   powershell -NoProfile -File tools\win\window_shots.ps1 -Native 0 -At 150,160,170 -Out out\emu_shots
param([int]$Native = 0, [string]$At = "156", [string]$Out = 'out\window_shots', [string]$Script = '', [int]$Total = 0)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe = Join-Path $root 'out\pc\bakugan.exe'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public class WinCap {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
New-Item -ItemType Directory -Force (Join-Path $root $Out) | Out-Null
$env:REX_BAKUGAN_RENDERER_NATIVE = if ($Native) { 'true' } else { 'false' }
$env:REX_AUDIO_MUTE = 'true'
$env:REX_WINDOW_START_MINIMIZED = 'false'
if ($Script) { $env:REX_INPUT_SCRIPT = $Script }
$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
$t0 = Get-Date
foreach ($s in ($At -split "," | ForEach-Object { [int]$_ })) {
    $wait = $s - ((Get-Date) - $t0).TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Seconds $wait }
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { "no window at $s s"; continue }
    $r = New-Object WinCap+RECT
    [void][WinCap]::GetClientRect($h, [ref]$r)
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    if ($w -le 0 -or $hh -le 0) { "empty client at $s s"; continue }
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][WinCap]::PrintWindow($h, $hdc, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    $path = Join-Path $root ("{0}\shot_{1}s.png" -f $Out, $s)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "saved $path ($w x $hh)"
}
Stop-Process -Id $p.Id -Force
