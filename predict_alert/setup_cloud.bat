@echo off
chcp 65001 >nul 2>&1
cd /d "%~dp0"
echo.
python "%~dp0setup_cloud.py"
echo.
pause
