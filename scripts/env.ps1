# XMF Flash Tool - build environment
# Usage:  . .\scripts\env.ps1
# Sets PATH so cl.exe-like invocations of g++, cmake, ninja and Qt tools work.

$QtRoot    = 'C:\Qt\6.8.3\mingw_64'
$MingwRoot = 'C:\Qt\Tools\mingw1310_64'
$CMakeExe  = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$NinjaExe  = 'C:\Qt\Tools\Ninja\ninja.exe'

foreach ($p in @($QtRoot, $MingwRoot, $CMakeExe, $NinjaExe)) {
    if (-not (Test-Path $p)) { throw "Missing build dependency: $p" }
}

$env:XMF_QT_ROOT    = $QtRoot
$env:XMF_MINGW_ROOT = $MingwRoot
$env:XMF_CMAKE      = $CMakeExe
$env:XMF_NINJA      = $NinjaExe
$env:CMAKE_PREFIX_PATH = $QtRoot
$env:PATH = "$MingwRoot\bin;$QtRoot\bin;$(Split-Path $CMakeExe);$(Split-Path $NinjaExe);$env:PATH"

Write-Host "XMF build env ready:" -ForegroundColor Green
Write-Host "  Qt      : $QtRoot"
Write-Host "  MinGW   : $MingwRoot"
Write-Host "  CMake   : $CMakeExe"
Write-Host "  Ninja   : $NinjaExe"
