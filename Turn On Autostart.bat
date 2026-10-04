@echo off
title SARTHIK Room - start automatically
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\make_shortcuts.ps1"
echo.
echo From now on the website starts by itself when you sign in to Windows.
echo Open it any time with the "SARTHIK Room" icon on your Desktop, or your saved link.
pause
