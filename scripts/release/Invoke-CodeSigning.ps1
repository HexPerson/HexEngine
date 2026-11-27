<#
.SYNOPSIS
    Authenticode-signs HexEngine release binaries with SignTool - or does
    nothing, cleanly, when no signing certificate is configured.

.DESCRIPTION
    Signing is enabled purely by GitHub Secrets; nothing secret is ever
    committed. When the secrets are absent this script prints a notice and
    exits 0 so unsigned releases still complete (pass -Required to make a
    missing certificate an error instead).

    Secrets / environment variables (see docs/RELEASING.md):
      WINDOWS_SIGNING_PFX_BASE64    base64 of the code-signing certificate (.pfx)
      WINDOWS_SIGNING_PFX_PASSWORD  its password
      WINDOWS_SIGNING_TIMESTAMP_URL optional RFC 3161 timestamp server
                                    (default http://timestamp.digicert.com)

    Files that ALREADY carry a valid signature are skipped: third-party DLLs
    signed by their vendor keep that signature. This matters -
    HexEngine.StreamlinePlugin verifies NVIDIA's signature on
    sl.interposer.dll at runtime, so re-signing it would disable DLSS.

    The PFX is decoded to a temp file, used, and deleted in a finally block.

.PARAMETER Path
    A directory (signed recursively: *.exe, *.dll) or a single file.
.PARAMETER Description
    Signature description (shown in UAC prompts).
.PARAMETER Required
    Fail if signing is not configured.
.PARAMETER EmitInnoSignToolCommand
    Instead of signing, print the SignTool command line (with $f placeholder)
    for Inno Setup's /S switch, or nothing if signing is disabled.
#>
[CmdletBinding()]
param(
    [string]$Path,
    [string]$Description = 'HexEngine',
    [switch]$Required,
    [switch]$EmitInnoSignToolCommand
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$pfxB64 = $env:WINDOWS_SIGNING_PFX_BASE64
$pfxPwd = $env:WINDOWS_SIGNING_PFX_PASSWORD
$tsUrl  = if ($env:WINDOWS_SIGNING_TIMESTAMP_URL) { $env:WINDOWS_SIGNING_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }

if ([string]::IsNullOrWhiteSpace($pfxB64)) {
    if ($Required) { throw 'Code signing is required but WINDOWS_SIGNING_PFX_BASE64 is not set.' }
    if (-not $EmitInnoSignToolCommand) {
        Write-Host '::notice::Code signing disabled (WINDOWS_SIGNING_PFX_BASE64 not set) - release binaries are unsigned.'
    }
    exit 0
}

function Find-SignTool {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $tool = Get-ChildItem -LiteralPath $kits -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
        Sort-Object { [version]($_.FullName -replace '.*\\bin\\([\d.]+)\\.*', '$1') } -Descending |
        Select-Object -First 1
    if (-not $tool) { throw "signtool.exe (x64) not found under $kits." }
    return $tool.FullName
}

$signtool = Find-SignTool
$tempDir = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$pfxPath = Join-Path $tempDir ("hexsign-{0}.pfx" -f [guid]::NewGuid().ToString('N'))

if ($EmitInnoSignToolCommand) {
    # Inno Setup runs this for the setup and its uninstaller; $f is the file,
    # $q a double quote. The PFX must outlive ISCC, so it is written to a
    # fixed temp path the workflow deletes afterwards.
    $pfxPath = Join-Path $tempDir 'hexsign-inno.pfx'
    [IO.File]::WriteAllBytes($pfxPath, [Convert]::FromBase64String($pfxB64))
    Write-Output ('"{0}" sign /fd SHA256 /f "{1}" /p $q{2}$q /tr {3} /td SHA256 /d $q{4}$q $f' -f $signtool, $pfxPath, $pfxPwd, $tsUrl, $Description)
    exit 0
}

if (-not $Path) { throw '-Path is required.' }
$files = if (Test-Path -LiteralPath $Path -PathType Container) {
    # (-Include is ignored with -LiteralPath in Windows PowerShell - filter explicitly.)
    @(Get-ChildItem -LiteralPath $Path -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' })
} else {
    @(Get-Item -LiteralPath $Path)
}

$toSign = @($files | Where-Object { (Get-AuthenticodeSignature -LiteralPath $_.FullName).Status -ne 'Valid' })
$skipped = $files.Count - $toSign.Count
Write-Host "Signing $($toSign.Count) file(s); $skipped already validly signed (vendor signatures kept)."
if ($toSign.Count -eq 0) { exit 0 }

try {
    [IO.File]::WriteAllBytes($pfxPath, [Convert]::FromBase64String($pfxB64))
    # Batches keep the command line short and the timestamp server happy.
    $batchSize = 20
    for ($i = 0; $i -lt $toSign.Count; $i += $batchSize) {
        $batch = $toSign[$i..([Math]::Min($i + $batchSize, $toSign.Count) - 1)] | ForEach-Object { $_.FullName }
        $signed = $false
        foreach ($attempt in 1..3) {
            & $signtool sign /fd SHA256 /f $pfxPath /p $pfxPwd /tr $tsUrl /td SHA256 /d $Description @batch
            if ($LASTEXITCODE -eq 0) { $signed = $true; break }
            Write-Warning "signtool attempt $attempt failed (exit $LASTEXITCODE); retrying (timestamp servers are flaky)."
            Start-Sleep -Seconds (5 * $attempt)
        }
        if (-not $signed) { throw 'signtool failed after 3 attempts.' }
    }
    & $signtool verify /pa /q @($toSign | ForEach-Object { $_.FullName })
    if ($LASTEXITCODE -ne 0) { throw 'signtool verify failed.' }
    Write-Host "Signed and verified $($toSign.Count) file(s)."
}
finally {
    if (Test-Path -LiteralPath $pfxPath) { Remove-Item -Force -LiteralPath $pfxPath }
}
