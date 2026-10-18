#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#define AppName "TStudio"
#define Repository "https://github.com/Luckyz-z/TStudio"

[Setup]
AppId={{6F1C2E7A-3B9D-4F2A-9E57-7D0B1C8A2F11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=TStudio contributors
AppPublisherURL={#Repository}
AppSupportURL={#Repository}/issues
AppUpdatesURL={#Repository}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=TStudio-{#AppVersion}-Setup
SetupIconFile=icon.ico
UninstallDisplayIcon={app}\TStudio.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern dynamic
WizardImageFile=wizard-164.bmp,wizard-192.bmp,wizard-246.bmp,wizard-273.bmp,wizard-328.bmp,wizard-410.bmp
WizardSmallImageFile=small-55.bmp,small-64.bmp,small-69.bmp,small-92.bmp,small-110.bmp,small-119.bmp,small-138.bmp
WizardImageFileDynamicDark=wizard-164.bmp,wizard-192.bmp,wizard-246.bmp,wizard-273.bmp,wizard-328.bmp,wizard-410.bmp
WizardSmallImageFileDynamicDark=small-55.bmp,small-64.bmp,small-69.bmp,small-92.bmp,small-110.bmp,small-119.bmp,small-138.bmp
DisableWelcomePage=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
CloseApplications=yes

[Messages]
WelcomeLabel1=Welcome to TStudio
WelcomeLabel2=TStudio is a free, open-source DAW with the workflow FL Studio producers already know: channel rack, piano roll, playlist and mixer, plus Lua plugins, native plugins and AI control.%n%nThis will install TStudio {#AppVersion} on your computer.
FinishedHeadingLabel=TStudio is ready
FinishedLabel=Setup installed TStudio. Open it, hit Space and make something.

[Types]
Name: "full"; Description: "Everything (recommended)"
Name: "app"; Description: "Only TStudio"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "app"; Description: "TStudio"; Types: full app custom; Flags: fixed
Name: "vst3"; Description: "Lua loader VST3 plugins (use TStudio Lua plugins in FL Studio, Ableton, Reaper...)"; Types: full
Name: "sdk"; Description: "Plugin SDK and examples (for developers)"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "associate"; Description: "Open .tsp projects with TStudio"; GroupDescription: "File types:"

[Files]
Source: "{#BuildDir}\TStudio_artefacts\Release\TStudio.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#BuildDir}\TStudio_artefacts\Release\Plugins\*"; DestDir: "{app}\Plugins"; Components: app; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "..\LICENSE"; DestDir: "{app}"; Components: app
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.md"; Components: app
Source: "{#BuildDir}\TStudioLuaFX_artefacts\Release\VST3\TStudio Lua FX.vst3\*"; DestDir: "{code:Vst3Folder}\TStudio Lua FX.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\TStudioLuaSynth_artefacts\Release\VST3\TStudio Lua Synth.vst3\*"; DestDir: "{code:Vst3Folder}\TStudio Lua Synth.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs
Source: "..\SDK\*"; DestDir: "{app}\SDK"; Components: sdk; Flags: ignoreversion recursesubdirs
Source: "..\docs\*.md"; DestDir: "{app}\docs"; Components: sdk; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\TStudio"; Filename: "{app}\TStudio.exe"
Name: "{autoprograms}\TStudio on GitHub"; Filename: "{#Repository}"
Name: "{autodesktop}\TStudio"; Filename: "{app}\TStudio.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.tsp"; ValueType: string; ValueName: ""; ValueData: "TStudio.Project"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\TStudio.Project"; ValueType: string; ValueName: ""; ValueData: "TStudio project"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\TStudio.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\TStudio.exe"",0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\TStudio.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\TStudio.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\TStudio.exe"; Description: "Launch TStudio"; Flags: nowait postinstall skipifsilent

[Code]
var
  CommunityPage: TWizardPage;
  StarBox: TNewCheckBox;

function Vst3Folder(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commoncf64}\VST3')
  else
    Result := ExpandConstant('{localappdata}\Programs\Common\VST3');
end;

procedure AddLabel(Page: TWizardPage; Top: Integer; Text: String; Bold: Boolean; Size: Integer);
var
  Caption: TNewStaticText;
begin
  Caption := TNewStaticText.Create(Page);
  Caption.Parent := Page.Surface;
  Caption.Top := ScaleY(Top);
  Caption.Width := Page.SurfaceWidth;
  Caption.AutoSize := False;
  Caption.WordWrap := True;
  Caption.Height := ScaleY(48);
  Caption.Caption := Text;
  Caption.Font.Size := Size;
  if Bold then
    Caption.Font.Style := [fsBold];
end;

procedure InitializeWizard;
begin
  CommunityPage := CreateCustomPage(wpSelectTasks, 'Join the TStudio community', 'TStudio is free forever, and it grows with the people who use it.');
  AddLabel(CommunityPage, 0, 'Make plugins, share presets, report bugs and send pull requests.', True, 10);
  AddLabel(CommunityPage, 34, 'Lua plugins can ship their own custom UIs and work inside FL Studio through the bundled loader VST3. Free or paid, it is your call. See the TStudio-Plugins repository on GitHub.', False, 9);
  AddLabel(CommunityPage, 96, 'One click helps a lot: a GitHub star helps other producers find TStudio.', False, 9);
  StarBox := TNewCheckBox.Create(CommunityPage);
  StarBox.Parent := CommunityPage.Surface;
  StarBox.Top := ScaleY(140);
  StarBox.Width := CommunityPage.SurfaceWidth;
  StarBox.Height := ScaleY(20);
  StarBox.Caption := 'Open the GitHub page after setup so I can star TStudio';
  StarBox.Checked := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if (CurStep = ssDone) and (not WizardSilent) and StarBox.Checked then
    ShellExec('open', '{#Repository}', '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
end;
