# shots.ps1: runs Star Blade (our build) and captures its window at given seconds.
param([string]$Exe = "C:\tmp\starblade-emu\starblade_emu.exe", [string]$Out = "C:\tmp\sbcmp", [int[]]$At = @(20,25,30,35,40,45,50,60,70,80,90,100,110,120,140,160,180))
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W { [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  public struct RECT { public int L, T, R, B; } }
"@
$p = Start-Process -FilePath $Exe -ArgumentList "--windowed","--roms","C:\tmp\mame\roms" -WorkingDirectory (Split-Path $Exe) -PassThru
$t0 = Get-Date
foreach ($s in $At) {
  while (((Get-Date) - $t0).TotalSeconds -lt $s) { Start-Sleep -Milliseconds 50 }
  $p.Refresh(); $h = $p.MainWindowHandle
  if ($h -eq 0) { Write-Output "no window at $s"; continue }
  $r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc(); [W]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
  $bmp.Save("$Out\ours_$s.png", [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
  Write-Output "shot $s"
}
Stop-Process -Id $p.Id
