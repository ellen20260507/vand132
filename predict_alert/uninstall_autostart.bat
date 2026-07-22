@echo off
cd /d "%~dp0"
set TASK_NAME=ESD-PredictAlert

echo ========================================
echo   Uninstall ESD Predict Alert Autostart
echo ========================================
echo.

schtasks /Query /TN "%TASK_NAME%" >nul 2>&1
if errorlevel 1 (
    echo Task not found: %TASK_NAME%
    goto cleanup
)

schtasks /Delete /F /TN "%TASK_NAME%"
echo Removed scheduled task: %TASK_NAME%

:cleanup
taskkill /F /IM pythonw.exe /FI "WINDOWTITLE eq predict_service.py*" >nul 2>&1
if exist "%~dp0auto_start.vbs" del /F /Q "%~dp0auto_start.vbs"
echo Done.
echo.
pause
