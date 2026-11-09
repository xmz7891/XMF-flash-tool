# UI smoke test: opens every main page (and one workbench) and saves a screenshot.
# Fails when a page cannot be rendered or the process times out.
param(
    # Defaults to the portable (self-contained) build. For a Debug build, dot-source
    # scripts/env.ps1 first so the Qt DLLs are on PATH.
    [string]$Exe = (Join-Path $PSScriptRoot '..\dist\XMF-Flash-Tool\XMF.exe'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\build\smoke'),
    [int]$DelayMs = 2200
)

$ErrorActionPreference = 'Continue'
$exePath = [System.IO.Path]::GetFullPath($Exe)
if (-not (Test-Path $exePath)) {
    Write-Host "XMF.exe not found: $exePath" -ForegroundColor Red
    exit 2
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$pages = @(
    @{ name = 'overview'; args = '--page 0' },
    @{ name = 'flash'; args = '--page 1' },
    @{ name = 'tools'; args = '--page 2' },
    @{ name = 'modes'; args = '--page 3' },
    @{ name = 'packets'; args = '--page 4' },
    @{ name = 'firmware'; args = '--page 5' },
    @{ name = 'batch'; args = '--page 6' },
    @{ name = 'logs'; args = '--page 7' },
    @{ name = 'official'; args = '--page 8' },
    @{ name = 'settings'; args = '--page 9' },
    @{ name = 'console'; args = '--page 10' },
    @{ name = 'workbench-miflash'; args = '--mode miflash' },
    @{ name = 'workbench-fastboot'; args = '--flash-mode fastboot:qcom' },
    @{ name = 'workbench-recovery'; args = '--flash-mode recovery:qcom' },
    @{ name = 'workbench-qcom-edl'; args = '--flash-mode qcom-edl' }
)

# Starts one XMF instance, waits for the screenshot and returns the result.
function Invoke-PageCapture([hashtable]$page, [string]$shot) {
    Get-Process XMF -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 350
    Remove-Item $shot -ErrorAction SilentlyContinue
    $arguments = @('--theme', 'light', '--no-auto-switch', '--screenshot', $shot, '--screenshot-delay', "$DelayMs")
    $arguments += $page.args.Split(' ')
    $proc = Start-Process -FilePath $exePath -ArgumentList $arguments -PassThru
    $finished = $proc.WaitForExit($DelayMs + 12000)
    if (-not $finished) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        return @{ ok = $false; reason = 'timeout' }
    }
    if (-not (Test-Path $shot)) {
        return @{ ok = $false; reason = "no screenshot (exit $($proc.ExitCode))" }
    }
    $size = (Get-Item $shot).Length
    if ($size -lt 10000) {
        return @{ ok = $false; reason = "screenshot too small ($size bytes)" }
    }
    return @{ ok = $true; size = $size }
}

$failed = New-Object System.Collections.Generic.List[string]
foreach ($page in $pages) {
    $shot = Join-Path $OutDir ("{0}.png" -f $page.name)
    $result = @{ ok = $false; reason = 'not started' }
    # One retry absorbs transient process start-up races.
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        $result = Invoke-PageCapture $page $shot
        if ($result.ok) { break }
        if ($attempt -lt 2) { Start-Sleep -Milliseconds 800 }
    }
    if ($result.ok) {
        Write-Host ("[ OK ] {0} ({1} bytes)" -f $page.name, $result.size) -ForegroundColor Green
    } else {
        $failed.Add("$($page.name): $($result.reason)")
        Write-Host ("[FAIL] {0}: {1}" -f $page.name, $result.reason) -ForegroundColor Red
    }
}
Get-Process XMF -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host ''
if ($failed.Count -eq 0) {
    Write-Host ("PASS: {0} pages rendered" -f $pages.Count) -ForegroundColor Green
    exit 0
}
Write-Host ("FAIL: {0} page(s)" -f $failed.Count) -ForegroundColor Red
$failed | ForEach-Object { Write-Host "  - $_" }
exit 1
