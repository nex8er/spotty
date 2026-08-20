; Установщик Windows для релизной сборки. CI передаёт SourceDir и MyAppVersion в ISCC.
; Скрипт не зависит от IDE: в release job вызывается консольный ISCC.exe.

#ifndef SourceDir
  #error SourceDir must point to the windeployqt staging directory.
#endif

#ifndef MyAppVersion
  #error MyAppVersion must be supplied by the release workflow.
#endif

#define MyAppName "Spotty"
#define MyAppExeName "spotty.exe"
#define MyAppId "F554A80C-C563-4965-9EFD-8D0FB267E2AC"
#define VCRedistName "vc_redist.x64.exe"

[Setup]
AppId={{#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Spotty
AppPublisherURL=https://github.com/nex8er/spotty
AppSupportURL=https://github.com/nex8er/spotty/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile={#SourcePath}\..\resources\icons\spotty.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Библиотеки Qt, плагины Qt и модули Spotty уже разложены здесь windeployqt.
; vc_redist ставится ниже отдельно: он принадлежит системе, а не каталогу приложения.
Source: "{#SourceDir}\*"; Excludes: "{#VCRedistName}"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\{#VCRedistName}"; DestDir: "{tmp}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\{#VCRedistName}"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated; Check: NeedsVCRedist
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent

[Code]
const
  VCRedistRegistryKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

function NeedsVCRedist: Boolean;
var
  InstalledFlag: Cardinal;
  InstalledVersion: String;
  InstalledPacked: Int64;
  BundledPacked: Int64;
begin
  Result := True;
  if not RegQueryDWordValue(HKLM64, VCRedistRegistryKey, 'Installed', InstalledFlag) then
    Exit;
  if InstalledFlag <> 1 then
    Exit;
  if not RegQueryStringValue(HKLM64, VCRedistRegistryKey, 'Version', InstalledVersion) then
    Exit;

  // Реестр обычно хранит версию с ведущей буквой "v".
  if (Length(InstalledVersion) > 0) and (InstalledVersion[1] = 'v') then
    Delete(InstalledVersion, 1, 1);
  if not StrToVersion(InstalledVersion, InstalledPacked) then
    Exit;
  if not GetPackedVersion(ExpandConstant('{tmp}\{#VCRedistName}'), BundledPacked) then
    Exit;

  Result := ComparePackedVersion(InstalledPacked, BundledPacked) < 0;
end;
