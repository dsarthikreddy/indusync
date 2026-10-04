@echo off
title SARTHIK Room - copy backups to a USB drive (keep it unplugged afterwards)
cd /d "%~dp0"
set /p D=Type the USB drive letter (for example E) and press Enter: 
if "%D%"=="" exit /b
if not exist "%D%:\" ( echo Drive %D%: not found. & pause & exit /b 1 )
robocopy "%~dp0backups" "%D%:\SARTHIK_Room_backups\backups" /E /R:1 /W:1 /NFL /NDL /NP
robocopy "%~dp0data" "%D%:\SARTHIK_Room_backups\data_now" /E /R:1 /W:1 /NFL /NDL /NP /XF *.db-wal *.db-shm server.log
echo.
echo Done. Safely eject the USB drive and keep it away from the laptop (protects against ransomware).
pause
