@echo off
rem Simple packaging script for vand1 application

set "APP_NAME=vand1"
set "RELEASE_DIR=%CD%\release"
set "PACKAGE_DIR=%CD%\package"

rem Clean previous package
if exist "%PACKAGE_DIR%" (
    echo Cleaning previous package directory...
    rmdir /s /q "%PACKAGE_DIR%"
)

rem Create directory structure
echo Creating package directory structure...
mkdir "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%\static"
mkdir "%PACKAGE_DIR%\symbol"
mkdir "%PACKAGE_DIR%\platforms"
mkdir "%PACKAGE_DIR%\sqldrivers"

rem Copy main executable
echo Copying main executable...
copy /y "%RELEASE_DIR%\%APP_NAME%.exe" "%PACKAGE_DIR%\"

rem Copy configuration files
echo Copying configuration files...
copy /y "%RELEASE_DIR%\*.ini" "%PACKAGE_DIR%\"
copy /y "%RELEASE_DIR%\*.txt" "%PACKAGE_DIR%\"

rem Copy static files
echo Copying static files...
copy /y "%CD%\static\*.js" "%PACKAGE_DIR%\static\"

rem Copy symbol files
echo Copying symbol files...
copy /y "%CD%\symbol\*.png" "%PACKAGE_DIR%\symbol\"

rem Copy DLL files
echo Copying DLL files...
copy /y "%RELEASE_DIR%\*.dll" "%PACKAGE_DIR%\"

rem Copy platforms files
echo Copying platforms files...
copy /y "%RELEASE_DIR%\platforms\*.dll" "%PACKAGE_DIR%\platforms\"

rem Copy sqldrivers files
echo Copying sqldrivers files...
copy /y "%RELEASE_DIR%\sqldrivers\*.dll" "%PACKAGE_DIR%\sqldrivers\"

echo.
echo Packaging completed successfully!
echo Package directory: %PACKAGE_DIR%
echo Main executable: %PACKAGE_DIR%\%APP_NAME%.exe
echo.
echo Press any key to continue...