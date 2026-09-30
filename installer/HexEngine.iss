; HexEngine Windows installer (Inno Setup 6).
;
; Packages the staged release (scripts/release/Stage-Release.ps1 -> dist\HexEngine).
; Built by .github/workflows/release.yml; see docs/RELEASING.md. Everything
; version-specific is passed in by the caller, so nothing here changes per release:
;
;   ISCC.exe installer\HexEngine.iss /DAppVersion=0.4.0 ^
;       /DStagingDir=<repo>\dist\HexEngine /DOutputDir=<repo>\release-out ^
;       /DVCRedist=<path>\vc_redist.x64.exe /DVCRedistVersion=14.42.34433 ^
;       [/DSignToolName=hexsign  plus  /Shexsign="signtool.exe sign ... $f"]

#ifndef AppVersion
  #error AppVersion is required (e.g. /DAppVersion=0.4.0)
#endif
#ifndef StagingDir
  #error StagingDir is required (the staged dist\HexEngine folder)
#endif
#ifndef OutputDir
  #define OutputDir "..\release-out"
#endif
#ifndef VCRedist
  #error VCRedist is required (path to vc_redist.x64.exe)
#endif
#ifndef VCRedistVersion
  #error VCRedistVersion is required (MAJOR.MINOR.BUILD of the bundled vc_redist.x64.exe)
#endif

#define AppName "HexEngine"
#define AppPublisher "HexPerson"
#define AppURL "https://github.com/HexPerson/HexEngine"
#define EditorExe "HexEngine.Editor.exe"

; Split "14.42.34433" for the registry comparison in [Code].
#define VCMajor Copy(VCRedistVersion, 1, Pos(".", VCRedistVersion) - 1)
#define VCRest  Copy(VCRedistVersion, Pos(".", VCRedistVersion) + 1, 100)
#define VCMinor Copy(VCRest, 1, Pos(".", VCRest) - 1)
#define VCBuild Copy(VCRest, Pos(".", VCRest) + 1, 100)

[Setup]
; NEVER change AppId: it is how a newer installer finds and upgrades an
; existing installation (same folder, same uninstall entry).
AppId={{8E6D4F7A-3C21-4B9E-A5D2-7F1C0B6E9A43}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
AppCopyright=Copyright (c) {#AppPublisher}
VersionInfoVersion={#AppVersion}.0
VersionInfoProductVersion={#AppVersion}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoDescription={#AppName} {#AppVersion} Setup

; 64-bit only, installed per machine under Program Files.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}

LicenseFile={#StagingDir}\LICENSE
SetupIconFile={#SourcePath}\..\Source\HexEngine\Common\HexEngine.ico
UninstallDisplayIcon={app}\{#EditorExe}
UninstallDisplayName={#AppName} {#AppVersion}
WizardStyle=modern

OutputDir={#OutputDir}
OutputBaseFilename=HexEngine-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4

; Upgrades: close a running editor rather than fail on locked files.
CloseApplications=yes
RestartApplications=no

#ifdef SignToolName
; Code signing (enabled by the release workflow only when the signing
; secrets exist): signs this setup AND the uninstaller embedded in it.
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Clean upgrade: every folder below is engine-owned and fully replaced by this
; version, so remove the previous version's copy first - otherwise a plugin or
; shader dropped between versions would linger and still be loaded. Nothing
; user-created lives under {app}: projects are wherever the user saved them
; and per-user state is in %LOCALAPPDATA%\HexEngine (neither is touched).
Type: filesandordirs; Name: "{app}\Plugins"
Type: filesandordirs; Name: "{app}\Bin"
Type: filesandordirs; Name: "{app}\Data"
Type: filesandordirs; Name: "{app}\SDK"
Type: filesandordirs; Name: "{app}\ThirdParty"
Type: filesandordirs; Name: "{app}\Licenses"

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Marks this copy as installed: per-user state goes to %LOCALAPPDATA%\HexEngine.
Source: "{#SourcePath}\HexEngine.installed"; DestDir: "{app}"; Flags: ignoreversion
; Microsoft Visual C++ 2015-2022 Redistributable (x64): every HexEngine binary
; links the CRT dynamically (/MD). Installed in PrepareToInstall if needed.
Source: "{#VCRedist}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: dontcopy

[Icons]
; Only the editor is user-facing. The Launcher (game runtime), AssetPacker,
; ShaderCompiler and McpServer are helpers the editor or games run; they get
; no shortcuts.
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#EditorExe}"; WorkingDir: "{app}"; Comment: "HexEngine editor"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#EditorExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#EditorExe}"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  VCRuntimeKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

// True if the given registry view has an x64 VC++ 14.x runtime at least as
// new as the one bundled with this installer.
function VCRuntimeUpToDate(RootKey: Integer): Boolean;
var
  Installed, Major, Minor, Bld: Cardinal;
begin
  Result := False;
  if RegQueryDWordValue(RootKey, VCRuntimeKey, 'Installed', Installed) and (Installed = 1) and
     RegQueryDWordValue(RootKey, VCRuntimeKey, 'Major', Major) and
     RegQueryDWordValue(RootKey, VCRuntimeKey, 'Minor', Minor) and
     RegQueryDWordValue(RootKey, VCRuntimeKey, 'Bld', Bld) then
    Result := (Major > {#VCMajor}) or
              ((Major = {#VCMajor}) and ((Minor > {#VCMinor}) or
                                         ((Minor = {#VCMinor}) and (Bld >= {#VCBuild}))));
end;

function VCRedistNeedsInstall(): Boolean;
begin
  // The (32-bit) redistributable bootstrapper records the x64 runtime in the
  // 64-bit view and/or WOW6432Node depending on version - accept either.
  Result := not (VCRuntimeUpToDate(HKLM64) or VCRuntimeUpToDate(HKLM32));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  if not VCRedistNeedsInstall() then
    exit;

  WizardForm.PreparingLabel.Caption := 'Installing the Microsoft Visual C++ Redistributable...';
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '',
              SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := 'The Microsoft Visual C++ Redistributable installer could not be started: ' +
              SysErrorMessage(ResultCode);
    exit;
  end;

  case ResultCode of
    0, 1638: ;                        // installed / a newer version is already present
    3010: NeedsRestart := True;       // installed, reboot required
  else
    Result := 'The Microsoft Visual C++ Redistributable failed to install (exit code ' +
              IntToStr(ResultCode) + '). HexEngine needs it to run.';
  end;
end;
