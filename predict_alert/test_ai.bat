@echo off
cd /d "%~dp0"
echo Testing AI connection...
echo.
python "%~dp0predict_service.py" --test-ai
echo.
pause
