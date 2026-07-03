# 设置中文编码
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "========================================" -ForegroundColor Green
Write-Host "开始打包程序" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

# 设置变量
$SOURCE_DIR = Join-Path $PSScriptRoot "release"
$PACKAGE_DIR = Join-Path $PSScriptRoot "package"
$APP_NAME = "vand1"

# 清理旧的打包目录
if (Test-Path $PACKAGE_DIR) {
    Write-Host "清理旧的打包目录..." -ForegroundColor Yellow
    Remove-Item -Path $PACKAGE_DIR -Recurse -Force
}

# 创建必要的目录
Write-Host "创建打包目录结构..." -ForegroundColor Yellow
New-Item -Path $PACKAGE_DIR -ItemType Directory | Out-Null
New-Item -Path (Join-Path $PACKAGE_DIR "static") -ItemType Directory | Out-Null
New-Item -Path (Join-Path $PACKAGE_DIR "symbol") -ItemType Directory | Out-Null
New-Item -Path (Join-Path $PACKAGE_DIR "platforms") -ItemType Directory | Out-Null
New-Item -Path (Join-Path $PACKAGE_DIR "sqldrivers") -ItemType Directory | Out-Null

# 复制主程序
Write-Host "复制主程序..." -ForegroundColor Yellow
Copy-Item -Path (Join-Path $SOURCE_DIR "$APP_NAME.exe") -Destination $PACKAGE_DIR -Force

# 复制配置文件
Write-Host "复制配置文件..." -ForegroundColor Yellow
Copy-Item -Path (Join-Path $SOURCE_DIR "*.ini") -Destination $PACKAGE_DIR -Force -ErrorAction SilentlyContinue
Copy-Item -Path (Join-Path $SOURCE_DIR "*.txt") -Destination $PACKAGE_DIR -Force -ErrorAction SilentlyContinue

# 复制静态文件
Write-Host "复制静态文件..." -ForegroundColor Yellow
Copy-Item -Path "$PSScriptRoot\static\*" -Destination "$PACKAGE_DIR\static\" -Force -ErrorAction SilentlyContinue

# 复制符号文件
Write-Host "复制符号文件..." -ForegroundColor Yellow
Copy-Item -Path "$PSScriptRoot\symbol\*" -Destination "$PACKAGE_DIR\symbol\" -Force -ErrorAction SilentlyContinue

# 复制DLL文件
Write-Host "复制DLL文件..." -ForegroundColor Yellow
Copy-Item -Path (Join-Path $SOURCE_DIR "*.dll") -Destination $PACKAGE_DIR -Force -ErrorAction SilentlyContinue

# 复制platforms文件
Write-Host "复制platforms文件..." -ForegroundColor Yellow
Copy-Item -Path (Join-Path $SOURCE_DIR "platforms\*.dll") -Destination (Join-Path $PACKAGE_DIR "platforms") -Force -ErrorAction SilentlyContinue

# 复制sqldrivers文件
Write-Host "复制sqldrivers文件..." -ForegroundColor Yellow
Copy-Item -Path (Join-Path $SOURCE_DIR "sqldrivers\*.dll") -Destination (Join-Path $PACKAGE_DIR "sqldrivers") -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "打包完成！" -ForegroundColor Green
Write-Host "打包目录: $PACKAGE_DIR" -ForegroundColor Green
Write-Host "主程序: $(Join-Path $PACKAGE_DIR "$APP_NAME.exe")" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Read-Host "按任意键继续..."