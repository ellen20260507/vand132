@echo off
cd /d "%~dp0"

echo ========================================
echo   ESD Predict Alert (test run)
echo ========================================
echo.

python --version >nul 2>&1
if errorlevel 1 goto no_python

echo Python:
python --version
echo.

python -c "import pymysql" >nul 2>&1
if errorlevel 1 (
    echo Installing pymysql...
    python -m pip install pymysql
    if errorlevel 1 goto pip_failed
    echo.
)

echo Running predict_service.py ...
echo.
python "%~dp0predict_service.py" --once
set RC=%ERRORLEVEL%
echo.
if "%RC%"=="0" echo Done. No alert or finished normally.
if "%RC%"=="1" echo Done. Alert written to MySQL table ai_alert.
if not "%RC%"=="0" if not "%RC%"=="1" (
    echo Failed with code %RC%
    echo Check MySQL service and mysql_config.ini
)
goto end

:no_python
echo [ERROR] python not found. Reinstall Python and check "Add to PATH".
goto end

:pip_failed
echo [ERROR] Failed to install pymysql.
echo Run manually: python -m pip install pymysql
goto end

:end
echo.
pause
