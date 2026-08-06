@echo off
chcp 65001 >nul
setlocal

set "PROJECT_DIR=%~dp0"
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"

echo ========================================
echo ESD-1000 installer build (vand1 3-2)
echo ========================================
echo.

echo [1/4] Prepare staging files...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%installer\prepare_staging.ps1"
if errorlevel 1 goto :failed

if not exist "%ISCC%" (
    echo Inno Setup 6 not found.
    echo Install with: winget install --id JRSoftware.InnoSetup -e
    goto :failed
)

echo.
echo [2/4] Build installer...
"%ISCC%" "%PROJECT_DIR%installer\vand1_setup.iss"
if errorlevel 1 goto :failed

echo.
echo [3/4] Build portable ZIP...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%installer\build_portable_zip.ps1"
if errorlevel 1 goto :failed

echo.
echo [4/4] Write checksum info...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%installer\write_checksum.ps1"

echo.
echo Done
echo Installer: %PROJECT_DIR%installer\output\ESD-1000_Setup.exe
echo Portable ZIP: %PROJECT_DIR%installer\output\ESD-1000_Portable.zip
echo Checksum: %PROJECT_DIR%installer\output\checksum.txt
echo.
echo Tip: send the ZIP to customers if WeChat corrupts the setup.exe
echo.
pause
exit /b 0

:failed
echo Build failed.
pause
exit /b 1
