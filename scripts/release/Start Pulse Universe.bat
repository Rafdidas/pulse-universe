@echo off
rem Pulse Universe: starts the engine and opens the browser.
rem Press Ctrl+C in this window to stop.
cd /d "%~dp0"
pulse-engine.exe
if errorlevel 1 pause
