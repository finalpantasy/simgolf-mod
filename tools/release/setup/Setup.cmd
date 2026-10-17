@echo off
rem Double-click to install SimGolf Remastered. Runs setup.ps1 next to this file.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
if errorlevel 1 pause
