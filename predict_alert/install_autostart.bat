@echo off
cd /d "%~dp0"
set TASK_NAME=ESD-PredictAlert
set VBS_FILE=%~dp0auto_start.vbs

echo ========================================
echo   Install ESD Predict Alert Autostart
echo ========================================
echo.

python --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found. Install Python first.
    pause
    exit /b 1
)

set "PY_EXE="
for /f "delims=" %%i in ('where pythonw 2^>nul') do (
    set "PY_EXE=%%i"
    goto found_py
)
for /f "delims=" %%i in ('where python 2^>nul') do (
    set "PY_EXE=%%i"
    goto found_py
)
:found_py
if "%PY_EXE%"=="" (
    echo [ERROR] Cannot find pythonw.exe or python.exe
    pause
    exit /b 1
)

echo Using Python: %PY_EXE%
echo Task name   : %TASK_NAME%
echo Work dir    : %~dp0
echo.

if not exist "%~dp0logs" mkdir "%~dp0logs"

python -c "import pymysql" >nul 2>&1
if errorlevel 1 (
    echo Installing pymysql...
    python -m pip install pymysql
)

> "%VBS_FILE%" echo Set shell = CreateObject("WScript.Shell")
>> "%VBS_FILE%" echo shell.CurrentDirectory = "%~dp0"
>> "%VBS_FILE%" echo shell.Run """%PY_EXE%"" ""%~dp0predict_service.py"" --service", 0, False

schtasks /Create /F /TN "%TASK_NAME%" /SC ONLOGON /RL LIMITED /TR "wscript.exe //B \"%VBS_FILE%\""
if errorlevel 1 (
    echo [ERROR] Failed to create scheduled task.
    pause
    exit /b 1
)

echo.
echo Installed successfully.
echo - Runs automatically when you log in to Windows
echo - Runs in background (no black window)
echo - Log file: %~dp0logs\predict_alert.log
echo.
echo You can manage it in: taskschd.msc
echo Task name: %TASK_NAME%
echo.
echo Start now? (Y/N)
choice /C YN /N
if errorlevel 2 goto end
if errorlevel 1 wscript.exe //B "%VBS_FILE%"

:end
echo.
pause
