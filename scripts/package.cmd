@echo off
setlocal EnableExtensions EnableDelayedExpansion
call "%~dp0build-release.cmd" || exit /b 1
set "ISCC_EXE="
for %%I in (ISCC.exe) do set "ISCC_EXE=%%~$PATH:I"
if not exist "%ISCC_EXE%" set "ISCC_EXE=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_EXE%" set "ISCC_EXE=C:\Program Files\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_EXE%" (
  echo Inno Setup 6 was not found.
  exit /b 2
)
"%ISCC_EXE%" "%~dp0..\installer\PdfToImage.iss" || exit /b 1
for /f "skip=1 tokens=* delims=" %%H in ('certutil -hashfile "%~dp0..\release\PdfToImage-Setup-1.0.0.exe" SHA256 ^| findstr /v /c:"CertUtil"') do set "SETUP_HASH=%%H"
set "SETUP_HASH=!SETUP_HASH: =!"
>"%~dp0..\release\SHA256SUMS.txt" echo !SETUP_HASH! *PdfToImage-Setup-1.0.0.exe
echo SHA256 !SETUP_HASH!
endlocal
