# Capture the emulator's window at fixed frames, for a benchmark run.
#
# The port's own counters are the primary evidence; a capture at a fixed
# script frame is the visual half of it ("captures at fixed script frames are
# unchanged from baseline", 000.md). The images are *game content*, so the
# repository's content gate (tools/check_repo_content.py) keeps them out of
# the tree: they are written here and are meant to be looked at, not
# committed.
#
# Usage (Windows, with Azahar installed and the port built):
#
#   powershell -File tools/capture_bench_frames.ps1 `
#       -Artifact build/game-3ds/pokeheartgold.3dsx `
#       -Script tests/inputs/hg-continue-field.txt `
#       -Frames 11400,12600 -Tag field
#
# The window is read with PrintWindow, which asks the window to render
# itself into a bitmap *without* being activated: the focus, and whatever
# else is on the screen, are left alone.

param(
  [string]$Artifact = "build/game-3ds/pokeheartgold.3dsx",
  [string]$Script = "tests/inputs/hg-continue-field.txt",
  [string]$Frames = "11400,12600",
  [string]$Tag = "bench",
  [string]$OutDir = "$env:TEMP\hgss-captures",
  [int]$Seconds = 420
)

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repo
Add-Type -AssemblyName System.Drawing
$sig = @'
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -MemberDefinition $sig -Name "W" -Namespace "N" -PassThru | Out-Null

$sd = Join-Path $env:APPDATA "Azahar\sdmc\3ds"
$data = Join-Path $sd "pokeheartgold"
$log = Join-Path $data "log.txt"
$ini = "docs/bench/port-baseline.ini"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Copy-Item $Artifact (Join-Path $sd "pokeheartgold.3dsx") -Force
Copy-Item "build/bench/heartgold.sav" (Join-Path $data "heartgold.sav") -Force
Copy-Item $Script (Join-Path $data "script.txt") -Force
Copy-Item $ini (Join-Path $data "port.ini") -Force
Remove-Item $log -ErrorAction SilentlyContinue

Start-Process -FilePath "C:\Program Files\Azahar\azahar.exe" -ArgumentList (Join-Path $sd "pokeheartgold.3dsx")
Write-Output "launched; waiting for frames $Frames"

$deadline = (Get-Date).AddSeconds($Seconds)
foreach ($fs in $Frames.Split(',')) {
  $f = [int]$fs
  $got = $false
  while ((Get-Date) -lt $deadline -and -not $got) {
    if (Test-Path $log) {
      if (Select-String -Path $log -Pattern "^\[$f\] frame $f" -ErrorAction SilentlyContinue) {
        $p = Get-Process azahar -ErrorAction SilentlyContinue |
             Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
        if ($p) {
          $r = New-Object N.W+RECT
          [N.W]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
          $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
          $bmp = New-Object System.Drawing.Bitmap $w, $h
          $g = [System.Drawing.Graphics]::FromImage($bmp)
          $hdc = $g.GetHdc()
          [N.W]::PrintWindow($p.MainWindowHandle, $hdc, 0) | Out-Null
          $g.ReleaseHdc($hdc); $g.Dispose()
          $out = Join-Path $OutDir ("capture-{0}-{1}.png" -f $Tag, $f)
          $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
          $bmp.Dispose()
          Write-Output "captured frame $f -> $out"
          $got = $true
        }
      }
    }
    if (-not $got) { Start-Sleep -Milliseconds 250 }
  }
  if (-not $got) { Write-Output "MISSED frame $f (the run's end, or the frame was not reached)" }
}
Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir ("run-{0}.log" -f $Tag)) -Force }
Write-Output "done; the images are in $OutDir (they are game content: not for the repository)"
