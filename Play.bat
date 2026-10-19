@echo off
rem cxmx-mod-lod-114d by cxmx-dev, CC BY-NC-SA 4.0
rem Play.bat - starts cxmx-mod fullscreen through PlugY (single player and LAN).
cd /d "%~dp0"
if not exist "PlugY.exe" (echo PlugY.exe not found. See README.md. & pause & exit /b 1)
start "" PlugY.exe -direct -txt -3dfx
