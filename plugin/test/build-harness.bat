@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /O2 /DWIN32_LEAN_AND_MEAN /DNOMINMAX harness.cpp ..\src\drop_feed.cpp ..\src\http_server.cpp ..\src\tally.cpp ..\src\grail.cpp ..\src\item_tables.cpp /Fe:harness.exe /link ws2_32.lib || exit /b 1
echo HARNESS OK
