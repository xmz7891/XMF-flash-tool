param(
    [string]$Exe = "C:\Users\shu\Desktop\sjgj\_research\reference\GeekFlashTool.exe",
    [string]$Out = "C:\Users\shu\Desktop\sjgj\_research\reference\shot_reference.png",
    [int]$WaitSeconds = 7
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCap {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
"@
Add-Type -AssemblyName System.Drawing

$workDir = Split-Path $Exe -Parent
$proc = Start-Process -FilePath $Exe -WorkingDirectory $workDir -PassThru
Start-Sleep -Seconds $WaitSeconds
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "no main window (pid=$($proc.Id))"
    Get-Process -Id $proc.Id | Select-Object Id,ProcessName,MainWindowTitle
    exit 1
}
[WinCap]::ShowWindow($hwnd, 9) | Out-Null   # SW_RESTORE
[WinCap]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 800
$rect = New-Object WinCap+RECT
[WinCap]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
Write-Host "window $width x $height at $($rect.Left),$($rect.Top) title=$($proc.MainWindowTitle)"
$bmp = New-Object System.Drawing.Bitmap($width, $height)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose()
$bmp.Dispose()
Write-Host "saved $Out"
Start-Sleep -Seconds 1
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
