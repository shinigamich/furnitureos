; FurnitureOS per-user Windows installer (Inno Setup 6.3 or newer). Saved as UTF-8 with BOM (Ukrainian text).
; SPDX-License-Identifier: AGPL-3.0-only
;
; Installs without administrator rights into %LOCALAPPDATA%\Programs\FurnitureOS, adds Start-menu
; and optional desktop shortcuts, a per-user .fosp file association (HKCU) and an uninstaller, and
; writes installed.txt next to furnitureos.exe so the application can tell it was installed by this
; setup (the portable ZIP has no such file).
;
; Built by .github/workflows/release.yml:
;   ISCC /DAppVersion=0.1.0-beta.1 /DAppVersionNumeric=0.1.0 /DStageDir=<portable folder>
;        /DOutputDir=<dir> /DOutputBaseName=FurnitureOS-0.1.0-beta.1-windows-x64-setup furnitureos.iss
;
; Silent install / upgrade (used by the in-app updater; same AppId = in-place upgrade):
;   FurnitureOS-X.Y.Z-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART [/LOG="file"]
; Silent uninstall:  "%LOCALAPPDATA%\Programs\FurnitureOS\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

#ifndef AppVersion
  #error AppVersion must be defined (e.g. /DAppVersion=0.1.0)
#endif
#ifndef AppVersionNumeric
  #error AppVersionNumeric must be defined (X.Y.Z, without pre-release suffix)
#endif
#ifndef StageDir
  #error StageDir must point to the installed portable folder (cmake --install ... --prefix)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputBaseName
  #define OutputBaseName "FurnitureOS-" + AppVersion + "-windows-x64-setup"
#endif

#define AppName "FurnitureOS"
#define AppExe "furnitureos.exe"
#define ProgId "FurnitureOS.Project"

[Setup]
; Never change AppId: it identifies the installation for upgrades and uninstall.
AppId={{6F3C2B1E-8D4A-4E7B-9A35-0C1F5E2D7B90}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=FurnitureOS contributors
AppPublisherURL=https://github.com/shinigamich/furnitureos
AppSupportURL=https://github.com/shinigamich/furnitureos/issues
AppUpdatesURL=https://github.com/shinigamich/furnitureos/releases
VersionInfoVersion={#AppVersionNumeric}.0
VersionInfoProductVersion={#AppVersionNumeric}.0
VersionInfoProductTextVersion={#AppVersion}
; Per-user installation: no UAC prompt. With PrivilegesRequired=lowest {autopf} is
; %LOCALAPPDATA%\Programs, {group} is the user's Start menu and HKA is HKEY_CURRENT_USER.
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesAssociations=yes
; Upgrades: close a running FurnitureOS (Restart Manager) instead of failing on locked files.
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[CustomMessages]
en.AssocTask=Open .fosp project files with FurnitureOS
uk.AssocTask=Відкривати файли проєктів .fosp у FurnitureOS
en.OtherTasks=File types:
uk.OtherTasks=Типи файлів:
en.ProjectFile=FurnitureOS project
uk.ProjectFile=Проєкт FurnitureOS

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "fileassoc"; Description: "{cm:AssocTask}"; GroupDescription: "{cm:OtherTasks}"

[InstallDelete]
; Qt plugin folders are fully owned by the installer: drop stale plugins from an older version.
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\printsupport"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\tls"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{userdocs}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{userdocs}"; Tasks: desktopicon

[Registry]
; Per-user file association (HKA = HKCU for a per-user install). Removed by the uninstaller.
Root: HKA; Subkey: "Software\Classes\.fosp"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\.fosp\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "{cm:ProjectFile}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".fosp"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\installed.txt"

[Code]
// Marker for the in-app updater: present only in folders installed by this setup.
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    if not SaveStringToFile(ExpandConstant('{app}\installed.txt'),
        'Installed by the FurnitureOS setup {#AppVersion}.' + #13#10 +
        'Do not delete: the in-app updater uses this file to update with the setup program.' + #13#10,
        False) then
      Log('Could not write installed.txt');
end;
