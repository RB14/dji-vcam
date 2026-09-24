; Windows installer for DJI VCam (Inno Setup 6).
;
; Built by app/scripts/package-windows.sh from the packaged app folder:
;   ISCC.exe /DAppVersion=0.1.0 /DSourceDir=<app folder> /DOutputDir=<binaries> dji-vcam.iss
;
; Installs the app into Program Files, registers the virtual camera's media source from there (the
; Windows camera services can read Program Files, so the app never has to ask for administrator
; rights itself), lets the camera's video in through Windows Firewall, and removes all of it on
; uninstall.

#ifndef AppVersion
  #error Define AppVersion, e.g. /DAppVersion=0.1.0
#endif
#ifndef SourceDir
  #error Define SourceDir, the packaged app folder
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName "DJI VCam"
#define AppExe "dji-vcam.exe"
#define MediaSourceDll "djivcam-source.dll"

[Setup]
; Never change AppId: upgrades and the uninstaller find earlier installs by it.
AppId={{C9375695-7CAF-4756-A78D-C5A9F3A38C8F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=DJI VCam project
AppComments=DJI Osmo Action live view as a webcam
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Registering the media source (HKLM) and the firewall rule need administrator rights.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Media Foundation virtual cameras (MFCreateVirtualCamera) exist from Windows 11 on.
MinVersion=10.0.22000
OutputDir={#OutputDir}
OutputBaseFilename=dji-vcam-setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
; Close a running DJI VCam before replacing its files.
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "{#MediaSourceDll}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\{#MediaSourceDll}"; DestDir: "{app}"; Flags: ignoreversion regserver

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; The video arrives as UDP from the camera; allow it for the app only, on any network profile
; (the camera's network is usually classified as Public). Delete first so upgrades do not stack
; duplicate rules.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#AppName}"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""{#AppName}"" dir=in action=allow program=""{app}\{#AppExe}"" enable=yes profile=any"; Flags: runhidden; StatusMsg: "Allowing the camera's video through Windows Firewall..."
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#AppName}"""; Flags: runhidden; RunOnceId: "RemoveFirewallRule"

[UninstallDelete]
; Media source logs, and the copy the app installs itself when run from the portable ZIP.
Type: filesandordirs; Name: "{commonappdata}\DJI VCam"

[Code]
// The Windows Camera Frame Server keeps the media source DLL loaded after an app used the webcam.
// Stop it (it starts again on demand) so the DLL can be replaced or deleted.
procedure StopCameraServices();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServerMonitor /y', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServer /y', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if FileExists(ExpandConstant('{app}\{#MediaSourceDll}')) then
    StopCameraServices();
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    StopCameraServices();
end;
