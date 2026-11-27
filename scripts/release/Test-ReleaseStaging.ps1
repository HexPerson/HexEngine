<#
.SYNOPSIS
    Validates a staged HexEngine release (dist\HexEngine) before it is packaged.

.DESCRIPTION
    Fails (non-zero exit) when:
      * a primary executable or the engine DLL is missing
      * development-only files leaked in (PDB/ILK/EXP/OBJ/iobj/ipdb, test
        binaries, build logs, .lib outside SDK\Lib, sample/Steamworks plugins)
      * Git LFS pointer files stand in for real assets
      * a HexEngine binary's version resource doesn't match -Version, or is
        still flagged as a pre-release/dev build
      * a staged EXE/DLL imports a DLL that is neither in the release (app
        folder, Bin\, Plugins\), part of Windows, nor the VC++ runtime the
        installer provides - i.e. something would fail to load on a clean PC
      * a debug CRT (vcruntime140d, msvcp140d, ucrtbased) is imported
      * -RequirePluginManifest is set and Plugins\plugins.json is missing or a
        hash does not match its DLL

.PARAMETER StagingDir
    The staged release.
.PARAMETER Version
    Expected MAJOR.MINOR.PATCH.
.PARAMETER RequirePluginManifest
    Also verify Plugins\plugins.json (run after New-PluginManifest.ps1).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$StagingDir,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [switch]$RequirePluginManifest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$failures = New-Object System.Collections.Generic.List[string]
$root = (Resolve-Path -LiteralPath $StagingDir).Path.TrimEnd('\')

# --- primary files ---------------------------------------------------------------
$primary = @(
    'HexEngine.Editor.exe', 'HexEngine.Core.dll', 'HexEngine.Launcher.exe',
    'HexEngine.AssetPacker.exe', 'HexEngine.ShaderCompiler.exe',
    'dxcompiler.dll', 'dxil.dll',
    'Plugins\HexEngine.D3D11Plugin.dll', 'Plugins\HexEngine.PhysXPlugin.dll',
    'Plugins\HexEngine.AssimpPlugin.dll', 'Plugins\HexEngine.NRDPlugin.dll',
    'Bin\PhysX_64.dll', 'Bin\sl.interposer.dll',
    'Data\Shaders\Default.hcs', 'Data\Fonts\Inter\Inter-Regular.ttf',
    'Data\Materials\Default.hmat', 'Data\Templates\ProjectTemplate.txt',
    'Data\AssetPackages\EngineAssets.pkg',
    'SDK\HexEngine.props', 'SDK\Include\HexEngine.Core\HexEngine.hpp',
    'SDK\Include\HexEngine.Core\HexVersion.generated.h',
    'SDK\Lib\x64\Release\HexEngine.Core.lib',
    'ThirdParty\nrd\Shaders\Include\NRD.hlsli',
    'LICENSE', 'THIRD_PARTY_NOTICES.md', 'version.txt'
)
foreach ($p in $primary) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $p) -PathType Leaf)) { $failures.Add("missing: $p") }
}

$allFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File)
function RelPath($f) { $f.FullName.Substring($root.Length + 1) }

# --- development-only files ---------------------------------------------------------
$devExt = '.pdb', '.ilk', '.exp', '.obj', '.iobj', '.ipdb', '.tlog', '.lastbuildstate', '.idb', '.pch', '.log'
foreach ($f in $allFiles) {
    $rel = RelPath $f
    # (.obj under Data\ is a Wavefront mesh, not a compiler object file.)
    if ($f.Extension -in $devExt -and -not ($f.Extension -eq '.obj' -and $rel -like 'Data\*')) { $failures.Add("development file staged: $rel") }
    if ($f.Extension -eq '.lib' -and $rel -notlike 'SDK\Lib\*') { $failures.Add("static/import library outside SDK\Lib: $rel") }
    if ($f.Extension -in '.exe', '.dll' -and ($f.Name -match 'Tests?\.exe$' -or $f.Name -like '*SamplePlugin*' -or $f.Name -like '*Steamworks*' -or $f.Name -like '*SampleGame*')) {
        $failures.Add("non-shipping binary staged: $rel")
    }
}
foreach ($dir in 'Logs', 'Data\Cache', 'Data\Shaders\Generated') {
    if (Test-Path -LiteralPath (Join-Path $root $dir)) { $failures.Add("per-machine scratch directory staged: $dir") }
}
if (Test-Path -LiteralPath (Join-Path $root 'Projects.json')) { $failures.Add('per-user Projects.json staged') }
if (Test-Path -LiteralPath (Join-Path $root 'HexEngine.installed')) { $failures.Add('install marker must be added by the installer only, not staged (the portable ZIP would then write to LocalAppData)') }

# --- Git LFS pointers ---------------------------------------------------------------
foreach ($f in $allFiles | Where-Object { $_.Length -lt 200 -and $_.Extension -notin '.txt', '.json', '.hmat', '.shader', '.hlsli', '.h', '.hpp', '.props', '.md' }) {
    $first = Get-Content -LiteralPath $f.FullName -TotalCount 1 -ErrorAction SilentlyContinue
    if ($first -like 'version https://git-lfs*') { $failures.Add("Git LFS pointer instead of content: $(RelPath $f)") }
}

# --- version resources ----------------------------------------------------------------
$ours = $allFiles | Where-Object { $_.Extension -in '.exe', '.dll' -and $_.Name -like 'HexEngine.*' }
foreach ($b in $ours) {
    $vi = $b.VersionInfo
    if ($vi.ProductVersion -ne $Version) { $failures.Add("version mismatch: $(RelPath $b) is '$($vi.ProductVersion)', expected '$Version'") }
    elseif ($vi.IsPreRelease) { $failures.Add("dev/pre-release build staged: $(RelPath $b) (build without /p:HexVersionIsRelease=true?)") }
}

# --- import resolution ------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$dumpbin = $null
if (Test-Path $vswhere) {
    $dumpbin = & $vswhere -latest -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
}
if (-not $dumpbin) {
    $failures.Add('dumpbin.exe not found (Visual Studio C++ tools) - cannot verify runtime dependencies')
}
else {
    $shipped = @{}
    foreach ($f in $allFiles | Where-Object { $_.Extension -eq '.dll' }) {
        $dir = Split-Path -Leaf (Split-Path $f.FullName)
        $rel = RelPath $f
        # Only DLLs the loader can actually find count: app folder, Bin\ (AddDllDirectory), Plugins\.
        if ($rel -notmatch '\\' -or $rel -like 'Bin\*' -or $rel -like 'Plugins\*') { $shipped[$f.Name.ToLowerInvariant()] = $true }
    }
    $vcRuntime = 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
                 'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'concrt140.dll', 'vcomp140.dll', 'vccorlib140.dll'
    $debugCrt = 'vcruntime140d.dll', 'vcruntime140_1d.dll', 'msvcp140d.dll', 'ucrtbased.dll', 'concrt140d.dll', 'vcomp140d.dll'
    # Installed by the NVIDIA display driver, not Windows. Streamline only loads
    # its nvngx_* feature DLLs (DLSS-G, DeepDVC) on NVIDIA GPUs, where the driver
    # is always present - so these are fine there and nowhere else. (A GPU-less
    # CI runner lacks them; a dev PC with an NVIDIA card has them in System32.)
    $nvidiaDriver = 'nvcuda.dll', 'vulkan-1.dll'
    $system32 = Join-Path $env:SystemRoot 'System32'

    $pe = $allFiles | Where-Object {
        $_.Extension -in '.exe', '.dll' -and ((RelPath $_) -notmatch '\\' -or (RelPath $_) -like 'Bin\*' -or (RelPath $_) -like 'Plugins\*')
    }
    foreach ($b in $pe) {
        $out = & $dumpbin /nologo /dependents $b.FullName 2>&1
        # dumpbin lists hard imports, then (optionally) delay-load imports. A
        # missing HARD import stops the DLL loading at all; a missing
        # delay-load only fails if that code path runs (e.g. Streamline's
        # Vulkan-only NvLowLatencyVk.dll under a D3D engine) - warn, don't fail.
        $imports = @(); $delayed = @(); $section = ''
        foreach ($line in $out) {
            if ($line -match 'following dependencies') { $section = 'hard'; continue }
            if ($line -match 'delay load dependencies') { $section = 'delay'; continue }
            if ($line -match '^\s*Summary') { $section = ''; continue }
            if ($line -match '^\s+(\S+\.dll)\s*$') {
                if ($section -eq 'hard') { $imports += $Matches[1].ToLowerInvariant() }
                elseif ($section -eq 'delay') { $delayed += $Matches[1].ToLowerInvariant() }
            }
        }
        foreach ($imp in $delayed) {
            if (-not $shipped.ContainsKey($imp) -and $imp -notin $vcRuntime -and $imp -notlike 'api-ms-win-*' -and
                -not (Test-Path -LiteralPath (Join-Path $system32 $imp))) {
                Write-Warning "unresolved DELAY-LOAD import (only fatal if that feature runs): $(RelPath $b) -> $imp"
            }
        }
        foreach ($imp in $imports) {
            if ($imp -in $debugCrt) { $failures.Add("debug CRT import: $(RelPath $b) -> $imp"); continue }
            if ($shipped.ContainsKey($imp)) { continue }
            if ($imp -in $vcRuntime) { continue }                       # installer: VC++ Redistributable
            if ($imp -in $nvidiaDriver -and ($b.Name -like 'nvngx_*' -or $b.Name -like 'sl.*')) { continue }
            if ($imp -like 'api-ms-win-*' -or $imp -like 'ext-ms-*') { continue }
            if (Test-Path -LiteralPath (Join-Path $system32 $imp)) { continue }
            $failures.Add("unresolved import: $(RelPath $b) -> $imp (not shipped, not Windows, not VC++ runtime)")
        }
    }
    Write-Host "Checked imports of $(@($pe).Count) binaries"
}

# --- plugin manifest -------------------------------------------------------------------
if ($RequirePluginManifest) {
    $mf = Join-Path $root 'Plugins\plugins.json'
    if (-not (Test-Path -LiteralPath $mf)) { $failures.Add('Plugins\plugins.json missing') }
    else {
        $m = Get-Content -Raw -LiteralPath $mf | ConvertFrom-Json
        $listed = @{}
        foreach ($e in @($m.plugins)) {
            $listed[$e.module.ToLowerInvariant()] = $true
            $dll = Join-Path $root "Plugins\$($e.module)"
            if (-not (Test-Path -LiteralPath $dll)) { $failures.Add("plugins.json lists a missing DLL: $($e.module)"); continue }
            $h = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($h -ne $e.sha256) { $failures.Add("plugins.json hash mismatch for $($e.module) (signed after the manifest was written?)") }
        }
        foreach ($d in Get-ChildItem -LiteralPath (Join-Path $root 'Plugins') -Filter '*.dll') {
            if (-not $listed.ContainsKey($d.Name.ToLowerInvariant())) { $failures.Add("plugin not in plugins.json: $($d.Name)") }
        }
    }
}

# --- result --------------------------------------------------------------------------------
$mb = ($allFiles | Measure-Object Length -Sum).Sum / 1MB
if ($failures.Count -gt 0) {
    Write-Host "Release staging validation FAILED ($($failures.Count) problem(s)):" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host ("Release staging valid: {0} files, {1:N1} MB, version {2}" -f $allFiles.Count, $mb, $Version) -ForegroundColor Green
