@echo off
title SARTHIK Room - Install
cd /d "%~dp0"
echo ================================================================
echo   SARTHIK Room - ONE-TIME INSTALLATION
echo   Needs internet. Takes about 5 to 15 minutes. Do not close this window.
echo ================================================================
echo.
python --version >nul 2>nul
if errorlevel 1 (
  echo [PROBLEM] Python was not found.
  echo Install Python 3.12 from python.org and TICK "Add python.exe to PATH".
  echo Then double-click Install.bat again.
  pause
  exit /b 1
)
for /f "tokens=2" %%v in ('python --version 2^>^&1') do echo Python found: %%v
if not exist ".venv\Scripts\python.exe" (
  echo.
  echo [1/3] Creating a private Python folder for this app ^(.venv^)...
  python -m venv .venv
  if errorlevel 1 (
    echo [PROBLEM] Could not create the .venv folder.
    pause
    exit /b 1
  )
)
echo.
echo [2/3] Downloading and installing the video + AI libraries...
".venv\Scripts\python.exe" -m pip install --upgrade pip
".venv\Scripts\python.exe" -m pip install -r requirements.txt
if errorlevel 1 (
  echo.
  echo [PROBLEM] Installation failed. Check the internet connection and run Install.bat again.
  pause
  exit /b 1
)
echo.
echo [3/3] Downloading the AI models ^(about 25 MB^)...
".venv\Scripts\python.exe" -c "from ultralytics import YOLO; YOLO('yolo11s.pt'); YOLO('yolo11n.pt'); print('AI models ready')"
".venv\Scripts\python.exe" -c "from cctv.faces import FaceEngine; FaceEngine(); print('Face recognition ready')"
if errorlevel 1 (
  echo [PROBLEM] The AI models could not be downloaded. Run Install.bat again.
  pause
  exit /b 1
)
echo.
echo ================================================================
echo   INSTALLATION COMPLETE.
echo   Now double-click  "Start CCTV.bat"
echo ================================================================
pause
