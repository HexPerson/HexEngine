# Releasing HexEngine

A release is a Git tag. Pushing `vMAJOR.MINOR.PATCH` makes GitHub Actions build,
test, package and publish HexEngine for Windows x64. Nothing else needs editing.

```
git checkout main
git pull
git tag v0.1.0
git push origin v0.1.0
```

## 1. How versions work

HexEngine uses [Semantic Versioning](https://semver.org): `MAJOR.MINOR.PATCH`.

The **Git tag is the single source of truth.** The workflow validates the tag
(`scripts/release/Get-ReleaseVersion.ps1`: exactly `vMAJOR.MINOR.PATCH`, no
leading zeros, no suffixes) and passes the version to MSBuild as
`/p:HexVersion=0.4.0 /p:HexVersionIsRelease=true`. From there:

| Where | How the version gets there |
|---|---|
| C++ code | `Source/HexEngine/Directory.Build.targets` defines `HEX_VERSION_MAJOR/MINOR/PATCH`; `HexEngine.Core/HexVersion.hpp` provides `HEX_VERSION_STRING` (`"0.4.0"`). The engine prints it at the top of every log. |
| Every HexEngine EXE/DLL | Shared `VERSIONINFO` resource (`Source/HexEngine/Common/HexVersion.rc`), visible in Explorer → Properties → Details |
| Game code built against the SDK | `SDK/Include/HexEngine.Core/HexVersion.generated.h`, written by the packaging script |
| Installer | Inno Setup `AppVersion`, file version, uninstall entry |
| File names | `HexEngine-Setup-0.4.0.exe`, `HexEngine-0.4.0-Windows-x64.zip` |
| GitHub Release | Title `HexEngine v0.4.0` |

A local build without `/p:HexVersion` is `0.0.0-dev` (flagged pre-release).

Not the release version: `HexEngineVersion`/`HexEditorVersion` in `Required.hpp`
are **project file-format** versions compared for exact equality when a project
loads. They only change when the format changes, never per release. The
`version-string` in `vcpkg.json` is vcpkg package metadata and is ignored.

## 2. PATCH, MINOR, MAJOR

- **PATCH** (`0.2.0` → `0.2.1`): bug fixes only; projects and plugins from the
  previous version keep working.
- **MINOR** (`0.2.1` → `0.3.0`): new features, backwards compatible.
- **MAJOR** (`0.x` → `1.0.0`, `1.x` → `2.0.0`): breaking changes to projects,
  the plugin API or the SDK.

## 3. 0.x versions are development releases

While the major version is `0`, anything may change between minor versions
(SemVer §4): the plugin API, SDK headers and project format are not yet stable.
Expect `0.2.0` → `0.3.0` to need project or plugin updates. `1.0.0` will be the
first release with a stability promise.

## 4. Creating a release

1. Make sure `main` is green (Windows Modern Build, Unit Tests, CodeQL).
2. Pick the next version (see above) and tag the commit you want to ship:

   ```
   git checkout main
   git pull
   git tag v0.1.0
   git push origin v0.1.0
   ```

   To tag with a message: `git tag -a v0.1.0 -m "HexEngine 0.1.0"`.
3. Watch **Actions → Release**. A full run takes roughly an hour (longer on the
   first run after a dependency change, which rebuilds PhysX/Assimp/NRD).

## 5. What GitHub Actions does after the tag is pushed

`.github/workflows/release.yml`, job **build** (windows-2022):

1. Validates the tag and derives the version.
2. Pulls the engine data from Git LFS (cached between runs).
3. Bootstraps dependencies (`tools/deps/bootstrap.py`, Release only) and the
   vcpkg manifest.
4. Builds `HexEngine-modern.slnf` in Release|x64 with the version stamped in.
5. Builds and runs the unit tests (`HexEngine.Tests`).
6. Stages exactly the shipping files into `dist/HexEngine`
   (`scripts/release/Stage-Release.ps1`), failing on anything missing.
7. Validates the staged tree (`Test-ReleaseStaging.ps1`): no PDB/LIB/debug
   files, no Git LFS pointer files, correct version resources, and every
   import of every EXE/DLL resolvable on a clean machine.
8. Code-signs the binaries if signing secrets are configured (see §9).
9. Writes `Plugins/plugins.json` with each plugin's SHA-256 (the engine
   refuses a plugin whose hash doesn't match), then validates again.
10. Creates the portable ZIP and a symbols ZIP (deterministic archives).
11. Builds the installer with Inno Setup (signing it too, if enabled).
12. Silently installs, checks and uninstalls the installer on the runner.

Job **publish** (only for a pushed `v*` tag, only if everything above passed)
creates the GitHub Release `HexEngine v0.1.0` with auto-generated release notes
(the PRs and commits since the previous tag) and uploads the files.

Pull requests that change the packaging, and manual runs (**Actions → Release
→ Run workflow**, with an optional dry-run version), run the build job only.
The installer and ZIP are attached to the run as artifacts and no release is
created.

## 6. Where the results appear

**GitHub → Releases → HexEngine v0.1.0:**

| File | What it is |
|---|---|
| `HexEngine-Setup-0.1.0.exe` | Installer (recommended) |
| `HexEngine-0.1.0-Windows-x64.zip` | Portable copy: extract anywhere, run `HexEngine.Editor.exe` |
| `HexEngine-0.1.0-Windows-x64-symbols.zip` | PDBs for crash analysis (not needed to run) |

## 7. Deleting or fixing a bad tag

**Before the release was published** (the Release workflow failed or is still
running): cancel the run if it is still going, then delete the tag locally and
on GitHub, fix, and re-tag:

```
git tag -d v0.1.0
git push origin :refs/tags/v0.1.0
# fix, commit, push to main, then:
git tag v0.1.0
git push origin v0.1.0
```

**After the release was published:** delete the GitHub Release first
(Releases → the release → Delete), then the tag as above. Prefer shipping a new
PATCH version (`v0.1.1`) over re-using a published version number: people may
already have downloaded it.

The publish step refuses to overwrite an existing release for the same tag.

## 8. Portable ZIP vs installer

- **Installer:** installs to `C:\Program Files\HexEngine` and adds the
  `HexEngine.installed` marker file. Because Program Files is read-only, the
  engine then keeps per-user state in `%LOCALAPPDATA%\HexEngine`: logs, the
  recent-projects list, the icon cache and generated material shaders.
- **Portable ZIP:** there is no marker, so everything stays in the extracted
  folder, exactly like a development build. Needs the VC++ Redistributable
  (§10).

User projects are never placed under Program Files. New projects default to
**Documents** in an installed build. Uninstalling removes the program files only.
Projects and `%LOCALAPPDATA%\HexEngine` are left alone, and upgrading keeps them
too.

## 9. Code signing

Signing is off until secrets are added. Without them, releases are built and
published **unsigned**, and Windows SmartScreen will warn users. To enable it,
add these repository secrets (**Settings → Secrets and variables → Actions**):

| Secret | Value |
|---|---|
| `WINDOWS_SIGNING_PFX_BASE64` | The code-signing certificate (`.pfx`), base64: `[Convert]::ToBase64String([IO.File]::ReadAllBytes('cert.pfx'))` |
| `WINDOWS_SIGNING_PFX_PASSWORD` | The `.pfx` password |
| `WINDOWS_SIGNING_TIMESTAMP_URL` | Optional; defaults to `http://timestamp.digicert.com` |

With them set, `scripts/release/Invoke-CodeSigning.ps1` signs, with SignTool
(SHA-256, RFC 3161 timestamp):

- every staged EXE/DLL that isn't already validly signed. Vendor-signed DLLs,
  such as NVIDIA Streamline, keep their original signature, which Streamline
  checks at runtime.
- the installer and its uninstaller, through Inno Setup's `SignTool`.

The certificate is decoded to a temporary file for the run and deleted
afterwards. It is never committed or logged.

> Certificates issued since mid-2023 generally keep their private key on a
> hardware token or a cloud HSM and can't be exported as a `.pfx`. For those,
> use a cloud signing service, such as Azure Trusted Signing
> (`azure/trusted-signing-action`) or your CA's SignTool plug-in. Put it in
> place of the two signing steps in `release.yml`, and keep the order: sign the
> binaries → write `plugins.json` → build and sign the installer.

## 10. Visual C++ runtime

Every HexEngine binary links the C runtime dynamically (`/MD`, the Visual Studio
default). The linkage is deliberately unchanged: plugins and game DLLs share CRT
state (heap, STL objects) with `HexEngine.Core.dll`, which a static CRT would
break.

- **Installer:** bundles `vc_redist.x64.exe` from the Visual Studio toolset
  that built the release. It installs only if the machine's x64 VC++ 14.x
  runtime is older, runs before any files are copied, and reports failures.
- **Portable ZIP:** requires the
  [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).
  No VC++ runtime DLLs are copied beside the executables.
- **Game builds** (compiling game code in the editor): require Visual Studio
  2022 or Build Tools with the C++ workload. The editor finds MSBuild with
  `vswhere`.

## 11. Shortcuts

Only **HexEngine** (`HexEngine.Editor.exe`) gets a Start Menu shortcut. A
desktop shortcut is an optional installer checkbox, off by default. Both start
in the install folder.

No shortcuts for:

- `HexEngine.Launcher.exe`: the game runtime, copied into every exported game.
- `HexEngine.AssetPacker.exe`: run by the editor when packing game data.
- `HexEngine.ShaderCompiler.exe`: run by the editor for materials and shader hot reload.
- `HexEngine.McpServer.exe`: AI-assistant bridge, configured in the assistant
  itself (see `docs/MCP.md`).

## 12. What goes into the distribution (`dist/HexEngine`)

Built by `scripts/release/Stage-Release.ps1` from an explicit list. Any missing
required file fails the release.

| Path | Contents |
|---|---|
| `HexEngine.Editor.exe`, `HexEngine.Core.dll` | Editor and engine |
| `HexEngine.Launcher.exe`, `HexEngine.AssetPacker.exe`, `HexEngine.ShaderCompiler.exe`, `HexEngine.McpServer.exe` | Helper programs (see §11) |
| `dxcompiler.dll`, `dxil.dll` | DirectX Shader Compiler runtime (vcpkg `directx-dxc`) |
| `Plugins\*.dll`, `Plugins\plugins.json` | Engine plugins and their SHA-256 manifest |
| `Bin\*.dll` | Third-party runtime DLLs: PhysX, Brotli, GameNetworkingSockets (+ protobuf, abseil, OpenSSL), HBAO+, NVIDIA Streamline/DLSS |
| `Data\` | Engine data from `Content/EngineData` (Git LFS), compiled shaders (`*.hcs`), shader sources (for the material-graph compiler), `AssetPackages\EngineAssets.pkg` (mounted by exported games) |
| `ThirdParty\nrd\Shaders\` | NRD denoiser shader includes (loaded at runtime) |
| `SDK\` | `HexEngine.props`, headers (`Include\HexEngine.Core`, `nlohmann`, `DirectXTK`), `Lib\x64\Release\HexEngine.Core.lib` + `DirectXTK.lib` for building game code |
| `LICENSE`, `THIRD_PARTY_NOTICES.md`, `Licenses\` | HexEngine and third-party licences |
| `version.txt` | Version and commit |

Left out on purpose:

- PDBs (shipped in the symbols ZIP), static libraries outside the SDK, and
  build intermediates.
- Test binaries, the SamplePlugin and the legacy SampleGame.
- The Steamworks plugin: `steam_api64.dll` is only redistributable under a
  Steamworks agreement.
- OIDN: its dependencies aren't provisioned in CI; NRD provides the denoiser.

### Engine data (`Content/EngineData`)

Source-controlled engine assets live in `Content/EngineData`, stored in Git LFS
for binary files. You need `git lfs install` once, before cloning. Only
redistributable content belongs here. The Microsoft fonts (Arial, Courier New,
YaHei) were removed, and the default UI font is **Inter** (SIL OFL).

A development build still reads `Bin\x64\<Config>\Data`. After pulling asset
changes, refresh it with:

```
./scripts/Sync-EngineData.ps1              # both configurations
./scripts/Sync-EngineData.ps1 -Configuration Debug
```

## 13. Building and testing a release locally

With a completed Release build (version stamped):

```
msbuild Source\HexEngine\HexEngine-modern.slnf /p:Configuration=Release /p:Platform=x64 /p:HexVersion=0.1.0 /p:HexVersionIsRelease=true /m
./scripts/release/Stage-Release.ps1 -Version 0.1.0
./scripts/release/New-PluginManifest.ps1 -StagingDir dist/HexEngine
./scripts/release/Test-ReleaseStaging.ps1 -StagingDir dist/HexEngine -Version 0.1.0 -RequirePluginManifest
./scripts/release/New-ReleaseZip.ps1 -SourceDir dist/HexEngine -DestinationPath release-out/HexEngine-0.1.0-Windows-x64.zip -RootFolderName HexEngine-0.1.0
ISCC installer\HexEngine.iss /DAppVersion=0.1.0 /DStagingDir=%CD%\dist\HexEngine /DOutputDir=%CD%\release-out /DVCRedist=<vc_redist.x64.exe> /DVCRedistVersion=<its 3-part version>
```

Or run **Actions → Release → Run workflow** for a full dry run on GitHub.
