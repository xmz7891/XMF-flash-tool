# Rename product identifiers from XMF -> XMF across the source tree.
# One-off maintenance script; safe to run multiple times.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exts = @('.cpp','.h','.qml','.txt','.cmake','.ps1','.py','.md','.json','.qrc','.xml','.bat','.cmd','.gitignore')
$skip = @('_research','build','.git','resources\icons','resources\fonts','resources\images')

$files = Get-ChildItem -Path $root -Recurse -File | Where-Object {
    $rel = $_.FullName.Substring($root.Length + 1)
    ($exts -contains $_.Extension -or $_.Name -eq '.gitignore') -and
    (-not ($skip | Where-Object { $rel -like "$_*" }))
}

$changed = 0
foreach ($f in $files) {
    $text = Get-Content -Path $f.FullName -Raw -Encoding UTF8
    if ($null -eq $text) { continue }
    $orig = $text
    $text = $text.Replace('XMF', 'XMF')
    $text = $text.Replace('xmf', 'xmf')
    $text = $text.Replace('XMF', 'XMF')
    $text = $text.Replace('xmf_', 'xmf_')
    $text = $text.Replace('xmf-', 'xmf-')
    $text = $text.Replace('xmf', 'xmf')
    $text = $text.Replace('XMF', 'XMF')
    $text = $text.Replace('xmf', 'xmf')
    if ($text -ne $orig) {
        [System.IO.File]::WriteAllText($f.FullName, $text, (New-Object System.Text.UTF8Encoding($false)))
        $changed++
        Write-Host "updated $($f.FullName.Substring($root.Length + 1))"
    }
}
Write-Host "files changed: $changed"
