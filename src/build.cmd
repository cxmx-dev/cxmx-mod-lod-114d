@echo off
rem cxmx-mod-lod-114d by cxmx-dev, CC BY-NC-SA 4.0
rem build.cmd - builds cxmxdev.dll (32-bit) with the Visual Studio C compiler.
rem Usage: build.cmd [OUTDIR]   Default OUTDIR is the build folder next to src.
rem Needs Visual Studio or Build Tools with the C++ x86 tools. Set VCVARS to your vcvars32.bat if it is not found.
setlocal
set "SRC=%~dp0cxmxdev.c"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCVARS if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (echo ERROR: vcvars32.bat not found. Set VCVARS to its path. & exit /b 1)
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%~dp0..\build"
if not exist "%OUT%" mkdir "%OUT%"
call "%VCVARS%" >nul || (echo ERROR: vcvars failed & exit /b 1)
copy /y "%SRC%" "%OUT%\cxmxdev.c" >nul
pushd "%OUT%"
cl /nologo /O1 /LD cxmxdev.c user32.lib kernel32.lib /Fe:cxmxdev.dll /link /Brepro
set "RC=%ERRORLEVEL%"
del /q cxmxdev.obj cxmxdev.exp cxmxdev.lib cxmxdev.c 2>nul
popd
if not "%RC%"=="0" (echo ERROR: cl failed %RC% & exit /b %RC%)
echo Built: %OUT%\cxmxdev.dll
certutil -hashfile "%OUT%\cxmxdev.dll" SHA256 | findstr /v /c:"CertUtil"
endlocal
