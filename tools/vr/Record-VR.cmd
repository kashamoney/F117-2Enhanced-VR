@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-VR.ps1" -CarrierPractice -RecordLive %*
if errorlevel 1 pause
