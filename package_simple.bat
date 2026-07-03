@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ========================================
echo 开始打包程序
echo ========================================

set "SOURCE_DIR=%~dp0release"
set "PACKAGE_DIR=%~dp0package"
set "APP_NAME=vand1"

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%\static"
mkdir "%PACKAGE_DIR%\symbol"
mkdir "%PACKAGE_DIR%\platforms"
mkdir "%PACKAGE_DIR%\sqldrivers"

echo 复制主程序...
copy /y "%SOURCE_DIR%\%APP_NAME%.exe" "%PACKAGE_DIR%\"
echo 复制配置文件...
copy /y "%SOURCE_DIR%\*.ini" "%PACKAGE_DIR%\"
copy /y "%SOURCE_DIR%\*.txt" "%PACKAGE_DIR%\"
echo 复制静态文件...
copy /y "%SOURCE_DIR%\..\static\*.js" "%PACKAGE_DIR%\static\"
echo 复制符号文件...
copy /y "%SOURCE_DIR%\..\symbol\*.png" "%PACKAGE_DIR%\symbol\"
echo 复制DLL文件...
copy /y "%SOURCE_DIR%\*.dll" "%PACKAGE_DIR%\"
echo 复制platforms文件...
copy /y "%SOURCE_DIR%\platforms\*.dll" "%PACKAGE_DIR%\platforms\"
echo 复制sqldrivers文件...
copy /y "%SOURCE_DIR%\sqldrivers\*.dll" "%PACKAGE_DIR%\sqldrivers\"

echo.
echo ========================================
echo 打包完成！
echo 打包目录: %PACKAGE_DIR%
echo 主程序: %PACKAGE_DIR%\%APP_NAME%.exe
echo ========================================
echo.
pause