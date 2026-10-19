@echo off
rem cxmx-mod-lod-114d by cxmx-dev, CC BY-NC-SA 4.0
rem Play-Windowed.bat - starts cxmx-mod in a window (D2GL reads d2gl_windowed.ini).
cd /d "%~dp0"
if not exist "PlugY.exe" (echo PlugY.exe not found. See README.md. & pause & exit /b 1)
start "" PlugY.exe -direct -txt -3dfx -config windowed
