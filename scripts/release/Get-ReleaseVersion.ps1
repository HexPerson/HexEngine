<#
.SYNOPSIS
    Derives the HexEngine release version from a Git tag.

.DESCRIPTION
    The Git tag is the single source of truth for a release. Tags must be
    vMAJOR.MINOR.PATCH (Semantic Versioning core, no pre-release/build suffix),
    e.g. v0.1.0, v0.2.1, v1.0.0. Leading zeros are rejected, as SemVer requires.

    Writes the version to stdout and, when running under GitHub Actions, also to
    $GITHUB_OUTPUT as `version`, `major`, `minor`, `patch` and `tag`.

.PARAMETER Tag
    The tag name. Defaults to $env:GITHUB_REF_NAME (set by GitHub Actions).

.EXAMPLE
    ./scripts/release/Get-ReleaseVersion.ps1 -Tag v0.4.0   # -> 0.4.0
#>
[CmdletBinding()]
param(
    [string]$Tag = $env:GITHUB_REF_NAME
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Tag)) {
    throw "No tag given (pass -Tag or run under GitHub Actions with GITHUB_REF_NAME set)."
}

$pattern = '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$'
$m = [regex]::Match($Tag, $pattern)
if (-not $m.Success) {
    throw "Tag '$Tag' is not a release tag. Expected vMAJOR.MINOR.PATCH, e.g. v0.1.0 (no leading zeros, no suffix)."
}

$major = [int]$m.Groups[1].Value
$minor = [int]$m.Groups[2].Value
$patch = [int]$m.Groups[3].Value
# FILEVERSION fields are 16-bit.
foreach ($part in @($major, $minor, $patch)) {
    if ($part -gt 65535) { throw "Version component $part exceeds 65535 (Windows VERSIONINFO limit)." }
}

$version = "$major.$minor.$patch"

if ($env:GITHUB_OUTPUT) {
    "version=$version" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "major=$major"     | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "minor=$minor"     | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "patch=$patch"     | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "tag=$Tag"         | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
}

Write-Output $version
