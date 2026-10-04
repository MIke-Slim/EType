; Validation is compile-time only. Production has no registration bypass.
#ifndef AppVersion
  #define AppVersion "0.1.1"
#endif
#ifdef InstallerTestMode
  #define PackageId "EType.InstallerValidation"
  #define Title "EType 安装流程验证"
#else
  #define PackageId "EType.WindowsInputMethod"
  #define Title "EType 英文词汇输入法"
#endif

[Setup]
AppId={#PackageId}
AppName={#Title}
AppVersion={#AppVersion}
AppPublisher=EType
AppPublisherURL=https://github.com/MIke-Slim/EType
DefaultDirName={autopf}\EType
DisableDirPage=no
DisableProgramGroupPage=yes
AllowRootDirectory=no
AllowUNCPath=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
AlwaysShowDirOnReadyPage=yes
AppendDefaultDirName=yes
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
WizardStyle=modern
SetupIconFile=..\assets\etype.ico
SetupLogging=yes
Compression=lzma2/max
SolidCompression=yes
CloseApplications=no
AllowCancelDuringInstall=yes
RestartApplications=no
UninstallDisplayIcon={app}\EType.exe
UninstallDisplayName={#Title}
UninstallFilesDir={app}
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription=EType Windows 输入法安装包
#ifdef InstallerTestMode
PrivilegesRequired=lowest
OutputDir=..\build\installer-tests
OutputBaseFilename=EType-Setup-Validation
#else
PrivilegesRequired=admin
OutputDir=..\releases
OutputBaseFilename=EType-{#AppVersion}-Setup
#endif

[Languages]
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
#ifdef InstallerTestMode
Source: "validation.id"; DestDir: "{app}"; DestName: "etype-installation.id"; Flags: ignoreversion
#else
Source: "production.id"; DestDir: "{app}"; DestName: "etype-installation.id"; Flags: ignoreversion
#endif
Source: "..\build\EType\*"; DestDir: "{app}"; Excludes: "x64\*,x86\*"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef InstallerTestMode
Source: "..\build\EType\x86\EType.dll"; DestDir: "{app}\x86"; Flags: ignoreversion
Source: "..\build\EType\x64\EType.dll"; DestDir: "{app}\x64"; Flags: ignoreversion; AfterInstall: RegisterComponents
#else
Source: "..\build\EType\x86\EType.dll"; DestDir: "{app}\x86"; Flags: ignoreversion regserver 32bit
Source: "..\build\EType\x64\EType.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit; AfterInstall: RegisterComponents

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked

[Icons]
Name: "{autoprograms}\EType 英文词汇输入法"; Filename: "{app}\EType.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\EType 英文词汇输入法"; Filename: "{app}\EType.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\EType.exe"; Description: "打开 EType 设置与试用"; Flags: nowait postinstall skipifsilent runasoriginaluser
#endif

[Messages]
chinesesimp.SelectDirDesc=选择 EType 的安装位置
chinesesimp.SelectDirLabel3=请选择一个专用于 EType 的文件夹。可点击“浏览”自主选择安装路径。
chinesesimp.FinishedLabelNoIcons=EType 已安装。请使用 Windows 输入法切换菜单选择 EType；已打开的软件可能需要重新打开。

[Code]
const
  OwnUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#PackageId}_is1';
  ETypeClassKey = 'Software\Classes\CLSID\{DC168F35-18EA-4EC5-B391-C4430C3F3ED9}\InprocServer32';
  ETypeMarker = 'etype-installation.id';
var
  ExistingDir: String;
  RegistrationFailed: Boolean;

function CreateFileW(lpFileName: String; dwDesiredAccess, dwShareMode: Cardinal;
  lpSecurityAttributes: Integer; dwCreationDisposition, dwFlagsAndAttributes: Cardinal;
  hTemplateFile: Integer): Integer;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseHandle(hObject: Integer): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function GetFileAttributesW(lpFileName: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function Canonical(const Path: String): String;
begin
  Result := RemoveBackslashUnlessRoot(ExpandFileName(Path));
end;

function HasReparseParent(const Path: String): Boolean;
var
  Part, Parent: String;
  Attributes: Cardinal;
begin
  Result := False;
  Part := Canonical(Path);
  repeat
    Attributes := GetFileAttributesW(Part);
    if (Attributes <> $FFFFFFFF) and ((Attributes and $400) <> 0) then begin
      Result := True;
      exit;
    end;
    Parent := ExtractFileDir(Part);
    if CompareText(Part, Parent) = 0 then exit;
    Part := Parent;
  until Part = '';
end;

function NonEmptyDirectory(const Path: String): Boolean;
var Found: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Path) + '*', Found) then begin
    try
      repeat
        if (Found.Name <> '.') and (Found.Name <> '..') then begin
          Result := True;
          exit;
        end;
      until not FindNext(Found);
    finally
      FindClose(Found);
    end;
  end;
end;

function MarkerMatches(const Path: String): Boolean;
var Marker: AnsiString;
begin
  Result := LoadStringFromFile(AddBackslash(Path) + ETypeMarker, Marker) and
    (Trim(String(Marker)) = '{#PackageId}');
end;

function PathError(const Directory: String): String;
var Path: String;
begin
  Result := '';
  Path := Canonical(Directory);
  if (Length(Path) < 4) or (Copy(Path, 2, 2) <> ':\') or (Length(Path) > 180) then
    Result := '请选择本机磁盘上长度不超过 180 个字符的独立安装文件夹。'
  else if (CompareText(Path, ExtractFileDrive(Path) + '\') = 0) or
    (CompareText(Path, Canonical(ExpandConstant('{win}'))) = 0) or
    (CompareText(Path, Canonical(ExpandConstant('{sys}'))) = 0) or
    (CompareText(Path, Canonical(ExpandConstant('{autopf}'))) = 0) or
    (CompareText(Path, Canonical(GetEnv('USERPROFILE'))) = 0) then
    Result := '不能将磁盘根目录或系统公共目录作为安装位置。'
  else if HasReparseParent(Path) then
    Result := '安装目录及其父目录不能是链接或目录联接，请选择普通本机文件夹。'
  else if (ExistingDir <> '') and (CompareText(Path, ExistingDir) <> 0) then
    Result := 'EType 已安装在 ' + ExistingDir + '。升级请使用原位置；如需更换路径，请先卸载原安装。'
  else if DirExists(Path) and NonEmptyDirectory(Path) and
    not ((ExistingDir <> '') and (CompareText(Path, ExistingDir) = 0) and MarkerMatches(Path)) then
    Result := '所选文件夹不为空且不是本安装器管理的 EType 目录。请另选一个空文件夹。';
end;

function FileUnlocked(const Path: String): Boolean;
var Handle: Integer;
begin
  Result := True;
  if not FileExists(Path) then exit;
  Handle := CreateFileW(Path, $80000000 or $40000000, 0, 0, 3, $80, 0);
  if Handle = -1 then Result := False else CloseHandle(Handle);
end;

function InitializeSetup(): Boolean;
var RegisteredDll: String; I: Integer;
begin
  ExistingDir := '';
  RegistrationFailed := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/NOCANCEL') = 0 then begin
      SuppressibleMsgBox('此安装包需要保留失败时的取消和回滚能力，不支持 /NOCANCEL。', mbError, MB_OK, IDOK);
      Result := False;
      exit;
    end;
#ifdef InstallerTestMode
  RegQueryStringValue(HKCU64, OwnUninstallKey, 'InstallLocation', ExistingDir);
#else
  RegQueryStringValue(HKLM64, OwnUninstallKey, 'InstallLocation', ExistingDir);
  if ExistingDir = '' then begin
    if RegQueryStringValue(HKLM64, ETypeClassKey, '', RegisteredDll) or
      RegQueryStringValue(HKLM32, ETypeClassKey, '', RegisteredDll) then begin
      SuppressibleMsgBox('检测到旧版或手动注册的 EType。请先从旧程序卸载，再运行此安装包。', mbError, MB_OK, IDOK);
      Result := False;
      exit;
    end;
  end;
#endif
  if ExistingDir <> '' then ExistingDir := Canonical(ExistingDir);
  if ExistingDir <> '' then begin
    SuppressibleMsgBox('EType 已安装在 ' + ExistingDir + '。此版本不执行覆盖升级，请先卸载再安装；个人偏好设置会保留。', mbError, MB_OK, IDOK);
    Result := False;
    exit;
  end;
  Result := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Error: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    Error := PathError(WizardDirValue);
    if Error <> '' then begin
      SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Path: String;
begin
  Path := Canonical(WizardDirValue);
  Result := PathError(Path);
  if Result <> '' then exit;
  if not FileUnlocked(AddBackslash(Path) + 'EType.exe') or
    not FileUnlocked(AddBackslash(Path) + 'x64\EType.dll') or
    not FileUnlocked(AddBackslash(Path) + 'x86\EType.dll') then
    Result := 'EType 文件正在使用中。请切换到其他输入法并关闭使用 EType 的窗口及设置程序，再重试。';
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if RegistrationFailed then begin
    Cancel := True;
    Confirm := False;
  end;
end;

procedure FailInstallation(const MessageText: String);
begin
  RegistrationFailed := True;
  Log(MessageText);
  SuppressibleMsgBox(MessageText, mbError, MB_OK, IDOK);
  { Inno catches AfterInstall exceptions, so request its real cancellation path.
    The cancel handler requires a visible form even in /VERYSILENT mode. }
  WizardForm.Show;
  WizardForm.Enabled := True;
  WizardForm.CancelButton.Visible := True;
  WizardForm.CancelButton.Enabled := True;
  WizardForm.CancelButton.OnClick(WizardForm.CancelButton);
end;

procedure RegisterComponents;
var ErrorMessage: String;
begin
#ifdef InstallerTestMode
  if ExpandConstant('{param:FailAfterCopy|0}') = '1' then
    FailInstallation('Validation: injected failure after all payload files were copied.');
#else
  try
    RegisterServer(False, ExpandConstant('{app}\x86\EType.dll'), False);
    RegisterServer(True, ExpandConstant('{app}\x64\EType.dll'), False);
  except
    ErrorMessage := GetExceptionMessage;
    if not UnregisterServer(True, ExpandConstant('{app}\x64\EType.dll'), False) then
      Log('Rollback: x64 registration cleanup requires retry.');
    if not UnregisterServer(False, ExpandConstant('{app}\x86\EType.dll'), False) then
      Log('Rollback: x86 registration cleanup requires retry.');
    FailInstallation('EType 注册失败，安装不能完成：' + ErrorMessage);
  end;
#endif
end;

function InitializeUninstall(): Boolean;
var Attempt: Integer;
begin
  Result := MarkerMatches(ExpandConstant('{app}')) and not HasReparseParent(ExpandConstant('{app}'));
  if not Result then
    SuppressibleMsgBox('安装标记缺失或目录已变为链接。为防止误删，请恢复原安装目录后重试。', mbError, MB_OK, IDOK);
  if Result then begin
    for Attempt := 1 to 10 do begin
      Result := FileUnlocked(ExpandConstant('{app}\EType.exe')) and
        FileUnlocked(ExpandConstant('{app}\x64\EType.dll')) and
        FileUnlocked(ExpandConstant('{app}\x86\EType.dll'));
      if Result then break;
      Sleep(100);
    end;
    if not Result then
      SuppressibleMsgBox('EType 文件正在使用或无法访问，尚未取消注册或删除文件。请关闭 EType 设置窗口，切换到其他输入法并关闭相关软件，确认访问权限后重试。', mbError, MB_OK, IDOK);
  end;
#ifndef InstallerTestMode
  if Result then begin
    Result := FileExists(ExpandConstant('{app}\x64\EType.dll')) and
      FileExists(ExpandConstant('{app}\x86\EType.dll'));
    if Result then
      Result := UnregisterServer(True, ExpandConstant('{app}\x64\EType.dll'), False) and
        UnregisterServer(False, ExpandConstant('{app}\x86\EType.dll'), False);
    if not Result then
      SuppressibleMsgBox('EType 取消注册未完成，尚未删除文件。请恢复缺失的组件文件并重试。', mbError, MB_OK, IDOK);
  end;
#endif
end;

[UninstallDelete]
Type: files; Name: "{app}\etype-installation.id"
Type: dirifempty; Name: "{app}"
