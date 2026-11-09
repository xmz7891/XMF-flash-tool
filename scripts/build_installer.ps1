# Builds the Inno Setup installer from the full portable package.
# Requires the portable package first:
#   scripts\package.ps1 -BuildDir build\Release
param(
    [string]$Root = (Join-Path $PSScriptRoot '..'),
    [string]$Iscc = ''
)

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath($Root)
$iss = Join-Path $root 'installer\XMF.iss'
$source = Join-Path $root 'dist\XMF-Flash-Tool-full\XMF.exe'

if (-not (Test-Path $source)) {
    Write-Host "portable package missing: $source" -ForegroundColor Red
    Write-Host "run scripts\\package.ps1 first" -ForegroundColor Yellow
    exit 2
}
if ([string]::IsNullOrEmpty($Iscc)) {
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )) {
        if (Test-Path $candidate) { $Iscc = $candidate; break }
    }
}
if ([string]::IsNullOrEmpty($Iscc) -or -not (Test-Path $Iscc)) {
    Write-Host "ISCC.exe (Inno Setup 6) not found; install it from https://jrsoftware.org/isdl.php" -ForegroundColor Red
    exit 3
}

New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist\installer') | Out-Null
Write-Host "ISCC: $Iscc"
& $Iscc "/Qp" $iss
if ($LASTEXITCODE -ne 0) {
    Write-Host "ISCC failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
$output = Get-ChildItem (Join-Path $root 'dist\installer') -Filter 'XMF-Flash-Tool-Setup-*.exe' |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $output) {
    Write-Host "installer output not found" -ForegroundColor Red
    exit 4
}
Write-Host ("Installer: {0} ({1:N1} MB)" -f $output.FullName, ($output.Length / 1MB)) -ForegroundColor Green
