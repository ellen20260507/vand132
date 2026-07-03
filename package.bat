@echo off
chcp 65001 >nul
echo ========================================
echo 开始打包程序
echo ========================================

set "SOURCE_DIR=D:\QT\object\vand1\release"
set "PACKAGE_DIR=D:\QT\object\vand1\deploy_v2"
set "APP_NAME=vand1"

echo.
echo [1/5] 清理旧的打包目录...
if exist "%PACKAGE_DIR%" (
    rmdir /s /q "%PACKAGE_DIR%"
)
mkdir "%PACKAGE_DIR%"

echo.
echo [2/5] 复制主程序...
copy "%SOURCE_DIR%\%APP_NAME%.exe" "%PACKAGE_DIR%\" /y >nul
if not exist "%PACKAGE_DIR%\%APP_NAME%.exe" (
    echo 错误：无法复制主程序！
    pause
    exit /b 1
)

echo.
echo [3/5] 复制配置文件...
if exist "%SOURCE_DIR%\server.ini" (
    copy "%SOURCE_DIR%\server.ini" "%PACKAGE_DIR%\" /y >nul
    echo 已复制 server.ini
)
if exist "%SOURCE_DIR%\poll_config.txt" (
    copy "%SOURCE_DIR%\poll_config.txt" "%PACKAGE_DIR%\" /y >nul
    echo 已复制 poll_config.txt
)
if exist "%SOURCE_DIR%\config.json" (
    copy "%SOURCE_DIR%\config.json" "%PACKAGE_DIR%\" /y >nul
    echo 已复制 config.json
)

echo.
echo [4/5] 复制OpenCV DLL文件...
copy "%SOURCE_DIR%\libopencv_*.dll" "%PACKAGE_DIR%\" /y >nul
copy "%SOURCE_DIR%\opencv_ffmpeg348_64.dll" "%PACKAGE_DIR%\" /y >nul
echo 已复制 OpenCV DLL文件

echo.
echo [5/5] 复制Qt DLL文件...
copy "%SOURCE_DIR%\Qt5*.dll" "%PACKAGE_DIR%\" /y >nul
echo 已复制 Qt DLL文件

echo.
echo 复制platforms插件...
mkdir "%PACKAGE_DIR%\platforms" >nul 2>&1
copy "%SOURCE_DIR%\platforms\*.dll" "%PACKAGE_DIR%\platforms\" /y >nul
echo 已复制 platforms插件

echo.
echo 复制SQL驱动插件...
mkdir "%PACKAGE_DIR%\plugins" >nul 2>&1
mkdir "%PACKAGE_DIR%\plugins\sqldrivers" >nul 2>&1
copy "%SOURCE_DIR%\plugins\sqldrivers\qsqlmysql.dll" "%PACKAGE_DIR%\plugins\sqldrivers\" /y >nul
copy "%SOURCE_DIR%\plugins\sqldrivers\qsqlite.dll" "%PACKAGE_DIR%\plugins\sqldrivers\" /y >nul
echo 已复制 SQL驱动插件

echo.
echo 复制MySQL库文件...
if exist "C:\Utils\my_sql\mysql-5.7.25-winx64\lib\libmysql.dll" (
    copy "C:\Utils\my_sql\mysql-5.7.25-winx64\lib\libmysql.dll" "%PACKAGE_DIR%\" /y >nul
    echo 已复制 libmysql.dll
) else (
    echo 警告：未找到libmysql.dll，尝试从其他位置复制...
    if exist "%SOURCE_DIR%\libmysql.dll" (
        copy "%SOURCE_DIR%\libmysql.dll" "%PACKAGE_DIR%\" /y >nul
        echo 已从源码目录复制 libmysql.dll
    )
)

echo.
echo 复制静态资源文件...
mkdir "%PACKAGE_DIR%\static" >nul 2>&1
if exist "%SOURCE_DIR%\static\echarts.min.js" (
    copy "%SOURCE_DIR%\static\echarts.min.js" "%PACKAGE_DIR%\static\" /y >nul
    echo 已复制 echarts.min.js
)

echo.
echo 复制符号资源文件...
mkdir "%PACKAGE_DIR%\symbol" >nul 2>&1
if exist "%SOURCE_DIR%\symbol\" (
    xcopy "%SOURCE_DIR%\symbol\" "%PACKAGE_DIR%\symbol\" /s /y >nul
    echo 已复制符号资源文件
)

echo.
echo ========================================
echo 打包完成！
echo ========================================
echo.
echo 打包目录: %PACKAGE_DIR%
echo 主程序: %APP_NAME%.exe
echo.
echo 请将整个deploy_v2文件夹复制到目标机器上运行
echo.
pause