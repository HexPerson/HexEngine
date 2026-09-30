<#
.SYNOPSIS
    Copies the source-controlled engine data (Content/EngineData) into a
    development build's Bin\x64\<Config>\Data folder.

.DESCRIPTION
    Content/EngineData is the source of truth for engine assets (textures,
    fonts, materials, models, templates) and is what releases ship. A local
    build reads Bin\x64\<Config>\Data; run this after pulling asset changes.

    Files are copied over existing ones; nothing is deleted, so build outputs
    in the same folder (compiled Shaders, Cache, AssetPackages) are untouched.
    Requires `git lfs pull` to have fetched the binary assets.

.PARAMETER Configuration
    Debug, Release, or both (default).
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string[]]$Configuration = @('Debug', 'Release')
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$src = Join-Path $repo 'Content\EngineData'

$pointer = Get-ChildItem -LiteralPath $src -Recurse -File |
    Where-Object { $_.Length -lt 200 -and $_.Extension -in '.png', '.jpg', '.ttf', '.hmesh' } |
    Where-Object { (Get-Content -LiteralPath $_.FullName -TotalCount 1) -like 'version https://git-lfs*' } |
    Select-Object -First 1
if ($pointer) { throw "Content/EngineData holds Git LFS pointers (e.g. $($pointer.Name)). Run 'git lfs install' then 'git lfs pull' first." }

foreach ($cfg in $Configuration) {
    $dst = Join-Path $repo "Bin\x64\$cfg\Data"
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    $count = 0
    Get-ChildItem -LiteralPath $src -Recurse -File | ForEach-Object {
        $target = Join-Path $dst $_.FullName.Substring($src.Length + 1)
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
        $count++
    }
    Write-Host "Synced $count files -> $dst"
}
