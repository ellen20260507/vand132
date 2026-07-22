@echo off
cd /d "%~dp0"
set TASK_NAME=ESD-PredictAlert

echo ========================================
echo   ESD Predict Alert Service Status
echo ========================================
echo.

schtasks /Query /TN "%TASK_NAME%" /FO LIST /V 2>nul
if errorlevel 1 (
    echo [NOT INSTALLED] Scheduled task "%TASK_NAME%" does not exist.
    echo Run install_autostart.bat to install.
) else (
    echo.
    echo Task is installed.
)

echo.
echo Recent log (last 20 lines):
echo ----------------------------------------
if exist "%~dp0logs\predict_alert.log" (
    powershell -NoProfile -Command "Get-Content -Path '%~dp0logs\predict_alert.log' -Tail 20 -Encoding UTF8"
) else (
    echo Log file not found yet.
)
echo ----------------------------------------
echo.
pause
