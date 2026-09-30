<#
.SYNOPSIS
    Creates a deterministic ZIP of a directory.

.DESCRIPTION
    Entries are added in ordinal-sorted order with forward-slash names and one
    fixed timestamp (default: the HEAD commit time), so the same staged tree
    always produces a byte-identical archive - unlike Compress-Archive, which
    records each file's mtime in filesystem order.

.PARAMETER SourceDir
    Directory to archive.
.PARAMETER DestinationPath
    ZIP file to create (overwritten).
.PARAMETER RootFolderName
    Optional folder name every entry is placed under (e.g. HexEngine-0.4.0), so
    extracting yields one tidy folder.
.PARAMETER Timestamp
    Fixed entry timestamp. Default: HEAD commit time.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceDir,
    [Parameter(Mandatory)][string]$DestinationPath,
    [string]$RootFolderName,
    [datetime]$Timestamp
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$src = (Resolve-Path -LiteralPath $SourceDir).Path.TrimEnd('\')
if (-not $PSBoundParameters.ContainsKey('Timestamp')) {
    $unix = (& git -C $src log -1 --format=%ct 2>$null)
    $Timestamp = if ($LASTEXITCODE -eq 0 -and $unix) { [DateTimeOffset]::FromUnixTimeSeconds([long]$unix).UtcDateTime } else { [datetime]'2000-01-01' }
}
# ZIP timestamps are DOS local time with 2 s resolution and a 1980 floor.
if ($Timestamp.Year -lt 1980) { $Timestamp = [datetime]'1980-01-01' }

# Resolve against PowerShell's location (IO.Path would use the process directory).
$dest = $PSCmdlet.GetUnresolvedProviderPathFromPSPath($DestinationPath)
New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
if (Test-Path -LiteralPath $dest) { Remove-Item -Force -LiteralPath $dest }

$files = Get-ChildItem -LiteralPath $src -Recurse -File |
    ForEach-Object { [pscustomobject]@{ Full = $_.FullName; Name = $_.FullName.Substring($src.Length + 1).Replace('\', '/') } } |
    Sort-Object -Property Name -CaseSensitive

$stream = [IO.File]::Open($dest, [IO.FileMode]::CreateNew)
try {
    $zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in $files) {
            $entryName = if ($RootFolderName) { "$RootFolderName/$($f.Name)" } else { $f.Name }
            $entry = $zip.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]$Timestamp
            $in = [IO.File]::OpenRead($f.Full)
            $out = $entry.Open()
            try { $in.CopyTo($out) } finally { $out.Dispose(); $in.Dispose() }
        }
    }
    finally { $zip.Dispose() }
}
finally { $stream.Dispose() }

$size = (Get-Item -LiteralPath $dest).Length
Write-Host ("Wrote {0} ({1} files, {2:N1} MB)" -f $dest, @($files).Count, ($size / 1MB))
