@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0generate_syscfg.ps1"
exit /b %errorlevel%
