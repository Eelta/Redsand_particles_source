@echo off
if exist "C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe" (
 "C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0ClearCache.ps1"
) else (
 powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ClearCache.ps1"
)
pause
