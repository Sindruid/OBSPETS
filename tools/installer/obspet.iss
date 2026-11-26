; OBSPETS's Windows installer (Inno Setup 6.7+). Built by tools/package.py, which passes:
;   /DAppVersion=0.2.0  /DStage=<folder holding obspet\ and the text files>  /DOutDir=<dist>
; and optionally /DSignCmd=1 with /Sobspet=<sign command> to sign Setup and the uninstaller.
;
; It copies obspet\ to %ProgramData%\obs-studio\plugins\obspet (where OBS loads plugins for every
; user, so Windows asks for permission once), waits for OBS to be closed, and can open OBS when
; done. It shows up in Windows' "Installed apps" as OBSPETS, which uninstalls it. The user's own
; characters, settings and sign-in live in %APPDATA%\obs-studio\plugin_config\obspet and stay.
;
; For testing without touching a real OBS: /DTest=1 installs for the current user only into
; /DTestDir=<folder>, /DObsProcess=<name.exe> changes which program counts as "OBS is open",
; /DTestObs=<path to obs64.exe> is the OBS it looks for and opens, /DObsParams=--multi, and
; /DTestConfig=<folder> is the plugin's config folder (where account.json is).

#ifndef AppVersion
  #error Build this with tools/package.py
#endif
#ifndef ObsProcess
  #define ObsProcess "obs64.exe"
#endif
#ifndef TestConfig
  #define TestConfig "{userappdata}\obs-studio\plugin_config\obspet"
#endif
#ifndef ObsParams
  #define ObsParams ""
#endif

[Setup]
AppId={{6A0C7B2E-3F1D-4E8A-9C55-0B7E2D4F9A31}
AppName=OBSPETS
AppVersion={#AppVersion}
AppVerName=OBSPETS {#AppVersion}
AppPublisher=Sindruid
AppPublisherURL=https://sindruid.com/streamtools/obspets
AppSupportURL=https://sindruid.com/streamtools/obspets
VersionInfoVersion={#AppVersion}
VersionInfoDescription=OBSPETS Setup
#ifdef Test
PrivilegesRequired=lowest
DefaultDirName={#TestDir}
#else
PrivilegesRequired=admin
DefaultDirName={commonappdata}\obs-studio\plugins\obspet
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableWelcomePage=no
UsePreviousAppDir=no
UninstallFilesDir={app}\uninstall
UninstallDisplayName=OBSPETS (OBS plugin)
UninstallDisplayIcon={app}\uninstall\obspet.ico
; OBS is handled in [Code], politely: Windows' own "close these apps" page could close it mid-stream.
; {userappdata} is only read when uninstalling, to sign this OBS out (see [Code]).
UsedUserAreasWarning=no
CloseApplications=no
RestartApplications=no
WizardStyle=modern dynamic
WizardSizePercent=100
#define WizImages "art\wizard-164.png,art\wizard-205.png,art\wizard-246.png,art\wizard-287.png,art\wizard-328.png"
#define SmallImages "art\small-55.png,art\small-68.png,art\small-82.png,art\small-96.png,art\small-110.png"
; Blob, on a transparent background, in both looks; never stretched, so its pixels stay sharp.
WizardImageFile={#WizImages}
WizardImageFileDynamicDark={#WizImages}
WizardSmallImageFile={#SmallImages}
WizardSmallImageFileDynamicDark={#SmallImages}
WizardImageStretch=no
SetupIconFile=art\obspet.ico
OutputDir={#OutDir}
OutputBaseFilename=OBSPETS-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
#ifdef SignCmd
SignTool=obspet
SignedUninstaller=yes
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
SetupAppTitle=OBSPETS Setup
SetupWindowTitle=OBSPETS Setup
UninstallAppTitle=Remove OBSPETS
UninstallAppFullTitle=Remove OBSPETS
WelcomeLabel1=Install OBSPETS
WelcomeLabel2=OBSPETS adds a pet to OBS. It lives along the bottom of your stream and reacts to follows, subs, bits, raids, channel points and chat.%n%nIt needs OBS Studio 33 or newer.%n%nYour settings and characters stay if you're updating.
FinishedHeadingLabel=OBSPETS is installed
FinishedLabelNoIcons=Next, in OBS: Tools, then OBSPETS: Add a pet to this scene. In the window that opens, click Sign in with Twitch (or Kick).
FinishedLabel=Next, in OBS: Tools, then OBSPETS: Add a pet to this scene. In the window that opens, click Sign in with Twitch (or Kick).
ClickFinish=
ClickNext=
ConfirmUninstall=Remove OBSPETS from OBS?%n%nIt also signs this OBS out. Your settings and characters are kept, in case you install it again.
UninstalledAll=OBSPETS was removed.

[InstallDelete]
; Files a previous version left behind (and the bin\64bit layout OBS 33 calls "Legacy"). The
; bundled characters are replaced whole; the user's own are elsewhere and untouched.
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\bin"

[Files]
Source: "{#Stage}\obspet\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\THIRD-PARTY.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\SOURCE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "art\obspet.ico"; DestDir: "{app}\uninstall"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Run]
Filename: "{code:ObsExe}"; Parameters: "{#ObsParams}"; WorkingDir: "{code:ObsDir}"; Description: "Open OBS"; Flags: postinstall nowait skipifsilent runasoriginaluser; Check: HaveObs

[Code]
var
  ObsPath: String;

function FindObs(): String;
var
  Dir: String;
begin
  Result := '';
#ifdef TestObs
  if FileExists('{#TestObs}') then Result := '{#TestObs}';
  Exit;
#endif
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Dir) and
     FileExists(AddBackslash(Dir) + 'bin\64bit\obs64.exe') then
    Result := AddBackslash(Dir) + 'bin\64bit\obs64.exe'
  else if FileExists(ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe')) then
    Result := ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe');
end;

function HaveObs(): Boolean;
begin
  Result := ObsPath <> '';
end;

function ObsExe(Param: String): String;
begin
  Result := ObsPath;
end;

function ObsDir(Param: String): String;
begin
  Result := ExtractFileDir(ObsPath);
end;

function ObsRunning(): Boolean;
var
  Wmi, Svc, Found: Variant;
begin
  Result := False;
  try
    Wmi := CreateOleObject('WbemScripting.SWbemLocator');
    Svc := Wmi.ConnectServer('.', 'root\CIMV2');
    Found := Svc.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''{#ObsProcess}''');
    Result := Found.Count > 0;
  except
    Result := False;   // can't tell: carry on rather than block
  end;
end;

{ Waits for OBS to be closed: OBS loads plugins only when it starts and holds their files while it
  runs. True once it's closed, False if the person clicked Cancel. }
function WaitForObsClosed(): Boolean;
begin
  Result := True;
  while ObsRunning() do
    if TaskDialogMsgBox('Close OBS first',
        'OBS is still open. In OBS, click File, then Exit. Then click Try again.',
        mbInformation, MB_OKCANCEL, ['Try again', 'Cancel'], 0) <> IDOK then
    begin
      Result := False;
      Exit;
    end;
end;

function InitializeSetup(): Boolean;
var
  Ms, Ls: Cardinal;
  Major: Integer;
begin
  Result := True;
  ObsPath := FindObs();
  if ObsPath = '' then
  begin
    Result := WizardSilent() or (TaskDialogMsgBox('OBS Studio wasn''t found',
        'OBSPETS needs OBS Studio 33 or newer, from obsproject.com.' + #13#10#13#10 +
        'If OBS is installed somewhere else (through Steam, for example), click Install anyway.',
        mbConfirmation, MB_OKCANCEL, ['Install anyway', 'Cancel'], 0) = IDOK);
  end
  else if GetVersionNumbers(ObsPath, Ms, Ls) then
  begin
    Major := Ms shr 16;
    if Major < 33 then
      Result := WizardSilent() or (TaskDialogMsgBox('OBS needs an update',
          'OBSPETS needs OBS Studio 33 or newer, and this PC has OBS ' + IntToStr(Major) + '.' + #13#10#13#10 +
          'To update it, open OBS and click Help, then Check For Updates. You can install OBSPETS now and update OBS after.',
          mbConfirmation, MB_OKCANCEL, ['Install anyway', 'Cancel'], 0) = IDOK);
  end;
end;

{ Clicking Install on the first page: OBS has to be closed. Cancel stays on that page. }
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpWelcome then
    Result := WaitForObsClosed();
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if WizardSilent() and ObsRunning() then
    Result := 'OBS is open. Close OBS, then run Setup again.';
end;

{ The ready page is skipped, so the first page starts the install: call its button Install. }
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpWelcome then
    WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall);
end;

function InitializeUninstall(): Boolean;
begin
  if UninstallSilent() then
    Result := not ObsRunning()
  else
    Result := WaitForObsClosed();
end;

{ Uninstalling deletes this PC's sign-in (account.json, encrypted for its Windows user). The OBS
  stays in the website's "Your OBS" list, where "Sign it out" removes it. The rest of the plugin's
  config stays. }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path: String;
begin
  if CurUninstallStep <> usUninstall then Exit;
  Path := ExpandConstant('{#TestConfig}\account.json');
  DeleteFile(Path);
  DeleteFile(Path + '.bak');
  DeleteFile(Path + '.tmp');
end;
