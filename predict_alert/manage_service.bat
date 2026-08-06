@echo off
cd /d "%~dp0"

:menu
cls
echo ========================================
echo   ESD Predict Alert Service Manager
echo ========================================
echo.
echo  1. Install autostart (run on Windows login)
echo  2. Uninstall autostart
echo  3. View service status / logs
echo  4. Run once (test)
echo  5. Run loop in this window (debug)
echo  6. Exit
echo.
choice /C 123456 /N /M "Select"
if errorlevel 6 goto end
if errorlevel 5 goto loop
if errorlevel 4 goto once
if errorlevel 3 goto status
if errorlevel 2 goto uninstall
if errorlevel 1 goto install
goto menu

:install
call "%~dp0install_autostart.bat"
goto menu

:uninstall
call "%~dp0uninstall_autostart.bat"
goto menu

:status
call "%~dp0service_status.bat"
goto menu

:once
call "%~dp0run_once.bat"
goto menu

:loop
python "%~dp0predict_service.py" --loop
pause
goto menu

:end
