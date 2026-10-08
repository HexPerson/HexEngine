<#
.SYNOPSIS
    Builds the HexEngine release staging directory (dist/HexEngine) from a
    completed Release|x64 build.

.DESCRIPTION
    Copies EXACTLY the files that ship - never a blind copy of the build output
    folder - and fails with a clear message if anything expected is missing.
    The portable ZIP and the Inno Setup installer both consume this directory.

    Staged layout (see docs/RELEASING.md for the rationale of each part):

        HexEngine.Editor.exe            user-facing editor (the only shortcut)
        HexEngine.Core.dll              engine
        HexEngine.Launcher.exe          game runtime, copied into exported games
        HexEngine.AssetPacker.exe       spawned by the editor to pack game data
        HexEngine.ShaderCompiler.exe    spawned by the editor (materials, hot reload)
        HexEngine.McpServer.exe         optional AI-assistant bridge (docs/MCP.md)
        dxcompiler.dll, dxil.dll        DXC runtime for the shader compiler
        Plugins\*.dll + plugins.json    engine plugins (+ SHA-256 manifest, see
                                        New-PluginManifest.ps1)
        Bin\*.dll                       third-party runtime DLLs
        Data\                           engine data (Content/EngineData) +
                                        compiled shaders + shader sources +
                                        AssetPackages\EngineAssets.pkg
        ThirdParty\nrd\Shaders\         NRD denoiser shader includes
        SDK\                            headers + import libs + HexEngine.props
                                        for building game code
        Licenses\, LICENSE, THIRD_PARTY_NOTICES.md, version.txt

    Deliberately NOT staged: PDBs (collected separately with -SymbolsDir),
    .lib/.exp/.ilk/.obj (except the SDK import libs), test binaries, the
    SamplePlugin, the Steamworks plugin (steam_api64.dll is not ours to
    redistribute) and the legacy SampleGame.

    The VC++ runtime is NOT copied: every binary links /MD and the installer
    installs the Microsoft Visual C++ 2015-2022 Redistributable.

.PARAMETER Version
    MAJOR.MINOR.PATCH (from Get-ReleaseVersion.ps1).

.PARAMETER RepoRoot
    Repository root. Defaults to two levels above this script.

.PARAMETER OutputDir
    Staging directory. Default: <repo>\dist\HexEngine. Wiped first.

.PARAMETER SymbolsDir
    If set, the PDBs of every staged HexEngine binary are copied here (for a
    separate symbols archive).

.PARAMETER SkipEngineAssetsPackage
    Don't build Data\AssetPackages\EngineAssets.pkg (faster local iteration).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$OutputDir,
    [string]$SymbolsDir,
    [string]$Commit = $env:GITHUB_SHA,
    [switch]$SkipEngineAssetsPackage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot 'dist\HexEngine' }
# Absolute paths throughout: AssetPacker and .NET APIs don't share PowerShell's location.
$OutputDir = $PSCmdlet.GetUnresolvedProviderPathFromPSPath($OutputDir)
if ($SymbolsDir) { $SymbolsDir = $PSCmdlet.GetUnresolvedProviderPathFromPSPath($SymbolsDir) }

$BuildRoot   = Join-Path $RepoRoot 'Bin\x64\Release'           # runtime output
$IntRoot     = Join-Path $RepoRoot 'Source\HexEngine\x64\Release'  # link output (libs, pdbs)
$SourceRoot  = Join-Path $RepoRoot 'Source\HexEngine'
$ContentRoot = Join-Path $RepoRoot 'Content\EngineData'

# --------------------------------------------------------------------------
# What ships. Every entry is REQUIRED unless listed under an *Optional* set;
# a missing required file fails the staging with a list of everything missing.
# --------------------------------------------------------------------------
$RootFiles = @(
    'HexEngine.Editor.exe',
    'HexEngine.Core.dll',
    'HexEngine.Launcher.exe',
    'HexEngine.AssetPacker.exe',
    'HexEngine.ShaderCompiler.exe',
    'HexEngine.McpServer.exe',
    'dxcompiler.dll',
    'dxil.dll'
)

$Plugins = @(
    # Required at engine start-up (created unconditionally by Game3DEnvironment).
    'HexEngine.D3D11Plugin.dll',
    'HexEngine.D3D12Plugin.dll',
    'HexEngine.AssimpPlugin.dll',
    'HexEngine.BrotliPlugin.dll',
    'HexEngine.PhysXPlugin.dll',
    'HexEngine.FreeTypePlugin.dll',
    'HexEngine.RecastNavigationPlugin.dll',
    'HexEngine.StreamlinePlugin.dll',
    'HexEngine.NRDPlugin.dll',
    # Optional features / tooling (TryCreateInterface or feature plugins).
    'HexEngine.HBAOPlusPlugin.dll',
    'HexEngine.FSRPlugin.dll',
    'HexEngine.GameNetworkingSocketsPlugin.dll',
    'HexEngine.CitySimulationPlugin.dll',
    'HexEngine.VolumetricTerrainPlugin.dll',
    'HexEngine.WeatherPlugin.dll',
    'HexEngine.EditorBridgePlugin.dll'
)

$BinFiles = @(
    # PhysX (bootstrap: build_physx)
    'PhysX_64.dll', 'PhysXCommon_64.dll', 'PhysXFoundation_64.dll', 'PhysXCooking_64.dll',
    # Brotli (bootstrap: build_brotli)
    'brotlicommon.dll', 'brotlidec.dll', 'brotlienc.dll',
    # GameNetworkingSockets + deps (vcpkg, plugin post-build)
    'GameNetworkingSockets.dll', 'libprotobuf.dll', 'abseil_dll.dll', 'libcrypto-3-x64.dll', 'libssl-3-x64.dll',
    # HBAO+ (bootstrap: stage_hbaoplus)
    'GFSDK_SSAO_D3D11.win64.dll',
    # NVIDIA Streamline runtime (bootstrap: ensure_streamline)
    'sl.interposer.dll', 'sl.common.dll', 'sl.dlss.dll', 'sl.dlss_g.dll', 'sl.deepdvc.dll', 'sl.directsr.dll',
    'nvngx_dlss.dll', 'nvngx_dlssg.dll', 'nvngx_deepdvc.dll'
)
# Staged when present, skipped with a warning otherwise.
$BinFilesOptional = @()
# NOT shipped: PhysXGpu_64.dll (~240 MB) and PhysXDevice64.dll. They are only
# loaded for PhysX GPU (CUDA) acceleration, which the engine doesn't enable
# (PhysicsSystemPhysX.cpp creates no CUDA context; eENABLE_GPU_DYNAMICS is off).
# Add them back here if GPU dynamics are ever turned on.

# Engine shaders that must be present (spot checks on top of the count check).
$CriticalShaders = @('Default.hcs', 'Deferred.hcs', 'ShadowMapGeometry.hcs', 'SkySphere.hcs', 'Water.hcs')
$MinCompiledShaders = 100

# --------------------------------------------------------------------------
$missing = New-Object System.Collections.Generic.List[string]
function Test-Required([string]$Path, [string]$What) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { $script:missing.Add("$What ($Path)"); return $false }
    return $true
}
function Copy-Required([string]$Source, [string]$DestDir, [string]$What) {
    if (Test-Required $Source $What) {
        New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
        Copy-Item -LiteralPath $Source -Destination $DestDir -Force
    }
}
# Extensions: e.g. @('.h', '.inl'); empty = everything. (Get-ChildItem's -Include
# is silently ignored together with -LiteralPath in Windows PowerShell.)
function Copy-Tree([string]$Source, [string]$Dest, [string[]]$Extensions = @()) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { $script:missing.Add("directory $Source"); return }
    Get-ChildItem -LiteralPath $Source -Recurse -File |
        Where-Object { $Extensions.Count -eq 0 -or $_.Extension -in $Extensions } | ForEach-Object {
        $rel = $_.FullName.Substring($Source.TrimEnd('\').Length + 1)
        $target = Join-Path $Dest $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
}

Write-Host "Staging HexEngine $Version"
Write-Host "  repo:   $RepoRoot"
Write-Host "  build:  $BuildRoot"
Write-Host "  output: $OutputDir"

if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# --- executables, engine DLL, DXC ------------------------------------------
foreach ($f in $RootFiles) { Copy-Required (Join-Path $BuildRoot $f) $OutputDir "binary $f" }

# --- plugins -----------------------------------------------------------------
$pluginDir = Join-Path $OutputDir 'Plugins'
foreach ($f in $Plugins) { Copy-Required (Join-Path $BuildRoot "Plugins\$f") $pluginDir "plugin $f" }

# --- third-party runtime DLLs --------------------------------------------------
$binDir = Join-Path $OutputDir 'Bin'
foreach ($f in $BinFiles) { Copy-Required (Join-Path $BuildRoot "Bin\$f") $binDir "runtime DLL $f" }
foreach ($f in $BinFilesOptional) {
    $src = Join-Path $BuildRoot "Bin\$f"
    if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination $binDir -Force }
    else { Write-Warning "Optional runtime DLL not found, skipping: $f" }
}

# --- engine data ---------------------------------------------------------------
$dataDir = Join-Path $OutputDir 'Data'
Copy-Tree $ContentRoot $dataDir

# Git LFS guard: a checkout without `git lfs pull` leaves ~130-byte pointer
# text files where the textures and fonts should be.
$pointerFiles = Get-ChildItem -LiteralPath $dataDir -Recurse -File |
    Where-Object { $_.Length -lt 200 -and $_.Extension -in '.png', '.jpg', '.jpeg', '.tga', '.dds', '.ttf', '.otf', '.ico', '.hmesh' } |
    Where-Object { (Get-Content -LiteralPath $_.FullName -TotalCount 1 -ErrorAction SilentlyContinue) -like 'version https://git-lfs*' }
if ($pointerFiles) {
    $missing.Add("engine data contains Git LFS pointer files instead of content (run 'git lfs pull'): " + (($pointerFiles | Select-Object -First 5 -ExpandProperty Name) -join ', '))
}

# Compiled shaders: the build writes them to Bin\x64\Release\Data\Shaders.
# Only top-level *.hcs - Generated\ holds per-machine material-graph output.
$shaderOut = Join-Path $dataDir 'Shaders'
New-Item -ItemType Directory -Force -Path $shaderOut | Out-Null
$compiled = @(Get-ChildItem -LiteralPath (Join-Path $BuildRoot 'Data\Shaders') -Filter '*.hcs' -File -ErrorAction SilentlyContinue)
foreach ($c in $compiled) { Copy-Item -LiteralPath $c.FullName -Destination $shaderOut -Force }
if ($compiled.Count -lt $MinCompiledShaders) {
    $missing.Add("compiled shaders: found $($compiled.Count) in $BuildRoot\Data\Shaders, expected at least $MinCompiledShaders")
}
foreach ($s in $CriticalShaders) { Test-Required (Join-Path $shaderOut $s) "compiled shader $s" | Out-Null }

# Shader SOURCES: the material-graph compiler #includes the engine's .shader
# files (MeshCommon, Utils, Global, PBRutils, ...) at runtime and searches
# Data\Shaders for them.
$shaderSrc = @(Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'HexEngine.Shaders') -Filter '*.shader' -File)
if ($shaderSrc.Count -eq 0) { $missing.Add('shader sources (HexEngine.Shaders\*.shader)') }
foreach ($s in $shaderSrc) { Copy-Item -LiteralPath $s.FullName -Destination $shaderOut -Force }

# --- NRD shader includes (searched relative to the working directory) ----------
$nrdSrc = Join-Path $RepoRoot 'ThirdParty\nrd\Shaders'
Copy-Tree (Join-Path $nrdSrc 'Include') (Join-Path $OutputDir 'ThirdParty\nrd\Shaders\Include')
Copy-Tree (Join-Path $nrdSrc 'Resources') (Join-Path $OutputDir 'ThirdParty\nrd\Shaders\Resources')
Copy-Required (Join-Path $RepoRoot 'ThirdParty\nrd\LICENSE.txt') (Join-Path $OutputDir 'ThirdParty\nrd') 'NRD licence'

# --- SDK (build game code against an installed engine) --------------------------
$sdk = Join-Path $OutputDir 'SDK'
Copy-Required (Join-Path $RepoRoot 'installer\sdk\HexEngine.props') $sdk 'SDK HexEngine.props'

$coreSrc = Join-Path $SourceRoot 'HexEngine.Core'
$coreInc = Join-Path $sdk 'Include\HexEngine.Core'
Get-ChildItem -LiteralPath $coreSrc -Recurse -File | Where-Object { $_.Extension -in '.h', '.hpp' } |
    Where-Object { $_.FullName -notmatch '\\(x64|Debug|Release)\\' } |
    ForEach-Object {
        $rel = $_.FullName.Substring($coreSrc.Length + 1)
        $target = Join-Path $coreInc $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
Test-Required (Join-Path $coreInc 'HexEngine.hpp') 'SDK header HexEngine.hpp' | Out-Null

# Game code doesn't see the engine's MSBuild version defines; pin the
# version for it here (read by HexVersion.hpp via __has_include).
$parts = $Version.Split('.')
@(
    '// Generated by scripts/release/Stage-Release.ps1 from the release tag. Do not edit.',
    '#pragma once',
    "#define HEX_VERSION_MAJOR $($parts[0])",
    "#define HEX_VERSION_MINOR $($parts[1])",
    "#define HEX_VERSION_PATCH $($parts[2])",
    '#define HEX_VERSION_IS_DEV 0'
) | Set-Content -LiteralPath (Join-Path $coreInc 'HexVersion.generated.h') -Encoding ascii

Copy-Tree (Join-Path $RepoRoot 'Include\nlohmann') (Join-Path $sdk 'Include\nlohmann')
Copy-Tree (Join-Path $RepoRoot 'ThirdParty\DirectXTK\Inc') (Join-Path $sdk 'Include\DirectXTK') @('.h', '.inl')

$sdkLib = Join-Path $sdk 'Lib\x64\Release'
Copy-Required (Join-Path $IntRoot 'HexEngine.Core.lib') $sdkLib 'SDK import library HexEngine.Core.lib'
Copy-Required (Join-Path $RepoRoot 'Libs\x64\Release\DirectXTK.lib') $sdkLib 'SDK library DirectXTK.lib'

# --- licences --------------------------------------------------------------------
Copy-Required (Join-Path $RepoRoot 'LICENSE') $OutputDir 'LICENSE'
Copy-Required (Join-Path $RepoRoot 'THIRD_PARTY_NOTICES.md') $OutputDir 'THIRD_PARTY_NOTICES.md'
$licDir = Join-Path $OutputDir 'Licenses'
Copy-Tree (Join-Path $RepoRoot 'Licenses') $licDir
# Per-dependency licence files recorded in the SBOM inventory.
$inventory = Get-Content -Raw (Join-Path $RepoRoot 'tools\deps\dependencies.json') | ConvertFrom-Json
foreach ($dep in @($inventory.thirdparty)) {
    $lf = $dep.PSObject.Properties['license_file']
    if ($lf -and $lf.Value) {
        $src = Join-Path $RepoRoot $lf.Value
        if (Test-Path -LiteralPath $src) {
            $dst = Join-Path $licDir $dep.name
            New-Item -ItemType Directory -Force -Path $dst | Out-Null
            Copy-Item -LiteralPath $src -Destination $dst -Force
        }
    }
}
foreach ($port in @($inventory.vcpkg)) {
    $src = Join-Path $RepoRoot "vcpkg_installed\x64-windows\share\$($port.name)\copyright"
    if (Test-Path -LiteralPath $src) {
        $dst = Join-Path $licDir $port.name
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Copy-Item -LiteralPath $src -Destination (Join-Path $dst 'copyright.txt') -Force
    }
}
Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'ThirdParty\Streamline\bin\x64') -Filter '*.license.txt' -ErrorAction SilentlyContinue |
    ForEach-Object {
        New-Item -ItemType Directory -Force -Path (Join-Path $licDir 'streamline') | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licDir 'streamline') -Force
    }

# --- version stamp ---------------------------------------------------------------
@("HexEngine $Version", "commit $Commit") | Set-Content -LiteralPath (Join-Path $OutputDir 'version.txt') -Encoding ascii

# --- fail now if anything required was missing -----------------------------------
if ($missing.Count -gt 0) {
    Write-Host ''
    Write-Host "Staging FAILED - $($missing.Count) required item(s) missing:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    throw "Release staging incomplete."
}

# --- EngineAssets.pkg (exported games mount it; the editor uses loose Data) ------
if (-not $SkipEngineAssetsPackage) {
    $pkgDir = Join-Path $dataDir 'AssetPackages'
    New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null
    $pkg = Join-Path $pkgDir 'EngineAssets.pkg'
    Write-Host "Packing $pkg"
    # AssetPacker runs headless from the staged tree (it loads Plugins\ for
    # Brotli relative to the working directory) and skips Cache\ and *.pkg.
    Push-Location $OutputDir
    try {
        & (Join-Path $OutputDir 'HexEngine.AssetPacker.exe') -I $dataDir -O $pkg
        $packExit = $LASTEXITCODE
    }
    finally { Pop-Location }
    if ($packExit -ne 0 -or -not (Test-Path -LiteralPath $pkg) -or (Get-Item -LiteralPath $pkg).Length -eq 0) {
        throw "HexEngine.AssetPacker failed to produce $pkg (exit $packExit)."
    }
    # Headless runs log beside the executable (no install marker here).
    foreach ($scratch in 'Logs', 'Projects.json', 'Data\Cache') {
        $p = Join-Path $OutputDir $scratch
        if (Test-Path -LiteralPath $p) { Remove-Item -Recurse -Force -LiteralPath $p }
    }
}

# --- optional: symbols -------------------------------------------------------------
if ($SymbolsDir) {
    if (Test-Path $SymbolsDir) { Remove-Item -Recurse -Force $SymbolsDir }
    New-Item -ItemType Directory -Force -Path $SymbolsDir | Out-Null
    $staged = Get-ChildItem -LiteralPath $OutputDir -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' } |
        Where-Object { $_.Name -like 'HexEngine.*' }
    foreach ($b in $staged) {
        $pdbName = [IO.Path]::ChangeExtension($b.Name, '.pdb')
        $candidates = @((Join-Path $IntRoot $pdbName), (Join-Path $BuildRoot $pdbName), (Join-Path $BuildRoot "Plugins\$pdbName"))
        $pdb = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if ($pdb) { Copy-Item -LiteralPath $pdb -Destination $SymbolsDir -Force }
        else { Write-Warning "No PDB found for $($b.Name)" }
    }
}

Write-Host ''
$files = Get-ChildItem -LiteralPath $OutputDir -Recurse -File
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ("Staged {0} files, {1:N1} MB -> {2}" -f $files.Count, ($bytes / 1MB), $OutputDir) -ForegroundColor Green
