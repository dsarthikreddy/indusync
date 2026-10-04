@echo off
title SARTHIK Room - RUNNING (close this window to stop)
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  echo This app is not installed yet. Double-click Install.bat first.
  pause
  exit /b 1
)
".venv\Scripts\python.exe" -m cctv.main
echo.
echo The CCTV server has stopped. You can close this window.
pause
