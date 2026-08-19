@echo off
:: 右键此文件 → 以管理员身份运行
chcp 65001 >nul
echo ========================================
echo  ESD-1000 放行防火墙：Ping + 端口 1388
echo ========================================
echo.

net session >nul 2>&1
if errorlevel 1 (
  echo [错误] 请右键本文件，选择「以管理员身份运行」
  pause
  exit /b 1
)

netsh advfirewall firewall delete rule name="ESD-1000 Ping ICMPv4" >nul 2>&1
netsh advfirewall firewall delete rule name="ESD-1000 Display 1388" >nul 2>&1

netsh advfirewall firewall add rule name="ESD-1000 Ping ICMPv4" dir=in action=allow protocol=icmpv4:8,any
netsh advfirewall firewall add rule name="ESD-1000 Display 1388" dir=in action=allow protocol=TCP localport=1388

echo.
echo [完成] 已放行 ICMP 与 TCP 1388
echo.
echo 请在显示机 192.168.0.199 浏览器打开：
echo   http://192.168.0.100:1388
echo.
pause
