@echo off
title SARTHIK Room - Turn off public link
set "TS=tailscale"
where tailscale >nul 2>nul
if errorlevel 1 set "TS=C:\Program Files\Tailscale\tailscale.exe"
"%TS%" funnel reset
echo.
echo The internet door is CLOSED. Only this laptop, your Wi-Fi
echo and your own Tailscale devices can open the dashboard now.
pause
