@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash_precompiled.ps1" %*
pause
