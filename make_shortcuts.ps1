# Creates: a Desktop icon "SARTHIK Room" and a Startup entry that starts the website silently at sign-in.
$dir = Split-Path -Parent $PSScriptRoot
$vbs = Join-Path $dir 'SARTHIK Room.vbs'
$wscript = Join-Path $env:WINDIR 'System32\wscript.exe'
$sh = New-Object -ComObject WScript.Shell
$startup = Join-Path ([Environment]::GetFolderPath('Startup')) 'SARTHIK Room (start silently).lnk'
$l = $sh.CreateShortcut($startup)
$l.TargetPath = $wscript; $l.Arguments = "`"$vbs`" /background"; $l.WorkingDirectory = $dir
$l.Description = 'Starts the SARTHIK Room website when you sign in'; $l.Save()
$desk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'SARTHIK Room.lnk'
$d = $sh.CreateShortcut($desk)
$d.TargetPath = $wscript; $d.Arguments = "`"$vbs`""; $d.WorkingDirectory = $dir
$d.IconLocation = (Join-Path $env:WINDIR 'System32\shell32.dll') + ',13'; $d.Description = 'Open SARTHIK Room'; $d.Save()
Write-Host "OK: Desktop icon 'SARTHIK Room' and automatic start at sign-in are ready."
