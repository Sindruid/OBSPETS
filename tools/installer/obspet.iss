; OBSPETS's Windows installer (Inno Setup 6.7+). Built by tools/package.py, which passes:
;   /DAppVersion=0.2.0  /DStage=<folder holding obspet\ and the text files>  /DOutDir=<dist>
; and optionally /DSignCmd=1 with /Sobspet=<sign command> to sign Setup and the uninstaller.
;
; It copies obspet\ to %ProgramData%\obs-studio\plugins\obspet (where OBS loads plugins for every
; user, so Windows asks for permission once), waits for OBS to be closed, and can open OBS when
; done. It shows up in Windows' "Installed apps" as OBSPETS, which uninstalls it. The user's own
; characters, settings and sign-in live in %APPDATA%\obs-studio\plugin_config\obspet and stay.
;
; Update mode, started by the plugin's Update card (src/updater.cpp) with /VERYSILENT and
; /OBSPETUPDATE=<update.txt>: Windows asks for permission, Setup writes the "ready" file named in
; update.txt (OBS then closes itself), waits up to 2 minutes for that OBS to be gone (or for the "cancel"
; file), installs, and opens that same OBS again with its own command line, as the user who
; started it (not elevated).
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
  { update mode (see the top) }
  UpdateMode, ObsClosed, Reopened: Boolean;
  UpdPid, UpdExe, UpdDir, UpdArgs, UpdReady, UpdCancel, UpdVersion: String;

{ update.txt, written by the plugin (UTF-8):
    OBSPETS-UPDATE-1 / OBS's process id / obs64.exe / its folder / its arguments / the ready file /
    the cancel file / the version it updates to }
function ReadUpdateTask(Path: String): Boolean;
var
  Lines: TArrayOfString;
begin
  Result := False;
  if not LoadStringsFromFile(Path, Lines) then
  begin
    Log('update: can''t read ' + Path);
    Exit;
  end;
  if (GetArrayLength(Lines) < 8) or (Lines[0] <> 'OBSPETS-UPDATE-1') then
  begin
    Log('update: ' + Path + ' isn''t an update task');
    Exit;
  end;
  UpdPid := Trim(Lines[1]);
  UpdExe := Lines[2];
  UpdDir := Lines[3];
  UpdArgs := Lines[4];
  UpdReady := Lines[5];
  UpdCancel := Lines[6];
  UpdVersion := Lines[7];
  Result := (StrToIntDef(UpdPid, 0) > 0) and FileExists(UpdExe);
  Log('update: to ' + UpdVersion + ', for OBS ' + UpdPid + ' (' + UpdExe + ' ' + UpdArgs + ')');
end;

function PidRunning(Pid: String): Boolean;
var
  Wmi, Svc, Found: Variant;
begin
  Result := False;
  try
    Wmi := CreateOleObject('WbemScripting.SWbemLocator');
    Svc := Wmi.ConnectServer('.', 'root\CIMV2');
    Found := Svc.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE ProcessId = ' + IntToStr(StrToIntDef(Pid, 0)));
    Result := Found.Count > 0;
  except
    Result := False;
  end;
end;

{ Opens OBS again the way it was started, as the user who started Setup (OBS shouldn't run elevated). }
procedure ReopenObs();
var
  Rc: Integer;
begin
  if Reopened or not UpdateMode or not ObsClosed then Exit;
  Reopened := True;
  if not ExecAsOriginalUser(UpdExe, UpdArgs, UpdDir, SW_SHOWNORMAL, ewNoWait, Rc) then
    Log('update: couldn''t open OBS again (' + SysErrorMessage(Rc) + ')')
  else
    Log('update: opened OBS again');
end;

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
  if ExpandConstant('{param:OBSPETUPDATE|}') <> '' then
  begin
    { The plugin started this to update itself: only with its task, and only silently }
    Result := WizardSilent() and ReadUpdateTask(ExpandConstant('{param:OBSPETUPDATE|}'));
    if not Result then Exit;
    UpdateMode := True;
    ObsPath := UpdExe;
    { Windows gave permission (this runs elevated): now OBS may close }
    if not SaveStringToFile(UpdReady, 'ok', False) then
      Log('update: couldn''t write ' + UpdReady);
    Exit;
  end;
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
  { (also called when silent; an update waits for OBS in PrepareToInstall instead) }
  if (CurPageID = wpWelcome) and not UpdateMode then
    Result := WaitForObsClosed();
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  I: Integer;
begin
  Result := '';
  if UpdateMode then
  begin
    { OBS closes itself once it sees the ready file: wait for it (and for every other OBS) }
    for I := 1 to 480 do
    begin
      if FileExists(UpdCancel) then
      begin
        Result := 'The update was called off in OBS.';
        Log('update: ' + Result);
        Exit;
      end;
      if not PidRunning(UpdPid) and not ObsRunning() then
      begin
        ObsClosed := True;
        Log('update: OBS is closed');
        Exit;
      end;
      Sleep(250);
    end;
    Result := 'OBS didn''t close.';
    Log('update: ' + Result);
    Exit;
  end;
  if WizardSilent() and ObsRunning() then
    Result := 'OBS is open. Close OBS, then run Setup again.';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssDone then
    ReopenObs();
end;

{ Also when the install failed after OBS closed: the OBS that was open comes back either way }
procedure DeinitializeSetup();
begin
  try
    ReopenObs();
  except
    Log('update: ' + GetExceptionMessage());
  end;
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
