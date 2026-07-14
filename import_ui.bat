@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0import_ui.ps1" %*
pause
