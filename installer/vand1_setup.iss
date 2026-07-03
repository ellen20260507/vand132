#define MyAppName "静电管理在线监控系统 B版"
#define MyAppVersion "1.0"
#define MyAppPublisher "ESD-1000"
#define MyAppExeName "vand1.exe"
#define StagingDir "staging"

[Setup]
AppId={{B7E4D1C2-8A5F-4E6B-9D2F-3C6A8B1E4F5D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\ESD-1000-B
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=ESD-1000-B_Setup
SetupIconFile={#StagingDir}\symbol\3HESD.ico
UninstallDisplayIcon={app}\symbol\3HESD.ico
Compression=lzma2/max
SolidCompression=no
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile={#StagingDir}\license.txt
InfoBeforeFile=install_notes.txt

[Tasks]
Name: "desktopicon"; Description: "在桌面创建快捷方式"; GroupDescription: "附加图标:"; Flags: checkedonce

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{app}\logs"; Permissions: users-full

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\symbol\3HESD.ico"
Name: "{group}\打开监控网页"; Filename: "http://localhost:1388"; IconFilename: "{app}\symbol\3HESD.ico"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\symbol\3HESD.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "安装完成后立即启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
