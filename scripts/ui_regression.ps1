param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\dist\XMF-Flash-Tool-full\XMF.exe'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\build\ui-regression')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$sizes = @('980x620', '1180x680', '1600x900')
$pages = @(
    @{ id = 'flash'; args = @('--page', '0') },
    @{ id = 'tools'; args = @('--page', '1') },
    @{ id = 'modes'; args = @('--page', '2') },
    @{ id = 'guide'; args = @('--page', '3') },
    @{ id = 'firmware'; args = @('--page', '4') },
    @{ id = 'batch'; args = @('--page', '6') },
    @{ id = 'logs'; args = @('--page', '7') },
    @{ id = 'official'; args = @('--page', '8') },
    @{ id = 'settings'; args = @('--page', '9') },
    @{ id = 'qcom9008'; args = @('--flash-mode', 'qcom-edl') },
    @{ id = 'amlogic'; args = @('--flash-mode', 'amlogic-burn') }
)
$fail = 0
foreach ($p in $pages) {
    foreach ($s in $sizes) {
        $file = Join-Path $OutDir ("{0}_{1}.png" -f $p.id, $s)
        $argList = @('--theme', 'dark') + $p.args + @('--window-size', $s,
            '--screenshot', $file, '--screenshot-delay', '900')
        $proc = Start-Process -FilePath $Exe -ArgumentList $argList -PassThru -WindowStyle Hidden
        if (-not $proc.WaitForExit(12000)) { $proc.Kill() }
        if (-not (Test-Path $file) -or (Get-Item $file).Length -lt 10000) {
            Write-Host "FAIL $($p.id) $s" -ForegroundColor Red
            $fail++
        } else {
            Write-Host "ok   $($p.id) $s"
        }
    }
}
Write-Host "regression failures: $fail"
exit $fail
