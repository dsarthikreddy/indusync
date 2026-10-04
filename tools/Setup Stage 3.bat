@echo off
cd /d "%~dp0"
title SARTHIK Room - Stage 3 setup
set "PY="
if exist "2_Website_v4\.venv\Scripts\python.exe" set "PY=2_Website_v4\.venv\Scripts\python.exe"
if not defined PY ( where py >nul 2>nul && set "PY=py -3" )
if not defined PY ( where python >nul 2>nul && set "PY=python" )
if not defined PY (
  echo Python was not found. First install the laptop website: 2_Website_v4\Install.bat
  pause
  exit /b 1
)
%PY% "tools\setup_stage3.py"
