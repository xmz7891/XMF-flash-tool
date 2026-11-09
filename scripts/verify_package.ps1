# Portable package verification for XMF Flash Tool.
# Runs the built-in self test and checks that the bundled runtime data exists.
param(
    # Full portable package (lite has no bundled data/tools).
    [string]$AppDir = (Join-Path $PSScriptRoot '..\dist\XMF-Flash-Tool-full'),
    [string]$Exe = ''
)

$ErrorActionPreference = 'Continue'
$appDir = [System.IO.Path]::GetFullPath($AppDir)
if ([string]::IsNullOrEmpty($Exe)) { $Exe = Join-Path $appDir 'XMF.exe' }

$failures = New-Object System.Collections.Generic.List[string]
$checks = 0

function Check-Path([string]$label, [string]$path) {
    $script:checks++
    if (Test-Path $path) {
        Write-Host ("[ OK ] {0}: {1}" -f $label, $path)
    } else {
        $script:failures.Add("$label missing: $path")
        Write-Host ("[FAIL] {0}: {1}" -f $label, $path) -ForegroundColor Red
    }
}

Write-Host "XMF package verification"
Write-Host "App dir: $appDir"
Check-Path 'XMF.exe' $Exe
Check-Path 'data/packets' (Join-Path $appDir 'data\packets')
Check-Path 'data/platform-tools' (Join-Path $appDir 'data\platform-tools')
Check-Path 'data/drivers' (Join-Path $appDir 'data\drivers')
Check-Path 'data/thirdparty' (Join-Path $appDir 'data\thirdparty')
Check-Path 'data/thirdparty/oss/zstd/zstd.exe' (Join-Path $appDir 'data\thirdparty\oss\zstd\zstd.exe')
Check-Path 'docs/USER_GUIDE.md' (Join-Path $appDir 'docs\USER_GUIDE.md')
Check-Path 'docs/FEATURE_COVERAGE.md' (Join-Path $appDir 'docs\FEATURE_COVERAGE.md')

if (Test-Path $Exe) {
    Write-Host ''
    Write-Host 'Running built-in self test...'
    & $Exe --run-tool system.selfTest 2>&1 | ForEach-Object { Write-Host "  $_" }
    $code = $LASTEXITCODE
    $script:checks++
    if ($code -eq 0) {
        Write-Host '[ OK ] environment self test passed'
    } else {
        $script:failures.Add("self test failed with exit code $code")
        Write-Host '[FAIL] environment self test failed' -ForegroundColor Red
    }
}

Write-Host ''
if ($failures.Count -eq 0) {
    Write-Host ("PASS: {0} checks OK" -f $checks) -ForegroundColor Green
    exit 0
}
Write-Host ("FAIL: {0}/{1} checks failed" -f $failures.Count, $checks) -ForegroundColor Red
$failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
exit 1
