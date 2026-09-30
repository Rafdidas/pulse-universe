@echo off
rem Pulse Universe with administrator rights: thread-to-core lines are measured instead of estimated.
rem A Windows permission prompt (UAC) appears. Press Ctrl+C in the new window to stop.
cd /d "%~dp0"
powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~dp0pulse-engine.exe' -WorkingDirectory '%~dp0'"
