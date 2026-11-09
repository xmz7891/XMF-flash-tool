param(
    [ValidateSet('Debug','Release','RelWithDebInfo')]
    [string]$BuildType = 'Debug',
    [string]$BuildDir = '',
    [string]$Target = '',
    [switch]$Clean,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')

$root = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrEmpty($BuildDir)) { $BuildDir = "build\$BuildType" }
$build = Join-Path $root $BuildDir
if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

# NOTE: every -D argument must be quoted; PowerShell 5.1 does not expand
# variables inside bare tokens that start with '-'.
$configureArgs = @(
    '-S', $root,
    '-B', $build,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_PREFIX_PATH=$env:XMF_QT_ROOT",
    "-DCMAKE_C_COMPILER=$env:XMF_MINGW_ROOT\bin\gcc.exe",
    "-DCMAKE_CXX_COMPILER=$env:XMF_MINGW_ROOT\bin\g++.exe",
    "-DCMAKE_MAKE_PROGRAM=$env:XMF_NINJA"
)
& $env:XMF_CMAKE @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
if ($ConfigureOnly) { return }

if ($Target) {
    & $env:XMF_CMAKE --build $build --target $Target
} else {
    & $env:XMF_CMAKE --build $build
}
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Write-Host "Build OK -> $build\bin" -ForegroundColor Green
