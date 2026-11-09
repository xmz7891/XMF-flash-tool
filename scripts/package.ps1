# Build the portable, double-clickable distribution.
#   dist/XMF-Flash-Tool/XMF.exe          portable folder (runs immediately)
#   dist/XMF-Flash-Tool-v<ver>-full.zip  app + official tools + packs + drivers
#   dist/XMF-Flash-Tool-v<ver>-lite.zip  app only (user-supplied data folder)
param(
    [string]$BuildDir = 'build\Release',
    [switch]$SkipZip,
    [switch]$SkipBuild,
    [switch]$Full
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')
$root = Split-Path $PSScriptRoot -Parent

$build = Join-Path $root $BuildDir
$exe = Join-Path $build 'bin\XMF.exe'
if (-not $SkipBuild) {
    # Tests are not part of the portable build: configure with GF_BUILD_TESTS=OFF
    & $env:XMF_CMAKE -S $root -B $build -G Ninja `
        "-DCMAKE_BUILD_TYPE=Release" `
        "-DCMAKE_PREFIX_PATH=$env:XMF_QT_ROOT" `
        "-DCMAKE_C_COMPILER=$env:XMF_MINGW_ROOT\bin\gcc.exe" `
        "-DCMAKE_CXX_COMPILER=$env:XMF_MINGW_ROOT\bin\g++.exe" `
        "-DCMAKE_MAKE_PROGRAM=$env:XMF_NINJA" `
        "-DGF_BUILD_TESTS=OFF"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $env:XMF_CMAKE --build $build --target XMF
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
}
if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }

$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(XMF VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$distRoot = Join-Path $root 'dist'
$appDir = Join-Path $distRoot 'XMF-Flash-Tool'
if (Test-Path $appDir) { Remove-Item -Recurse -Force $appDir }
New-Item -ItemType Directory -Force -Path $appDir | Out-Null
Copy-Item $exe (Join-Path $appDir 'XMF.exe')

# ---- Qt runtime -------------------------------------------------------------
$windeployqt = Join-Path $env:XMF_QT_ROOT 'bin\windeployqt.exe'
& $windeployqt --release --compiler-runtime --no-translations --no-opengl-sw `
    --qmldir (Join-Path $root 'qml') (Join-Path $appDir 'XMF.exe') | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

# ---- docs (always) ----------------------------------------------------------
$docsDir = Join-Path $appDir 'docs'
New-Item -ItemType Directory -Force -Path $docsDir | Out-Null
$scriptsDir = Join-Path $appDir 'scripts'
New-Item -ItemType Directory -Force -Path $scriptsDir | Out-Null
foreach ($name in @('verify_package.ps1', 'smoke_test.ps1', 'build_installer.ps1')) {
    $src = Join-Path $root ('scripts\' + $name)
    if (Test-Path $src) { Copy-Item $src $scriptsDir -Force }
}
Copy-Item (Join-Path $root 'README.md') $appDir -Force
if (Test-Path (Join-Path $root 'docs')) {
    Copy-Item (Join-Path $root 'docs\*.md') $docsDir -Force
}

$licenses = @'
XMF Flash Tool
==============
Qt 6.8.3 (LGPL v3) - https://www.qt.io
MinGW-w64 runtime
Lucide icons (MIT) - https://lucide.dev
Inter font (SIL OFL 1.1) - https://rsms.me/inter
miniz (MIT) - https://github.com/richgel999/miniz

Bundled official vendor tools (optional backend, user-supplied):
  MiFlash (Xiaomi), SP Flash Tool (MediaTek), Unisoc Research Download (Unisoc)
  plus their bundled drivers. Copyright belongs to the respective vendors.
GeekFlashPacket resource packages are provided by the user.
'@
Set-Content -Path (Join-Path $appDir 'THIRD-PARTY-NOTICES.txt') -Value $licenses -Encoding UTF8

function New-Zip([string]$sourceDir, [string]$zipPath) {
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Write-Host "Compressing $sourceDir -> $zipPath ..." -ForegroundColor Yellow
    $sevenZip = Join-Path $root '_research\tools\7zip\7z.exe'
    if (-not (Test-Path $sevenZip)) {
        $sevenZip = 'C:\Program Files\7-Zip\7z.exe'
    }
    if (Test-Path $sevenZip) {
        & $sevenZip a -tzip -mx=5 -bso0 -bsp0 $zipPath (Join-Path $sourceDir '*') | Out-Host
    } else {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::CreateFromDirectory($sourceDir, $zipPath,
            [System.IO.Compression.CompressionLevel]::Optimal, $true)
    }
    $sizeMb = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
    Write-Host "Package: $zipPath ($sizeMb MB)" -ForegroundColor Green
}

# ---- lite package: app + docs only -----------------------------------------
New-Zip $appDir (Join-Path $distRoot "XMF-Flash-Tool-v$version-lite.zip")

if ($Full -or -not $SkipZip) {
    # ---- full package: include all runtime data ----------------------------
    $fullDir = Join-Path $distRoot 'XMF-Flash-Tool-full'
    if (Test-Path $fullDir) { Remove-Item -Recurse -Force $fullDir }
    Copy-Item -Recurse $appDir $fullDir
    $dataSrc = Join-Path $root 'data'
    if (Test-Path $dataSrc) {
        Write-Host 'Copying data (official tools + drivers + packs + platform-tools)...' -ForegroundColor Yellow
        Copy-Item -Recurse -Force $dataSrc (Join-Path $fullDir 'data')
    }
    New-Zip $fullDir (Join-Path $distRoot "XMF-Flash-Tool-v$version-full.zip")
}

Write-Host "Portable folder ready: $appDir" -ForegroundColor Green
