@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0read-battery.ps1" %*
set "gmk70_battery_result=%ERRORLEVEL%"
popd
exit /b %gmk70_battery_result%
