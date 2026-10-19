@echo off
setlocal
cd /d "%~dp0"
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build\windows-debug\build.ninja cmake -S . -B build\windows-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug || exit /b 1
cmake --build build\windows-debug %* || exit /b 1
