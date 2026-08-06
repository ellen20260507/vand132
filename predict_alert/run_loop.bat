@echo off
cd /d "%~dp0"

python --version >nul 2>&1
if errorlevel 1 (
    echo [错误] 找不到 python，请先安装并加入 PATH。
    pause
    exit /b 1
)

python -c "import pymysql" >nul 2>&1
if errorlevel 1 python -m pip install pymysql

echo 循环模式，每 5 分钟运行一次。按 Ctrl+C 停止。
python "%~dp0predict_service.py" --loop
