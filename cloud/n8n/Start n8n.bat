@echo off
cd /d "%~dp0"
title SARTHIK Room - n8n (keep this window open, you may minimise it)
where npx >nul 2>nul || ( echo Node.js is missing. Install the LTS version from https://nodejs.org then run this again. & pause & exit /b 1 )
rem n8n listens ONLY on this laptop, never on the network. Workflows are imported by "Setup Stage 3.bat".
set "N8N_LISTEN_ADDRESS=127.0.0.1"
set "N8N_HOST=localhost"
set "N8N_PORT=5678"
set "N8N_DIAGNOSTICS_ENABLED=false"
set "N8N_PERSONALIZATION_ENABLED=false"
set "N8N_USER_FOLDER=%~dp0n8n_data"
echo Opening http://localhost:5678 in 25 seconds (press F5 there if it is not ready yet)...
start "" /b cmd /c "timeout /t 25 >nul & start http://localhost:5678"
call npx -y n8n start
pause
