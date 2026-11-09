# XMF Flash Tool - download the official Android platform-tools (adb + fastboot).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_platform_tools.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_platform_tools.ps1 -Force
#
# The zip is downloaded from Google's official mirror and only the files the
# device layer needs are copied to data\platform-tools (the data directory is
# copied next to XMF.exe by the build, so the shipped tool is portable).

param(
    [switch]$Force,
    [string]$Dest = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrEmpty($Dest)) { $Dest = Join-Path $root 'data\platform-tools' }
$url  = 'https://dl.google.com/android/repository/platform-tools-latest-windows.zip'
$cacheDir = Join-Path $env:TEMP 'xmf-platform-tools'
$zip  = Join-Path $cacheDir 'platform-tools-latest-windows.zip'

# adb.exe / fastboot.exe plus the USB API helper DLLs. libwinpthread is shipped
# with the Google build and keeps adb working on Windows when it is not present
# in the system PATH.
$wanted = @(
    'adb.exe',
    'fastboot.exe',
    'AdbWinApi.dll',
    'AdbWinUsbApi.dll',
    'libwinpthread-1.dll',
    'source.properties',
    'NOTICE.txt'
)

$missing = @()
foreach ($name in $wanted) {
    if (-not (Test-Path (Join-Path $Dest $name))) { $missing += $name }
}
if ((-not $Force) -and ($missing.Count -eq 0)) {
    Write-Host "platform-tools already present in $Dest" -ForegroundColor Green
    exit 0
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null

$needDownload = $true
if ((Test-Path $zip) -and (-not $Force)) {
    try {
        $existing = Get-Item $zip
        if ($existing.Length -gt 5MB) { $needDownload = $false }
    } catch { $needDownload = $true }
}

if ($needDownload) {
    Write-Host "Downloading $url" -ForegroundColor Cyan
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -L --fail --retry 3 -o $zip $url
        if ($LASTEXITCODE -ne 0) { throw "curl failed with exit code $LASTEXITCODE" }
    } else {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    }
}

$extractDir = Join-Path $cacheDir ('extract-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -Path $zip -DestinationPath $extractDir -Force
    $src = Join-Path $extractDir 'platform-tools'
    if (-not (Test-Path $src)) { throw "unexpected archive layout: platform-tools\ not found" }
    foreach ($name in $wanted) {
        $from = Join-Path $src $name
        if (Test-Path $from) {
            Copy-Item -Force $from (Join-Path $Dest $name)
        } elseif ($name -ne 'NOTICE.txt' -and $name -ne 'source.properties' -and $name -ne 'libwinpthread-1.dll') {
            throw "required file missing from archive: $name"
        }
    }
} finally {
    Remove-Item -Recurse -Force $extractDir -ErrorAction SilentlyContinue
}

Write-Host "platform-tools installed -> $Dest" -ForegroundColor Green
Get-ChildItem $Dest | Where-Object { $wanted -contains $_.Name } |
    Select-Object Name, Length | Format-Table -AutoSize
