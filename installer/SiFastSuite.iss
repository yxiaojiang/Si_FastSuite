; ==============================================================================
; Inno Setup 6 Script for SiFastSuite OLP Standalone Installer
; Packages build\dist into single-file setup: SiFastSuiteOLP_Setup_v0.1.0.exe
; ==============================================================================

#define MyAppName "SiFastSuite OLP"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "SiFastSuite Team"
#define MyAppURL "https://github.com/yxiaojiang/Si_FastSuite"
#define MyAppExeName "SiFastSuiteOLP.exe"

[Setup]
; Unique application GUID for Windows Add/Remove Programs
AppId={{C6B5E86E-35F7-4180-87F8-E3E374E51F7D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; Destination installation directory (64-bit Program Files)
DefaultDirName={autopf}\SiFastSuite OLP
DefaultGroupName={#MyAppName}
AllowNoIcons=yes

; Output binary configuration
OutputDir=..\build\installer
OutputBaseFilename=SiFastSuiteOLP_Setup_v0.1.0
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

; Privileges and elevation
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline

; Modern UI setup
WizardStyle=modern
DisableProgramGroupPage=yes

; Uninstaller configuration
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
CreateUninstallRegKey=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Full standalone distribution payload
Source: "..\build\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Sample Scenes"; Filename: "{app}\SampleScenes"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Ensure clean uninstallation with zero leftovers
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\SampleScenes"
Type: filesandordirs; Name: "{app}\resources"
Type: files; Name: "{app}\*.ls"
Type: files; Name: "{app}\*.html"
Type: files; Name: "{app}\*.log"
Type: files; Name: "{app}\qt.conf"
Type: dirifempty; Name: "{app}"
