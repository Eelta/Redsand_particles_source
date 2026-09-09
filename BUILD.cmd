@echo off
setlocal
set VSLANG=1033
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1" %*
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" echo Build failed with exit code %RESULT%.
if "%~1"=="" pause
exit /b %RESULT%
