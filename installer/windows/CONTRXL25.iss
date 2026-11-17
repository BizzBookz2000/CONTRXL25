; ==========================================================================
;  CONTRXL 25 - Windows setup (Inno Setup 6)
;  Built by make_installer_windows.bat, which passes the version:
;      ISCC.exe /DAppVersion=0.84.4 installer\windows\CONTRXL25.iss
;  Result: installers\CONTRXL25-<version>-Windows-Setup.exe
;
;  The setup wizard: Welcome > License agreement > Install for me / everyone >
;  Folder > Start Menu folder > Desktop shortcut > Install > Launch.
;  Adds an uninstaller to Settings > Apps. Installing a newer version over an
;  older one updates it in place (same AppId).
; ==========================================================================

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName      "CONTRXL 25"
#define AppExe       "CONTRXL 25.exe"
#define AppPublisher "Illahtainment"
#define Root         "..\.."

[Setup]
; Never change the AppId: Windows uses it to recognise updates and the uninstaller.
AppId={{1E75D921-A632-4328-A73B-880E96A8FDE6}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright=Copyright (c) 2026 {#AppPublisher}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=auto
LicenseFile={#Root}\installer\common\EULA.txt
OutputDir={#Root}\installers
OutputBaseFilename=CONTRXL25-{#AppVersion}-Windows-Setup
SetupIconFile=icon.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
DisableWelcomePage=no
WizardImageFile=wizard.bmp,wizard-200.bmp
WizardSmallImageFile=wizard-small.bmp,wizard-small-200.bmp
Compression=lzma2/max
SolidCompression=yes
; 64-bit Windows 10 / 11 only (the app and the plug-ins it hosts are 64-bit)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; Ask "Install for me only" or "for all users" (all users needs administrator rights)
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
; Close CONTRXL 25 if it is running while updating / uninstalling
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Root}\dist\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
; the safe plug-in scanner must sit next to the app
Source: "{#Root}\dist\CONTRXL25_PluginScanner.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\docs\CONTRXL 25 User's Manual.pdf"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Root}\installer\common\EULA.txt"; DestDir: "{app}"; DestName: "License.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{#AppName} User's Manual"; Filename: "{app}\CONTRXL 25 User's Manual.pdf"; Check: ManualInstalled
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
function ManualInstalled: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\CONTRXL 25 User''s Manual.pdf'));
end;

{ Uninstall: the app itself is removed. Settings, MIDI links and the plug-in list (files in
  %APPDATA%\CONTRXL25) are removed only if the user says so. The sample library folder
  (%APPDATA%\CONTRXL25\Library) and songs are never touched. }
procedure DeleteSettingsFiles();
var
  Dir: String;
  Rec: TFindRec;
begin
  Dir := ExpandConstant('{userappdata}\CONTRXL25');
  if FindFirst(Dir + '\*', Rec) then
  begin
    try
      repeat
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
          DeleteFile(Dir + '\' + Rec.Name);
      until not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) and
     DirExists(ExpandConstant('{userappdata}\CONTRXL25')) then
  begin
    if MsgBox('Also remove your CONTRXL 25 settings, MIDI links and plug-in list?' + #13#10 + #13#10 +
              'Your sample library and your songs are kept either way.',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DeleteSettingsFiles();
  end;
end;
