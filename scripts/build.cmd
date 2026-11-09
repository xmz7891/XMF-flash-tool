@echo off
rem Convenience wrapper so the build works without changing the PowerShell
rem execution policy manually.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
