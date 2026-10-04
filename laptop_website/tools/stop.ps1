$n = 0
Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='pythonw.exe'" | Where-Object { $_.CommandLine -like '*cctv.main*' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force; $n++ }
Write-Host "Stopped $n SARTHIK Room process(es)."
