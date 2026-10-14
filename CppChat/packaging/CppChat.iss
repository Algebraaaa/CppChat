; Tested with Inno Setup 6.7.3. Parameters are supplied by build-installer.ps1.
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef StageDir
  #error StageDir is required
#endif
#ifndef PackageDir
  #error PackageDir is required
#endif
#ifndef AppIcon
  #error AppIcon is required
#endif

[Setup]
AppId={{D84D25B0-04A5-48F8-B3C9-04937B05E2AD}
AppName=CppChat
AppVersion={#AppVersion}
AppPublisher=Algebra
DefaultDirName={localappdata}\Programs\CppChat
DefaultGroupName={param:GROUP|CppChat}
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\CppChat.exe
OutputDir={#PackageDir}
OutputBaseFilename=CppChat-Setup-{#AppVersion}-x64
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
LicenseFile={#StageDir}\licenses\CppChat-MIT.txt
#ifdef SigningEnabled
SignTool=cppchat
SignedUninstaller=yes
SignedUninstallerDir={#SigningWorkDir}\signed-uninstallers
#endif

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "config.ini"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\config.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\CppChat"; Filename: "{app}\CppChat.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall CppChat"; Filename: "{uninstallexe}"
Name: "{autodesktop}\CppChat"; Filename: "{app}\CppChat.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\CppChat.exe"; Description: "Launch CppChat"; Flags: nowait postinstall skipifsilent

; User-generated chat_data and logs are not installer files. Never delete them
; with wildcard [UninstallDelete] entries; updates also leave them untouched.

[Code]
procedure ProtectInstalledData();
var
  ResultCode: Integer;
begin
  if not Exec(ExpandConstant('{app}\CppChat.exe'), '--secure-installation',
    ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    RaiseException('Could not start local data protection.');
  if ResultCode <> 0 then
    RaiseException('Local data protection failed. Original data and recovery backups were retained.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    ProtectInstalledData();
end;
