@echo off
title SARTHIK Room - Make public link (free, Tailscale Funnel)
cd /d "%~dp0"
set "TS=tailscale"
where tailscale >nul 2>nul
if errorlevel 1 set "TS=C:\Program Files\Tailscale\tailscale.exe"
"%TS%" version >nul 2>nul
if errorlevel 1 (
  echo [PROBLEM] Tailscale is not installed on this laptop.
  echo Install it free from https://tailscale.com/download/windows , sign in,
  echo then double-click this file again.
  pause
  exit /b 1
)
echo ================================================================
echo   Opening the internet door to your CLASSYNC dashboard (port 8000)
echo   Visitors still need an APPROVED account to see anything inside.
echo ================================================================
echo.
echo If a link starting with https://login.tailscale.com appears below:
echo   1. open it in your browser   2. click Enable / Approve
echo   3. come back here (it continues by itself, or run this file again)
echo.
"%TS%" funnel --bg 8000
echo.
echo ---------------- current public link ----------------
"%TS%" funnel status
echo -----------------------------------------------------
echo Your public address is the https://....ts.net line above.
echo It never changes and keeps working after restarts.
echo Now open the dashboard: Settings, switch "Public link ON".
pause
