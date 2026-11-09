param(
    [string]$Exe = "C:\Users\shu\Desktop\sjgj\build\Debug\bin\XMF.exe",
    [string]$Out = "C:\Users\shu\Desktop\sjgj\build\embed_shot.png",
    [string]$AppArgs = "",
    [int]$WaitSeconds = 12
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AppCap {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
"@
Add-Type -AssemblyName System.Drawing

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
$psi.WorkingDirectory = (Split-Path $Exe -Parent)
$psi.Arguments = $AppArgs
$psi.UseShellExecute = $true
$proc = [System.Diagnostics.Process]::Start($psi)
Start-Sleep -Seconds $WaitSeconds
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { Write-Host "no main window"; $proc.Kill(); exit 1 }
[AppCap]::ShowWindow($hwnd, 9) | Out-Null
[AppCap]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 800
$rect = New-Object AppCap+RECT
[AppCap]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bmp = New-Object System.Drawing.Bitmap($width, $height)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
Write-Host "saved $Out ($width x $height) title=$($proc.MainWindowTitle)"
$proc.Kill()
Get-Process XiaoMiFlash,flash_tool,SPRDClientCore,XMF -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
