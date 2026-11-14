@echo off
rem Builds loot-drop-overlay.dll (Release) with the VS 2022 Build Tools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set CMAKE="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
set PATH=%NINJA_DIR%;%PATH%
cd /d "%~dp0"
%CMAKE% -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
%CMAKE% --build build || exit /b 1
echo BUILD OK: %~dp0build\loot-drop-overlay.dll
