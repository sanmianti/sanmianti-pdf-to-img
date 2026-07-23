#define MyAppName "PDF 图片转换"
#define MyAppVersion "1.0.0"

[Setup]
AppId={{C6E128B9-47C6-46C2-9A8F-DF303E3E5094}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=PdfToImage
DefaultDirName={localappdata}\Programs\PdfToImage
DefaultGroupName={#MyAppName}
PrivilegesRequired=lowest
MinVersion=6.1sp1
ArchitecturesAllowed=x86compatible
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
OutputDir=..\release
OutputBaseFilename=PdfToImage-Setup-1.0.0
UninstallDisplayIcon={app}\PdfToImage.exe
LicenseFile=..\third_party\pdfium\LICENSE

[Languages]
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: checkedonce

[Files]
Source: "..\build\Release\PdfToImage.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\PdfWorker.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\pdfium.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\pdfium\LICENSE"; DestDir: "{app}"; DestName: "THIRD_PARTY_NOTICES.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\PdfToImage.exe"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\PdfToImage.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\PdfToImage.exe"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent
