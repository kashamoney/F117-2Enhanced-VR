@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-VR.ps1" -CarrierPractice %*
if errorlevel 1 pause
