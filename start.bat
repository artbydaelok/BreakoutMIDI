@echo off
cd /d "%~dp0"
echo Starting Breakout MIDI server...
node server.js
pause
